#import "InternalSynth.h"
#import "AmySynth.h"

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/MusicDevice.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <math.h>
#include <pthread.h>
#include <sys/stat.h>
#include <limits.h>

#import <AudioToolbox/AUAudioUnit.h>
#import <AVFAudio/AVAudioEngine.h>
#import <AVFAudio/AVAudioFormat.h>
#import <AVFAudio/AVAudioMixerNode.h>
#import <AVFAudio/AVAudioMixing.h>
#import <AVFAudio/AVAudioSourceNode.h>
#import <AVFAudio/AVAudioUnit.h>
#import <AVFAudio/AVAudioUnitEQ.h>
#import <AVFAudio/AVAudioUnitReverb.h>
#import <AVFAudio/AVAudioUnitEffect.h>

#define kMaxNumberOfPorts 2

static AVAudioEngine *sEngine = nil;
static AVAudioUnit *sDLSUnit[2];
static AudioUnit sDLS[2];
static MIDIEndpointRef sInternalDest[2];
static MIDIEndpointRef sAmyDest = 0;
/* Web Editor ports: created only while the MIDI-menu switch is ON. */
static MIDIEndpointRef sAmyEditorIn = 0;
static MIDIEndpointRef sAmyEditorOut = 0;
static int sAmyEditorHold = 0;
static int sAmyPort = -1;
static AVAudioSourceNode *sAmyNode = nil;
static MIDIClientRef sSynthMIDIClient = 0;
static AUScheduleMIDIEventBlock sScheduleMIDI[2];
static double sSampleRate[2];
static double sHostTicksPerSecond;
static pthread_mutex_t sSynthLock = PTHREAD_MUTEX_INITIALIZER;
static char sDLSFontFile[PATH_MAX];
static AVAudioMixerNode *sSum = nil;
static AVAudioUnitEQ *sEQ = nil;
static AVAudioUnitReverb *sReverb = nil;
static AVAudioUnitEffect *sLimiter = nil;
static int sAppliedReverbPreset = -1;
static DLSEffectSettings sEffects = {
	0.0f, 0.0f, 0.0f, 3, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1
};
static int sUseInternalReverb = 1;
static int sReverbRoomType = 3;

/*  Render thread publishes, player thread reads. seqlock: odd means a write is in progress. */
typedef struct {
	_Atomic uint32_t seq;
	int64_t sampleTime;
	uint64_t hostTime;
} SynthAnchor;

static SynthAnchor sAnchor[2];

static void
StoreAnchor(int idx, int64_t sampleTime, uint64_t hostTime)
{
	SynthAnchor *a = &sAnchor[idx];
	uint32_t s = atomic_load_explicit(&a->seq, memory_order_relaxed);
	atomic_store_explicit(&a->seq, s + 1, memory_order_relaxed);
	a->sampleTime = sampleTime;
	a->hostTime = hostTime;
	atomic_store_explicit(&a->seq, s + 2, memory_order_release);
}

static int
LoadAnchor(int idx, int64_t *sampleTime, uint64_t *hostTime)
{
	SynthAnchor *a = &sAnchor[idx];
	uint32_t s1, s2;
	int64_t sample;
	uint64_t host;
	int spins = 0;

	do {
		s1 = atomic_load_explicit(&a->seq, memory_order_acquire);
		if (s1 & 1) {
			if (++spins > 8)
				return 0;
			continue;
		}
		sample = a->sampleTime;
		host = a->hostTime;
		s2 = atomic_load_explicit(&a->seq, memory_order_acquire);
	} while (s1 != s2);
	if (s1 == 0 || host == 0)
		return 0;
	*sampleTime = sample;
	*hostTime = host;
	return 1;
}

static OSStatus
InternalSynthRenderNotify(void *inRefCon,
	AudioUnitRenderActionFlags *ioActionFlags,
	const AudioTimeStamp *inTimeStamp,
	UInt32 inBusNumber,
	UInt32 inNumberFrames,
	AudioBufferList *ioData)
{
	int idx = (int)(intptr_t)inRefCon;
	(void)inBusNumber;
	(void)inNumberFrames;
	(void)ioData;
	if (idx < 0 || idx > 1 || ioActionFlags == NULL || inTimeStamp == NULL)
		return noErr;
	if ((*ioActionFlags & kAudioUnitRenderAction_PreRender) == 0)
		return noErr;
	if ((inTimeStamp->mFlags & kAudioTimeStampSampleTimeValid) == 0)
		return noErr;
	if ((inTimeStamp->mFlags & kAudioTimeStampHostTimeValid) == 0)
		return noErr;
	StoreAnchor(idx, (int64_t)inTimeStamp->mSampleTime, (uint64_t)inTimeStamp->mHostTime);
	return noErr;
}

static void
CacheScheduleBlock(int idx, AVAudioUnit *unit)
{
	AUScheduleMIDIEventBlock block;
	if (unit == nil)
		return;
	block = unit.AUAudioUnit.scheduleMIDIEventBlock;
	if (block == nil)
		return;
	if (sScheduleMIDI[idx] != nil)
		[sScheduleMIDI[idx] release];
	sScheduleMIDI[idx] = [block copy];
}

static void
ReleaseScheduleBlocks(void)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (sScheduleMIDI[i] != nil) {
			[sScheduleMIDI[i] release];
			sScheduleMIDI[i] = nil;
		}
	}
}

static double
SynthSampleRate(AudioUnit au, AVAudioEngine *engine)
{
	Float64 sr = 0;
	UInt32 sz = sizeof sr;
	if (AudioUnitGetProperty(au, kAudioUnitProperty_SampleRate,
			kAudioUnitScope_Output, 0, &sr, &sz) != noErr || sr <= 0) {
		sz = sizeof sr;
		sr = 0;
		AudioUnitGetProperty(au, kAudioUnitProperty_SampleRate,
			kAudioUnitScope_Global, 0, &sr, &sz);
	}
	if (sr <= 0 && engine != nil)
		sr = [engine.outputNode outputFormatForBus:0].sampleRate;
	return sr;
}

static OSStatus ScheduleToInternalSynth(int synth, const uint8_t *bytes, int length, MIDITimeStamp atTime);
static void MirrorToAmyEditorOut(const uint8_t *bytes, int length, MIDITimeStamp atTime);
static int AmyAudioWanted(void);

/*  Playback prefetches at most one second. Timestamps farther out are not from that clock. */
int
InternalSynthSchedule(MIDIEndpointRef dest, const uint8_t *bytes, int length,
	MIDITimeStamp atTime, OSStatus *status)
{
	int synth;

	if (dest == 0 || bytes == NULL || length <= 0)
		return 0;
	if (dest == sAmyDest) {
		if (status != NULL)
			*status = noErr;
		AmySynthSendMIDI(bytes, length, atTime);
		/* Mirror STed2→AMY playback to the Editor OUT source when open.
		   Editor INPUT is not echoed (avoids Web MIDI feedback). */
		MirrorToAmyEditorOut(bytes, length, atTime);
		return 1;
	}
	synth = -1;
	if (dest == sInternalDest[0])
		synth = 0;
	else if (dest == sInternalDest[1])
		synth = 1;
	if (synth < 0)
		return 0;
	if (status != NULL)
		*status = ScheduleToInternalSynth(synth, bytes, length, atTime);
	return 1;
}

static OSStatus
ScheduleToInternalSynth(int synth, const uint8_t *bytes, int length, MIDITimeStamp atTime)
{
	static int sMissingLogged;
	AUScheduleMIDIEventBlock schedule;
	AUEventSampleTime when;
	int64_t sampleTime;
	uint64_t hostTime;
	int64_t deltaTicks;
	double deltaSeconds;

	if (synth < 0 || synth > 1 || bytes == NULL || length <= 0)
		return -1;
	pthread_mutex_lock(&sSynthLock);
	schedule = sScheduleMIDI[synth];
	if (schedule == NULL) {
		pthread_mutex_unlock(&sSynthLock);
		if (!sMissingLogged) {
			NSLog(@"internal synth scheduleMIDIEventBlock is nil");
			sMissingLogged = 1;
		}
		return -1;
	}
	when = AUEventSampleTimeImmediate;
	if (atTime != 0 && sSampleRate[synth] > 0 && sHostTicksPerSecond > 0
		&& LoadAnchor(synth, &sampleTime, &hostTime)) {
		deltaTicks = (int64_t)(atTime - hostTime);
		deltaSeconds = (double)deltaTicks / sHostTicksPerSecond;
		if (deltaSeconds >= 0.0 && deltaSeconds <= 2.0)
			when = sampleTime + (AUEventSampleTime)llround(deltaSeconds * sSampleRate[synth]);
	}
	schedule(when, 0, length, bytes);
	pthread_mutex_unlock(&sSynthLock);
	return noErr;
}

static AVAudioUnit *
InstantiateDLS(void)
{
	AudioComponentDescription desc;
	__block AVAudioUnit *made = nil;
	static dispatch_semaphore_t sem;

	memset(&desc, 0, sizeof desc);
	desc.componentType = kAudioUnitType_MusicDevice;
	desc.componentSubType = kAudioUnitSubType_DLSSynth;
	desc.componentManufacturer = kAudioUnitManufacturer_Apple;
	if (sem == NULL)
		sem = dispatch_semaphore_create(0);
	[AVAudioUnit instantiateWithComponentDescription:desc
		options:kAudioComponentInstantiation_LoadInProcess
		completionHandler:^(AVAudioUnit *au, NSError *error) {
			if (au != nil)
				made = [au retain];
			else
				NSLog(@"DLS synth instantiate failed: %@", error);
			dispatch_semaphore_signal(sem);
		}];
	dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
	return made;
}

/* Drop same-named virtual endpoints we own from a prior client / double
   create. keep is retained; all other matches are disposed. */
static void
DisposeEndpointsNamedExcept(CFStringRef name, MIDIEndpointRef keep, int asSource)
{
	MIDIEndpointRef found[32];
	int nFound = 0;
	ItemCount count, i;

	count = asSource ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
	for (i = 0; i < count && nFound < 32; i++) {
		MIDIEndpointRef e = asSource ? MIDIGetSource(i) : MIDIGetDestination(i);
		CFStringRef ename = NULL;
		if (e == 0 || e == keep)
			continue;
		if (MIDIObjectGetStringProperty(e, kMIDIPropertyName, &ename) != noErr || ename == NULL)
			continue;
		if (CFStringCompare(ename, name, 0) == kCFCompareEqualTo)
			found[nFound++] = e;
		CFRelease(ename);
	}
	for (i = 0; i < (ItemCount)nFound; i++) {
		OSStatus sts = MIDIEndpointDispose(found[i]);
		if (sts != noErr)
			NSLog(@"dispose orphan %@: %d", name, (int)sts);
	}
}

static int
AddInternalDestination(MIDIEndpointRef *dest, CFStringRef name)
{
	OSStatus sts;
	/*  The name stays visible as a destination. Playback does not arrive here:
	    CoreMIDISend writes the chosen synth directly so host time is not
	    mistaken for a sample offset. */
	if (*dest == 0)
		DisposeEndpointsNamedExcept(name, 0, 0);
	sts = MIDIDestinationCreateWithProtocol(sSynthMIDIClient, name, kMIDIProtocol_1_0,
		dest, ^(const MIDIEventList *evtlist, void *srcConnRefCon) {
			(void)evtlist;
			(void)srcConnRefCon;
		});
	if (sts != noErr) {
		NSLog(@"MIDI destination %@ failed: %d", name, (int)sts);
		*dest = 0;
		return 0;
	}
	DisposeEndpointsNamedExcept(name, *dest, 0);
	return 1;
}

static void
ClearAnchors(void)
{
	int i;
	for (i = 0; i < 2; i++) {
		sAnchor[i].sampleTime = 0;
		sAnchor[i].hostTime = 0;
		sSampleRate[i] = 0;
		atomic_store_explicit(&sAnchor[i].seq, 0, memory_order_release);
	}
}

static DLSEffectSettings ClampEffects(DLSEffectSettings s);

/*  Caller holds sSynthLock. Frequencies stay fixed; only the stored gains move. */
static void
ApplyEffectsLocked(void)
{
	BOOL bypass = sEffects.totalEffect == 0;

	if (sEQ != nil) {
		sEQ.bypass = bypass;
		sEQ.globalGain = sEffects.globalGain;
		if ([sEQ.bands count] >= 2) {
			AVAudioUnitEQFilterParameters *low = [sEQ.bands objectAtIndex:0];
			AVAudioUnitEQFilterParameters *high = [sEQ.bands objectAtIndex:1];
			low.filterType = AVAudioUnitEQFilterTypeLowShelf;
			low.frequency = 60.0;
			low.bandwidth = 1.0;
			low.gain = sEffects.bassGain;
			low.bypass = NO;
			high.filterType = AVAudioUnitEQFilterTypeHighShelf;
			high.frequency = 5500.0;
			high.bandwidth = 1.0;
			high.gain = sEffects.trebleGain;
			high.bypass = NO;
		}
	}
	if (sReverb != nil) {
		sReverb.bypass = bypass;
		if (sAppliedReverbPreset != sEffects.reverbPreset) {
			[sReverb loadFactoryPreset:(AVAudioUnitReverbPreset)sEffects.reverbPreset];
			sAppliedReverbPreset = sEffects.reverbPreset;
		}
		sReverb.wetDryMix = sEffects.reverbMix;
	}
	if (sLimiter != nil) {
		sLimiter.bypass = bypass;
		AudioUnitSetParameter(sLimiter.audioUnit, kLimiterParam_PreGain,
			kAudioUnitScope_Global, 0, sEffects.limiterBoost, 0);
	}
	/*  Per-source levels into sSum, before the shared effect chain. */
	if (sDLSUnit[0] != nil && sAmyPort != 0)
		((id<AVAudioMixing>)sDLSUnit[0]).volume = sEffects.dlsVolume;
	if (sDLSUnit[1] != nil && sAmyPort != 1)
		((id<AVAudioMixing>)sDLSUnit[1]).volume = sEffects.dlsVolume;
	if (sAmyNode != nil)
		sAmyNode.volume = sEffects.amyVolume;
	if (sEngine != nil)
		sEngine.mainMixerNode.outputVolume = sEffects.volume;
}

static int
AmyAudioWanted(void)
{
	return (sAmyPort >= 0 || sAmyEditorHold) ? 1 : 0;
}

static void
MirrorToAmyEditorOut(const uint8_t *bytes, int length, MIDITimeStamp atTime)
{
	Byte buffer[512];
	MIDIPacketList *packetList = (MIDIPacketList *)buffer;
	MIDIPacket *packet;

	if (sAmyEditorOut == 0 || bytes == NULL || length <= 0)
		return;
	if (bytes[0] == 0xF0)
		return;
	if (length > 256)
		length = 256;
	packet = MIDIPacketListInit(packetList);
	packet = MIDIPacketListAdd(packetList, sizeof buffer, packet, atTime,
		(ByteCount)length, bytes);
	if (packet != NULL)
		(void)MIDIReceived(sAmyEditorOut, packetList);
}

static void
ReleaseEffectNodes(void)
{
	[sSum release];
	[sEQ release];
	[sReverb release];
	[sLimiter release];
	[sAmyNode release];
	sSum = nil;
	sEQ = nil;
	sReverb = nil;
	sLimiter = nil;
	sAmyNode = nil;
	sAppliedReverbPreset = -1;
}

static BOOL
ConnectNode(AVAudioEngine *engine, AVAudioNode *src, AVAudioNode *dst, AVAudioNodeBus bus, NSError **err)
{
	if (@available(macOS 27.0, *))
		return [engine connect:src to:dst fromBus:0 toBus:bus format:nil error:err];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	[engine connect:src to:dst fromBus:0 toBus:bus format:nil];
#pragma clang diagnostic pop
	if (err != NULL)
		*err = nil;
	return YES;
}

/*  Caller holds sSynthLock. Render callbacks do not take that lock. */
static void
StopInternalSynthEngine(void)
{
	AVAudioEngine *engine;
	AVAudioUnit *u0, *u1;

	if (sEngine == nil) {
		AmySynthStop();
		return;
	}
	engine = sEngine;
	u0 = sDLSUnit[0];
	u1 = sDLSUnit[1];
	[engine stop];
	if (u0 != nil)
		AudioUnitRemoveRenderNotify(u0.audioUnit, InternalSynthRenderNotify, (void *)(intptr_t)0);
	if (u1 != nil)
		AudioUnitRemoveRenderNotify(u1.audioUnit, InternalSynthRenderNotify, (void *)(intptr_t)1);
	ReleaseScheduleBlocks();
	ClearAnchors();
	sDLS[0] = NULL;
	sDLS[1] = NULL;
	sDLSUnit[0] = nil;
	sDLSUnit[1] = nil;
	sEngine = nil;
	ReleaseEffectNodes();
	AmySynthStop();
	[engine release];
	[u0 release];
	[u1 release];
}

static void LoadDLSSoundBank(AudioUnit au);

static int
ClampReverbRoomType(int room)
{
	if (room < 0 || room > 12)
		return 3;
	return room;
}

/*  Caller holds sSynthLock. use is written first so the room type lands while reverb is on. */
static void
ApplyDLSInternalReverbToUnit(AudioUnit au)
{
	UInt32 use, room;

	if (au == NULL)
		return;
	use = sUseInternalReverb ? 1u : 0u;
	room = (UInt32)sReverbRoomType;
	AudioUnitSetProperty(au, kMusicDeviceProperty_UsesInternalReverb,
		kAudioUnitScope_Global, 0, &use, sizeof use);
	AudioUnitSetProperty(au, kAudioUnitProperty_ReverbRoomType,
		kAudioUnitScope_Global, 0, &room, sizeof room);
}

static void
ApplyInternalReverbLocked(void)
{
	ApplyDLSInternalReverbToUnit(sDLS[0]);
	ApplyDLSInternalReverbToUnit(sDLS[1]);
}

/*  Caller holds sSynthLock. */
static void
StartInternalSynthEngine(void)
{
	AVAudioUnit *u0, *u1;
	NSError *err = nil;
	SInt32 proto = kMIDIProtocol_1_0;

	if (sEngine != nil)
		return;
	ClearAnchors();
	u0 = InstantiateDLS();
	u1 = InstantiateDLS();
	if (u0 == nil || u1 == nil) {
		[u0 release];
		[u1 release];
		return;
	}
	LoadDLSSoundBank(u0.audioUnit);
	LoadDLSSoundBank(u1.audioUnit);
	ApplyDLSInternalReverbToUnit(u0.audioUnit);
	ApplyDLSInternalReverbToUnit(u1.audioUnit);
	AVAudioMixerNode *sum;
	AVAudioUnitEQ *eq;
	AVAudioUnitReverb *reverb;
	AVAudioUnitEffect *limiter;
	NSError *connectErr = nil;
	BOOL connected = NO;
	AVAudioNodeBus outBus;

	/*  Both DLS units share one chain: sum → EQ → reverb → limiter → main mixer. */
	sEngine = [[AVAudioEngine alloc] init];
	sum = [[AVAudioMixerNode alloc] init];
	eq = [[AVAudioUnitEQ alloc] initWithNumberOfBands:2];
	reverb = [[AVAudioUnitReverb alloc] init];
	{
		AudioComponentDescription limDesc;
		memset(&limDesc, 0, sizeof limDesc);
		limDesc.componentType = kAudioUnitType_Effect;
		limDesc.componentSubType = kAudioUnitSubType_PeakLimiter;
		limDesc.componentManufacturer = kAudioUnitManufacturer_Apple;
		limiter = [[AVAudioUnitEffect alloc] initWithAudioComponentDescription:limDesc];
	}
	[sEngine attachNode:u0];
	[sEngine attachNode:u1];
	[sEngine attachNode:sum];
	[sEngine attachNode:eq];
	[sEngine attachNode:reverb];
	[sEngine attachNode:limiter];
	AudioUnitSetProperty(u0.audioUnit, kAudioUnitProperty_HostMIDIProtocol,
		kAudioUnitScope_Global, 0, &proto, sizeof proto);
	AudioUnitSetProperty(u1.audioUnit, kAudioUnitProperty_HostMIDIProtocol,
		kAudioUnitScope_Global, 0, &proto, sizeof proto);
	sum.outputVolume = 1.0;
	if (AmyAudioWanted()) {
		AVAudioFormat *fmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:AMY_SYNTH_SAMPLE_RATE channels:2];
		sAmyNode = [[AVAudioSourceNode alloc] initWithFormat:fmt renderBlock:^OSStatus(
			BOOL *isSilence, const AudioTimeStamp *timestamp, AVAudioFrameCount frameCount, AudioBufferList *outputData) {
			uint64_t hostTime = 0;
			float *left;
			float *right;
			int stride;
			if (timestamp != NULL && (timestamp->mFlags & kAudioTimeStampHostTimeValid) != 0)
				hostTime = timestamp->mHostTime;
			if (outputData->mNumberBuffers >= 2) {
				left = outputData->mBuffers[0].mData;
				right = outputData->mBuffers[1].mData;
				stride = 1;
			} else {
				left = outputData->mBuffers[0].mData;
				right = left + 1;
				stride = 2;
			}
			AmySynthFill(left, right, stride, (int)frameCount, hostTime);
			if (isSilence != NULL)
				*isSilence = NO;
			return noErr;
		}];
		[sEngine attachNode:sAmyNode];
		[fmt release];
	}
	if (sum == nil || eq == nil || reverb == nil || limiter == nil || (AmyAudioWanted() && sAmyNode == nil)) {
		NSLog(@"internal synth effect unit missing");
		[sEngine release];
		sEngine = nil;
		[u0 release];
		[u1 release];
		[sum release];
		[eq release];
		[reverb release];
		[limiter release];
		[sAmyNode release];
		sAmyNode = nil;
		return;
	}
	connected = YES;
	if (sAmyPort != 0)
		connected = ConnectNode(sEngine, u0, sum, 0, &connectErr);
	if (connected && sAmyPort != 1)
		connected = ConnectNode(sEngine, u1, sum, 1, &connectErr);
	if (connected && sAmyNode != nil) {
		/* STed2 output select uses bus 0/1. Editor-only hold uses bus 2 so
		   both DLS units stay connected. */
		AVAudioNodeBus amyBus = (sAmyPort >= 0) ? (AVAudioNodeBus)sAmyPort : 2;
		AVAudioFormat *fmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:AMY_SYNTH_SAMPLE_RATE channels:2];
		if (@available(macOS 27.0, *))
			connected = [sEngine connect:sAmyNode to:sum fromBus:0 toBus:amyBus format:fmt error:&connectErr];
		else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			[sEngine connect:sAmyNode to:sum fromBus:0 toBus:amyBus format:fmt];
#pragma clang diagnostic pop
		}
		[fmt release];
	}
	if (connected)
		connected = ConnectNode(sEngine, sum, eq, 0, &connectErr);
	if (connected)
		connected = ConnectNode(sEngine, eq, reverb, 0, &connectErr);
	if (connected)
		connected = ConnectNode(sEngine, reverb, limiter, 0, &connectErr);
	if (connected) {
		outBus = sEngine.mainMixerNode.nextAvailableInputBus;
		connected = ConnectNode(sEngine, limiter, sEngine.mainMixerNode, outBus, &connectErr);
	}
	if (!connected) {
		NSLog(@"internal synth connect failed: %@", connectErr);
		[sEngine release];
		sEngine = nil;
		[u0 release];
		[u1 release];
		[sum release];
		[eq release];
		[reverb release];
		[limiter release];
		[sAmyNode release];
		sAmyNode = nil;
		return;
	}
	sSum = sum;
	sEQ = eq;
	sReverb = reverb;
	sLimiter = limiter;
	ApplyEffectsLocked();
	CacheScheduleBlock(0, u0);
	CacheScheduleBlock(1, u1);
	AudioUnitAddRenderNotify(u0.audioUnit, InternalSynthRenderNotify, (void *)(intptr_t)0);
	AudioUnitAddRenderNotify(u1.audioUnit, InternalSynthRenderNotify, (void *)(intptr_t)1);
	sHostTicksPerSecond = AudioGetHostClockFrequency();
	if (AmyAudioWanted())
		AmySynthStart();
	if (![sEngine startAndReturnError:&err]) {
		NSLog(@"internal synth start failed: %@", err);
		AudioUnitRemoveRenderNotify(u0.audioUnit, InternalSynthRenderNotify, (void *)(intptr_t)0);
		AudioUnitRemoveRenderNotify(u1.audioUnit, InternalSynthRenderNotify, (void *)(intptr_t)1);
		ReleaseScheduleBlocks();
		ReleaseEffectNodes();
		AmySynthStop();
		[sEngine release];
		sEngine = nil;
		[u0 release];
		[u1 release];
		return;
	}
	sSampleRate[0] = SynthSampleRate(u0.audioUnit, sEngine);
	sSampleRate[1] = SynthSampleRate(u1.audioUnit, sEngine);
	if (sScheduleMIDI[0] == nil)
		CacheScheduleBlock(0, u0);
	if (sScheduleMIDI[1] == nil)
		CacheScheduleBlock(1, u1);
	if (sScheduleMIDI[0] == nil || sScheduleMIDI[1] == nil)
		NSLog(@"internal synth MIDI schedule block missing");
	sDLSUnit[0] = u0;
	sDLSUnit[1] = u1;
	sDLS[0] = u0.audioUnit;
	sDLS[1] = u1.audioUnit;
	ApplyInternalReverbLocked();
}

int
InternalSynthIsAmyDestination(MIDIEndpointRef dest)
{
	return dest != 0 && dest == sAmyDest;
}

int
InternalSynthAnyDestination(const MIDIEndpointRef *dests)
{
	int i, j;
	for (i = 0; i < kMaxNumberOfPorts; i++) {
		if (dests[i] == 0)
			continue;
		if (dests[i] == sAmyDest)
			return 1;
		for (j = 0; j < 2; j++) {
			if (sInternalDest[j] != 0 && dests[i] == sInternalDest[j])
				return 1;
		}
	}
	return 0;
}

static void
ApplyAmyPortLocked(int port)
{
	if (sAmyPort == port)
		return;
	sAmyPort = port;
	if (sEngine != nil) {
		StopInternalSynthEngine();
		StartInternalSynthEngine();
	}
}

static void
ApplyAmyPortMain(const MIDIEndpointRef *dests)
{
	int port = -1;
	int i;
	for (i = 0; i < kMaxNumberOfPorts; i++) {
		if (dests[i] != 0 && dests[i] == sAmyDest)
			port = i;
	}
	pthread_mutex_lock(&sSynthLock);
	ApplyAmyPortLocked(port);
	pthread_mutex_unlock(&sSynthLock);
}

void
InternalSynthApplyAmyPort(const MIDIEndpointRef *dests)
{
	if ([NSThread isMainThread])
		ApplyAmyPortMain(dests);
	else
		dispatch_sync(dispatch_get_main_queue(), ^{ ApplyAmyPortMain(dests); });
}

static void
EnsureInternalSynthRunningMain(void)
{
	pthread_mutex_lock(&sSynthLock);
	if (sEngine == nil)
		StartInternalSynthEngine();
	pthread_mutex_unlock(&sSynthLock);
}

static void
EnsureInternalSynthStoppedMain(void)
{
	pthread_mutex_lock(&sSynthLock);
	StopInternalSynthEngine();
	pthread_mutex_unlock(&sSynthLock);
}

void
InternalSynthEnsureRunning(void)
{
	if ([NSThread isMainThread])
		EnsureInternalSynthRunningMain();
	else
		dispatch_sync(dispatch_get_main_queue(), ^{ EnsureInternalSynthRunningMain(); });
}

void
InternalSynthEnsureStopped(void)
{
	if ([NSThread isMainThread])
		EnsureInternalSynthStoppedMain();
	else
		dispatch_sync(dispatch_get_main_queue(), ^{ EnsureInternalSynthStoppedMain(); });
}

void
InternalSynthCreateDestinations(MIDIClientRef client)
{
	sSynthMIDIClient = client;
	if (sInternalDest[0] == 0)
		AddInternalDestination(&sInternalDest[0], CFSTR("Apple DLS Synth"));
	if (sInternalDest[1] == 0)
		AddInternalDestination(&sInternalDest[1], CFSTR("Apple DLS Synth 2"));
	if (sAmyDest == 0)
		AddInternalDestination(&sAmyDest, CFSTR("AMY Synthesizer"));
	/* Keep exactly one of each even if a previous client left orphans. */
	DisposeEndpointsNamedExcept(CFSTR("Apple DLS Synth"), sInternalDest[0], 0);
	DisposeEndpointsNamedExcept(CFSTR("Apple DLS Synth 2"), sInternalDest[1], 0);
	DisposeEndpointsNamedExcept(CFSTR("AMY Synthesizer"), sAmyDest, 0);
}

int
InternalSynthIsAmyEditorEndpoint(MIDIEndpointRef endpoint)
{
	if (endpoint == 0)
		return 0;
	return (endpoint == sAmyEditorIn || endpoint == sAmyEditorOut) ? 1 : 0;
}

int
InternalSynthIsPlaybackListDestination(MIDIEndpointRef endpoint)
{
	CFStringRef name = NULL;
	int ours;

	if (endpoint == 0)
		return 0;
	if (endpoint == sAmyEditorIn || endpoint == sAmyEditorOut)
		return 0;
	if (endpoint == sInternalDest[0] || endpoint == sInternalDest[1] || endpoint == sAmyDest)
		return 1;
	/* Hide orphan copies of our virtual destinations (double Init / stale
	   clients). Matching is by kMIDIPropertyName, not display name. */
	if (MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &name) != noErr || name == NULL)
		return 1;
	ours = (CFStringCompare(name, CFSTR("Apple DLS Synth"), 0) == kCFCompareEqualTo
		|| CFStringCompare(name, CFSTR("Apple DLS Synth 2"), 0) == kCFCompareEqualTo
		|| CFStringCompare(name, CFSTR("AMY Synthesizer"), 0) == kCFCompareEqualTo
		|| CFStringCompare(name, CFSTR("AMY EDITOR INPUT"), 0) == kCFCompareEqualTo);
	CFRelease(name);
	return ours ? 0 : 1;
}

int
InternalSynthAmyEditorPortsOpen(void)
{
	return sAmyEditorHold ? 1 : 0;
}

/*
  Editor ports use classic MIDIDestinationCreate / MIDISourceCreate
  (MIDIPacketList), not CreateWithProtocol. Chrome Web MIDI enumerates
  those reliably; MIDI Monitor sees both, but Audio MIDI Setup never
  lists app-created virtual endpoints (IAC only) — that is normal.
*/
static void
AmyEditorInputReadProc(const MIDIPacketList *pktlist, void *readProcRefCon, void *srcConnRefCon)
{
	const MIDIPacket *packet;
	UInt32 i;

	(void)readProcRefCon;
	(void)srcConnRefCon;
	if (pktlist == NULL || !sAmyEditorHold)
		return;
	packet = &pktlist->packet[0];
	for (i = 0; i < pktlist->numPackets; i++) {
		if (packet->length > 0)
			AmySynthSendMIDI(packet->data, (int)packet->length, 0);
		packet = MIDIPacketNext(packet);
	}
}

static SInt32
StableEditorUniqueID(CFStringRef defaultsKey, SInt32 fallback)
{
	NSString *key = (NSString *)defaultsKey;
	NSUserDefaults *def = [NSUserDefaults standardUserDefaults];
	SInt32 uid = (SInt32)[def integerForKey:key];

	if (uid == 0) {
		uid = fallback;
		[def setInteger:uid forKey:key];
	}
	return uid;
}

static void
PublishAmyEditorEndpoint(MIDIEndpointRef endpoint, int isInput)
{
	SInt32 uid;

	if (endpoint == 0)
		return;
	MIDIObjectSetIntegerProperty(endpoint, kMIDIPropertyOffline, 0);
	MIDIObjectSetStringProperty(endpoint, kMIDIPropertyManufacturer, CFSTR("shore pine sound systems"));
	MIDIObjectSetStringProperty(endpoint, kMIDIPropertyModel, CFSTR("STed2 AMY Editor"));
	/* Stable unique IDs help Chrome / Web MIDI keep the port across relaunches. */
	uid = StableEditorUniqueID(
		isInput ? CFSTR("AmyEditorInputUniqueID") : CFSTR("AmyEditorOutputUniqueID"),
		isInput ? (SInt32)0x53544531 : (SInt32)0x53544532); /* STE1 / STE2 */
	(void)MIDIObjectSetIntegerProperty(endpoint, kMIDIPropertyUniqueID, uid);
}

static void
DisposeAmyEditorPorts(void)
{
	if (sAmyEditorIn != 0) {
		MIDIEndpointDispose(sAmyEditorIn);
		sAmyEditorIn = 0;
	}
	if (sAmyEditorOut != 0) {
		MIDIEndpointDispose(sAmyEditorOut);
		sAmyEditorOut = 0;
	}
}

static int
CreateAmyEditorPorts(void)
{
	OSStatus sts;

	if (sSynthMIDIClient == 0)
		return 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	if (sAmyEditorIn == 0) {
		DisposeEndpointsNamedExcept(CFSTR("AMY EDITOR INPUT"), 0, 0);
		sts = MIDIDestinationCreate(sSynthMIDIClient, CFSTR("AMY EDITOR INPUT"),
			AmyEditorInputReadProc, NULL, &sAmyEditorIn);
		if (sts != noErr) {
			NSLog(@"AMY EDITOR INPUT create failed: %d", (int)sts);
			sAmyEditorIn = 0;
			return 0;
		}
		PublishAmyEditorEndpoint(sAmyEditorIn, 1);
		DisposeEndpointsNamedExcept(CFSTR("AMY EDITOR INPUT"), sAmyEditorIn, 0);
	}
	if (sAmyEditorOut == 0) {
		DisposeEndpointsNamedExcept(CFSTR("AMY EDITOR OUTPUT"), 0, 1);
		sts = MIDISourceCreate(sSynthMIDIClient, CFSTR("AMY EDITOR OUTPUT"), &sAmyEditorOut);
		if (sts != noErr) {
			NSLog(@"AMY EDITOR OUTPUT create failed: %d", (int)sts);
			DisposeAmyEditorPorts();
			return 0;
		}
		PublishAmyEditorEndpoint(sAmyEditorOut, 0);
		DisposeEndpointsNamedExcept(CFSTR("AMY EDITOR OUTPUT"), sAmyEditorOut, 1);
	}
#pragma clang diagnostic pop
	NSLog(@"AMY Editor ports published (IN=%u OUT=%u) destCount=%u srcCount=%u",
		(unsigned)sAmyEditorIn, (unsigned)sAmyEditorOut,
		(unsigned)MIDIGetNumberOfDestinations(),
		(unsigned)MIDIGetNumberOfSources());
	return 1;
}

static void
ApplyAmyEditorPortsMain(int on)
{
	int want = on ? 1 : 0;
	int hadNode;

	if (want == sAmyEditorHold && ((want != 0) == (sAmyEditorIn != 0)))
		return;

	/* Publish/dispose endpoints outside the audio lock so CoreMIDI can
	   register them without waiting on DLS instantiate. */
	if (want) {
		sAmyEditorHold = 1;
		if (!CreateAmyEditorPorts()) {
			sAmyEditorHold = 0;
			return;
		}
		pthread_mutex_lock(&sSynthLock);
		hadNode = (sAmyNode != nil);
		if (sEngine == nil)
			StartInternalSynthEngine();
		else if (!hadNode) {
			StopInternalSynthEngine();
			StartInternalSynthEngine();
		}
		pthread_mutex_unlock(&sSynthLock);
	} else {
		DisposeAmyEditorPorts();
		sAmyEditorHold = 0;
		pthread_mutex_lock(&sSynthLock);
		if (sEngine != nil && sAmyPort < 0)
			StopInternalSynthEngine();
		pthread_mutex_unlock(&sSynthLock);
	}
}

void
InternalSynthSetAmyEditorPortsOpen(int on)
{
	if ([NSThread isMainThread])
		ApplyAmyEditorPortsMain(on);
	else
		dispatch_sync(dispatch_get_main_queue(), ^{ ApplyAmyEditorPortsMain(on); });
}

static int
IsDLSBankFilename(const char *name)
{
	const char *dot;
	if (name == NULL || name[0] == 0)
		return 0;
	if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL)
		return 0;
	dot = strrchr(name, '.');
	if (dot == NULL)
		return 0;
	return strcasecmp(dot, ".sf2") == 0 || strcasecmp(dot, ".dls") == 0;
}

/*  Empty path keeps the unit's built-in bank. A missing file is the same. */
static void
ResolveDLSFontPath(const char *filename, char *path, size_t pathSize)
{
	NSString *full;
	struct stat st;
	path[0] = 0;
	if (!IsDLSBankFilename(filename))
		return;
	full = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Audio/Sounds/Banks"];
	full = [full stringByAppendingPathComponent:[NSString stringWithUTF8String:filename]];
	if (full == nil || [full length] == 0 || [full length] >= pathSize)
		return;
	strlcpy(path, [full fileSystemRepresentation], pathSize);
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
		path[0] = 0;
}

static void
LoadDLSSoundBank(AudioUnit au)
{
	CFURLRef url;
	OSStatus sts;
	if (au == NULL || sDLSFontFile[0] == 0)
		return;
	url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
		(const UInt8 *)sDLSFontFile, (CFIndex)strlen(sDLSFontFile), false);
	if (url == NULL)
		return;
	sts = AudioUnitSetProperty(au, kMusicDeviceProperty_SoundBankURL,
		kAudioUnitScope_Global, 0, &url, sizeof url);
	CFRelease(url);
	if (sts != noErr)
		NSLog(@"DLS sound bank load failed: %d", (int)sts);
}

static void
ApplyDLSFontPath(const char *path)
{
	if (path == NULL)
		path = "";
	pthread_mutex_lock(&sSynthLock);
	if (strcmp(sDLSFontFile, path) != 0) {
		strlcpy(sDLSFontFile, path, sizeof sDLSFontFile);
		if (sEngine != nil) {
			StopInternalSynthEngine();
			StartInternalSynthEngine();
		}
	}
	pthread_mutex_unlock(&sSynthLock);
}

void
CoreMIDISetDLSSynthFont(const char *filename)
{
	char *path = malloc(PATH_MAX);
	if (path == NULL)
		return;
	ResolveDLSFontPath(filename, path, PATH_MAX);
	if ([NSThread isMainThread]) {
		ApplyDLSFontPath(path);
		free(path);
	} else {
		dispatch_sync(dispatch_get_main_queue(), ^{
			ApplyDLSFontPath(path);
			free(path);
		});
	}
}

static DLSEffectSettings
ClampEffects(DLSEffectSettings s)
{
	if (s.globalGain < -36.0f)
		s.globalGain = -36.0f;
	if (s.globalGain > 0.0f)
		s.globalGain = 0.0f;
	if (s.bassGain < -6.0f)
		s.bassGain = -6.0f;
	if (s.bassGain > 12.0f)
		s.bassGain = 12.0f;
	if (s.trebleGain < -6.0f)
		s.trebleGain = -6.0f;
	if (s.trebleGain > 12.0f)
		s.trebleGain = 12.0f;
	if (s.reverbPreset < 0)
		s.reverbPreset = 0;
	if (s.reverbPreset > 12)
		s.reverbPreset = 12;
	if (s.reverbMix < 0.0f)
		s.reverbMix = 0.0f;
	if (s.reverbMix > 100.0f)
		s.reverbMix = 100.0f;
	if (s.limiterBoost < 0.0f)
		s.limiterBoost = 0.0f;
	if (s.limiterBoost > 6.0f)
		s.limiterBoost = 6.0f;
	if (s.dlsVolume < 0.0f)
		s.dlsVolume = 0.0f;
	if (s.dlsVolume > 1.0f)
		s.dlsVolume = 1.0f;
	if (s.amyVolume < 0.0f)
		s.amyVolume = 0.0f;
	if (s.amyVolume > 1.0f)
		s.amyVolume = 1.0f;
	if (s.volume < 0.0f)
		s.volume = 0.0f;
	if (s.volume > 1.0f)
		s.volume = 1.0f;
	s.totalEffect = s.totalEffect != 0;
	return s;
}

static void
ApplyEffectsNow(DLSEffectSettings s)
{
	pthread_mutex_lock(&sSynthLock);
	sEffects = ClampEffects(s);
	ApplyEffectsLocked();
	pthread_mutex_unlock(&sSynthLock);
}

void
CoreMIDIGetDLSEffects(DLSEffectSettings *settings)
{
	if (settings == NULL)
		return;
	pthread_mutex_lock(&sSynthLock);
	*settings = sEffects;
	pthread_mutex_unlock(&sSynthLock);
}

void
CoreMIDISetDLSEffects(const DLSEffectSettings *settings)
{
	DLSEffectSettings s;
	if (settings == NULL)
		return;
	s = *settings;
	if ([NSThread isMainThread])
		ApplyEffectsNow(s);
	else
		dispatch_sync(dispatch_get_main_queue(), ^{ ApplyEffectsNow(s); });
}

static void
ApplyInternalReverbNow(int useReverb, int roomType)
{
	pthread_mutex_lock(&sSynthLock);
	sUseInternalReverb = useReverb != 0;
	sReverbRoomType = ClampReverbRoomType(roomType);
	ApplyInternalReverbLocked();
	pthread_mutex_unlock(&sSynthLock);
}

void
CoreMIDIGetDLSInternalReverb(DLSInternalReverb *settings)
{
	if (settings == NULL)
		return;
	pthread_mutex_lock(&sSynthLock);
	settings->useReverb = sUseInternalReverb;
	settings->roomType = sReverbRoomType;
	pthread_mutex_unlock(&sSynthLock);
}

void
CoreMIDISetDLSInternalReverb(const DLSInternalReverb *settings)
{
	DLSInternalReverb s;
	if (settings == NULL)
		return;
	s = *settings;
	if ([NSThread isMainThread])
		ApplyInternalReverbNow(s.useReverb, s.roomType);
	else
		dispatch_sync(dispatch_get_main_queue(), ^{
			ApplyInternalReverbNow(s.useReverb, s.roomType);
		});
}

const char *
CoreMIDIDLSSynthFontFilename(void)
{
	static char name[256];
	const char *base;

	pthread_mutex_lock(&sSynthLock);
	base = strrchr(sDLSFontFile, '/');
	if (base != NULL)
		base++;
	else
		base = sDLSFontFile;
	strlcpy(name, base, sizeof name);
	pthread_mutex_unlock(&sSynthLock);
	return name;
}

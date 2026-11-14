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
#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>

#include "rcp_transport.h"
#include "midi_in.h"

#define kMaxNumberOfPorts 2

static AVAudioEngine *sEngine = nil;
static AVAudioUnit *sDLSUnit[2];
static AudioUnit sDLS[2];
static MIDIEndpointRef sInternalDest[2];
static MIDIEndpointRef sAmyDest = 0;
static int sAmyPort = -1;
static AVAudioSourceNode *sAmyNode = nil;
static MIDIClientRef sSynthMIDIClient = 0;
static AUScheduleMIDIEventBlock sScheduleMIDI[2];
static double sSampleRate[2];
static double sHostTicksPerSecond;
static pthread_mutex_t sSynthLock = PTHREAD_MUTEX_INITIALIZER;
static char sDLSFontFile[2][PATH_MAX];
static AVAudioMixerNode *sSum = nil;
static AVAudioUnitEQ *sEQ = nil;
static AVAudioUnitReverb *sReverb = nil;
static AVAudioUnitEffect *sLimiter = nil;
static int sAppliedReverbPreset = -1;
static DLSEffectSettings sEffects = {
	0.0f, 0.0f, 0.0f, 3, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1
};
static int sUseInternalReverb = 1;
static int sReverbRoomType = 3;
/* 1 while a MIDI port still targets an internal synth. */
static int sEngineWanted = 0;
static id sConfigObserver = nil;
static int sEngineGeneration = 0;
static int sRecoveryBusy = 0;
static int sRecoveryNeeded = 0;
static int sRecoveryRetryLeft = 0;
static int sRecoveryStoppedTransport = 0;
static int sRecoveryStartOK = 0;
static int sRecoveryQuietScheduled = 0;

static void RecoveryRun(void);
static void InternalSynthNoteConfigurationChange(int generation);

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

/*  GM / GM2 / GS / XG reset fingerprints. Device ID bytes are wildcards. */
static int
IsDlsResetSysex(const uint8_t *b, int n)
{
	if (b == NULL || n < 6 || b[0] != 0xf0 || b[n - 1] != 0xf7)
		return 0;
	/* GM System On (09 01) / GM2 System On (09 03): F0 7E dd 09 vv F7 */
	if (n == 6 && b[1] == 0x7e && b[3] == 0x09 && (b[4] == 0x01 || b[4] == 0x03))
		return 1;
	/* GS Reset: F0 41 dd 42 12 40 00 7F 00 ck F7 */
	if (n == 11 && b[1] == 0x41 && b[3] == 0x42 && b[4] == 0x12
	    && b[5] == 0x40 && b[6] == 0x00 && b[7] == 0x7f && b[8] == 0x00)
		return 1;
	/* XG System On: F0 43 dd 4C 00 00 7E 00 F7 */
	if (n == 9 && b[1] == 0x43 && b[3] == 0x4c
	    && b[4] == 0x00 && b[5] == 0x00 && b[6] == 0x7e && b[7] == 0x00)
		return 1;
	return 0;
}

static void
ScheduleShortMIDI(AUScheduleMIDIEventBlock schedule, AUEventSampleTime when,
	uint8_t s0, uint8_t s1, uint8_t s2, int len)
{
	uint8_t msg[3];

	msg[0] = s0;
	msg[1] = s1;
	msg[2] = s2;
	schedule(when, 0, len, msg);
}

/*  DLS ignores reset SysEx; approximate GM-ish channel defaults instead.
    Panel USE/Room for internal reverb is left alone — only CC#91 is set. */
static void
InjectDlsChannelDefaults(AUScheduleMIDIEventBlock schedule, AUEventSampleTime when)
{
	int ch;

	for (ch = 0; ch < 16; ch++) {
		uint8_t cc = (uint8_t)(0xb0 | ch);
		uint8_t pc = (uint8_t)(0xc0 | ch);
		uint8_t pb = (uint8_t)(0xe0 | ch);

		ScheduleShortMIDI(schedule, when, cc, 120, 0, 3);   /* All Sound Off */
		ScheduleShortMIDI(schedule, when, cc, 123, 0, 3);   /* All Notes Off */
		ScheduleShortMIDI(schedule, when, cc, 121, 0, 3);   /* Reset All Controllers */
		ScheduleShortMIDI(schedule, when, cc, 0, 0, 3);     /* Bank MSB */
		ScheduleShortMIDI(schedule, when, cc, 32, 0, 3);    /* Bank LSB */
		ScheduleShortMIDI(schedule, when, pc, 0, 0, 2);     /* Program */
		ScheduleShortMIDI(schedule, when, cc, 7, 100, 3);   /* Volume */
		ScheduleShortMIDI(schedule, when, cc, 11, 127, 3);  /* Expression */
		ScheduleShortMIDI(schedule, when, cc, 10, 64, 3);   /* Pan */
		ScheduleShortMIDI(schedule, when, pb, 0, 64, 3);    /* Pitch Bend center */
		ScheduleShortMIDI(schedule, when, cc, 91, 40, 3);   /* Reverb Send */
		ScheduleShortMIDI(schedule, when, cc, 93, 0, 3);    /* Chorus Send */
	}
}

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
		/* Same lock as amy_stop / amy_start in the configuration rebuild. */
		pthread_mutex_lock(&sSynthLock);
		AmySynthSendMIDI(bytes, length, atTime);
		pthread_mutex_unlock(&sSynthLock);
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
	if (bytes[0] == 0xf0 && IsDlsResetSysex(bytes, length)) {
		InjectDlsChannelDefaults(schedule, when);
		pthread_mutex_unlock(&sSynthLock);
		return noErr;
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

static int
AddInternalDestination(MIDIEndpointRef *dest, CFStringRef name)
{
	OSStatus sts;
	/*  The name stays visible as a destination. Playback does not arrive here:
	    CoreMIDISend writes the chosen synth directly so host time is not
	    mistaken for a sample offset. */
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
		((id<AVAudioMixing>)sDLSUnit[1]).volume = sEffects.dlsVolume2;
	if (sAmyNode != nil)
		sAmyNode.volume = sEffects.amyVolume;
	if (sEngine != nil)
		sEngine.mainMixerNode.outputVolume = sEffects.volume;
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

/*  Drops in-flight configuration notifications for the engine being released.
    The block only hops to the main queue; it must not run on the audio queue. */
static void
UnwatchEngineConfiguration(void)
{
	sEngineGeneration++;
	if (sConfigObserver == nil)
		return;
	[[NSNotificationCenter defaultCenter] removeObserver:sConfigObserver];
	[sConfigObserver release];
	sConfigObserver = nil;
}

static void
WatchEngineConfiguration(AVAudioEngine *engine)
{
	int generation;

	UnwatchEngineConfiguration();
	if (engine == nil)
		return;
	generation = ++sEngineGeneration;
	sConfigObserver = [[[NSNotificationCenter defaultCenter]
		addObserverForName:AVAudioEngineConfigurationChangeNotification
		object:engine
		queue:nil
		usingBlock:^(NSNotification *note) {
			(void)note;
			dispatch_async(dispatch_get_main_queue(), ^{
				InternalSynthNoteConfigurationChange(generation);
			});
		}] retain];
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
	UnwatchEngineConfiguration();
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

static void LoadDLSSoundBank(AudioUnit au, int port);

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
	LoadDLSSoundBank(u0.audioUnit, 0);
	LoadDLSSoundBank(u1.audioUnit, 1);
	ApplyDLSInternalReverbToUnit(u0.audioUnit);
	ApplyDLSInternalReverbToUnit(u1.audioUnit);
	AVAudioMixerNode *sum;
	AVAudioUnitEQ *eq;
	AVAudioUnitReverb *reverb;
	AVAudioUnitEffect *limiter;
	NSError *connectErr = nil;
	BOOL connected = NO;
	AVAudioNodeBus outBus;

	/*  Both DLS units share one chain: sum → EQ → reverb → limiter → main mixer.
	    The configuration observer is installed only after start succeeds.
	    Watching earlier treats the first hardware format as a device change,
	    stops this engine, and then drops that notification. */
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
	if (sAmyPort == 0 || sAmyPort == 1) {
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
	if (sum == nil || eq == nil || reverb == nil || limiter == nil || (sAmyPort >= 0 && sAmyNode == nil)) {
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
		AVAudioFormat *fmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:AMY_SYNTH_SAMPLE_RATE channels:2];
		if (@available(macOS 27.0, *))
			connected = [sEngine connect:sAmyNode to:sum fromBus:0 toBus:(AVAudioNodeBus)sAmyPort format:fmt error:&connectErr];
		else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			[sEngine connect:sAmyNode to:sum fromBus:0 toBus:(AVAudioNodeBus)sAmyPort format:fmt];
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
	if (sAmyPort >= 0)
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
	WatchEngineConfiguration(sEngine);
}

int
InternalSynthIsAmyDestination(MIDIEndpointRef dest)
{
	return dest != 0 && dest == sAmyDest;
}

int
InternalSynthIsDlsDestination(MIDIEndpointRef dest)
{
	int i;

	if (dest == 0)
		return 0;
	for (i = 0; i < 2; i++) {
		if (sInternalDest[i] != 0 && dest == sInternalDest[i])
			return 1;
	}
	return 0;
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
	sEngineWanted = 1;
	if (sEngine == nil)
		StartInternalSynthEngine();
	/* A stopped engine is left in place when the configuration notification
	   never arrives. Port changes call this and would otherwise no-op.
	   The observer is off for this start so it is not seen as a device change. */
	if (sEngine != nil && ![sEngine isRunning]) {
		NSError *err = nil;
		UnwatchEngineConfiguration();
		if ([sEngine startAndReturnError:&err] && [sEngine isRunning])
			WatchEngineConfiguration(sEngine);
		else {
			NSLog(@"internal synth restart failed: %@", err);
			StopInternalSynthEngine();
			StartInternalSynthEngine();
		}
	}
	pthread_mutex_unlock(&sSynthLock);
}

static void
EnsureInternalSynthStoppedMain(void)
{
	pthread_mutex_lock(&sSynthLock);
	sEngineWanted = 0;
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

/*  Keys live in ja.lproj and en.lproj Localizable.strings. The bundle language
    picks the table; a later pass can edit the strings without touching this. */
static void
ShowRecoveryAlert(int started, int stoppedTransport)
{
	NSString *text;

	if (started && stoppedTransport) {
		text = NSLocalizedString(
			@"The internal synthesizer was reset. Playback has also been stopped. Play the song from the beginning.",
			@"Internal synth reset and transport stopped");
	} else if (started) {
		text = NSLocalizedString(
			@"The internal synthesizer was reset.",
			@"Internal synth reset");
	} else if (stoppedTransport) {
		text = NSLocalizedString(
			@"The internal synthesizer could not be started. Playback has also been stopped.",
			@"Internal synth start failed and transport stopped");
	} else {
		text = NSLocalizedString(
			@"The internal synthesizer could not be started.",
			@"Internal synth start failed");
	}
	NSAlert *alert = [[NSAlert alloc] init];
	[alert setMessageText:text];
	[alert addButtonWithTitle:NSLocalizedString(@"OK", @"Alert button")];
	[alert setAlertStyle:NSAlertStyleInformational];
	[alert runModal];
	[alert release];
}

static void
RecoveryClear(void)
{
	sRecoveryBusy = 0;
	sRecoveryNeeded = 0;
	sRecoveryStoppedTransport = 0;
	sRecoveryQuietScheduled = 0;
}

static void RecoveryScheduleQuiet(void);

static void
RecoveryFinish(void)
{
	int started = sRecoveryStartOK;
	int stoppedTransport = sRecoveryStoppedTransport;

	sRecoveryStoppedTransport = 0;
	if (!sEngineWanted) {
		RecoveryClear();
		return;
	}
	/* Stay busy across the modal so a nested device notification only sets the flag. */
	ShowRecoveryAlert(started, stoppedTransport);
	sRecoveryBusy = 0;
	if (sRecoveryNeeded && sEngineWanted)
		InternalSynthNoteConfigurationChange(sEngineGeneration);
}

static void
RecoveryScheduleQuiet(void)
{
	if (sRecoveryQuietScheduled)
		return;
	sRecoveryQuietScheduled = 1;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC),
		dispatch_get_main_queue(), ^{
			sRecoveryQuietScheduled = 0;
			if (!sEngineWanted) {
				RecoveryClear();
				return;
			}
			if (sRecoveryNeeded) {
				RecoveryRun();
				return;
			}
			RecoveryFinish();
		});
}

static void
RecoveryRetryFired(void)
{
	if (!sEngineWanted) {
		RecoveryClear();
		return;
	}
	if (sRecoveryNeeded) {
		RecoveryRun();
		return;
	}
	pthread_mutex_lock(&sSynthLock);
	if (sEngine == nil && sEngineWanted)
		StartInternalSynthEngine();
	sRecoveryStartOK = (sEngine != nil);
	pthread_mutex_unlock(&sSynthLock);
	if (!sEngineWanted) {
		RecoveryClear();
		return;
	}
	if (sRecoveryNeeded) {
		RecoveryRun();
		return;
	}
	RecoveryScheduleQuiet();
}

/*  Main queue. Does not hold sSynthLock across the transport wait: the player
    thread takes that lock to deliver its last note-offs. */
static void
RecoveryRun(void)
{
	if (!sEngineWanted) {
		RecoveryClear();
		return;
	}
	while (sRecoveryNeeded && sEngineWanted) {
		sRecoveryNeeded = 0;
		if (rcp_transport_is_active()) {
			stop_coremidi_rcpplayer();
			sRecoveryStoppedTransport = 1;
		}
		if (!sEngineWanted)
			break;
		pthread_mutex_lock(&sSynthLock);
		if (sEngine != nil)
			StopInternalSynthEngine();
		if (sEngineWanted)
			StartInternalSynthEngine();
		sRecoveryStartOK = (sEngine != nil);
		pthread_mutex_unlock(&sSynthLock);
	}
	if (!sEngineWanted) {
		RecoveryClear();
		return;
	}
	if (!sRecoveryStartOK && sRecoveryRetryLeft > 0) {
		sRecoveryRetryLeft = 0;
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC),
			dispatch_get_main_queue(), ^{
				RecoveryRetryFired();
			});
		return;
	}
	RecoveryScheduleQuiet();
}

static void
InternalSynthNoteConfigurationChange(int generation)
{
	/* The engine stops itself, then posts. If it is still running, look
	   once more: the stop can land without a second notification. */
	if (generation != sEngineGeneration || !sEngineWanted || sEngine == nil)
		return;
	if ([sEngine isRunning]) {
		dispatch_async(dispatch_get_main_queue(), ^{
			if (generation != sEngineGeneration || !sEngineWanted || sEngine == nil)
				return;
			if ([sEngine isRunning])
				return;
			InternalSynthNoteConfigurationChange(generation);
		});
		return;
	}
	sRecoveryNeeded = 1;
	if (sRecoveryBusy)
		return;
	sRecoveryBusy = 1;
	sRecoveryRetryLeft = 1;
	sRecoveryStoppedTransport = 0;
	RecoveryRun();
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
LoadDLSSoundBank(AudioUnit au, int port)
{
	CFURLRef url;
	OSStatus sts;
	if (au == NULL || port < 0 || port > 1 || sDLSFontFile[port][0] == 0)
		return;
	url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
		(const UInt8 *)sDLSFontFile[port], (CFIndex)strlen(sDLSFontFile[port]), false);
	if (url == NULL)
		return;
	sts = AudioUnitSetProperty(au, kMusicDeviceProperty_SoundBankURL,
		kAudioUnitScope_Global, 0, &url, sizeof url);
	CFRelease(url);
	if (sts != noErr)
		NSLog(@"DLS sound bank load failed (port %d): %d", port, (int)sts);
}

/*  Caller holds sSynthLock. Restart once if any path actually changed. */
static void
ApplyDLSFontPathsLocked(const char *path0, const char *path1)
{
	int changed = 0;

	if (path0 == NULL)
		path0 = "";
	if (path1 == NULL)
		path1 = "";
	if (strcmp(sDLSFontFile[0], path0) != 0) {
		strlcpy(sDLSFontFile[0], path0, sizeof sDLSFontFile[0]);
		changed = 1;
	}
	if (strcmp(sDLSFontFile[1], path1) != 0) {
		strlcpy(sDLSFontFile[1], path1, sizeof sDLSFontFile[1]);
		changed = 1;
	}
	if (changed && sEngine != nil) {
		StopInternalSynthEngine();
		StartInternalSynthEngine();
	}
}

static void
ApplyDLSFontPathForPort(int port, const char *path)
{
	if (port < 0 || port > 1)
		return;
	if (path == NULL)
		path = "";
	pthread_mutex_lock(&sSynthLock);
	if (port == 0)
		ApplyDLSFontPathsLocked(path, sDLSFontFile[1]);
	else
		ApplyDLSFontPathsLocked(sDLSFontFile[0], path);
	pthread_mutex_unlock(&sSynthLock);
}

static void
ApplyDLSFontPathBoth(const char *path)
{
	if (path == NULL)
		path = "";
	pthread_mutex_lock(&sSynthLock);
	ApplyDLSFontPathsLocked(path, path);
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
		ApplyDLSFontPathBoth(path);
		free(path);
	} else {
		dispatch_sync(dispatch_get_main_queue(), ^{
			ApplyDLSFontPathBoth(path);
			free(path);
		});
	}
}

void
CoreMIDISetDLSSynthFontForPort(int port, const char *filename)
{
	char *path = malloc(PATH_MAX);
	if (path == NULL)
		return;
	ResolveDLSFontPath(filename, path, PATH_MAX);
	if ([NSThread isMainThread]) {
		ApplyDLSFontPathForPort(port, path);
		free(path);
	} else {
		dispatch_sync(dispatch_get_main_queue(), ^{
			ApplyDLSFontPathForPort(port, path);
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
	if (s.dlsVolume2 < 0.0f)
		s.dlsVolume2 = 0.0f;
	if (s.dlsVolume2 > 1.0f)
		s.dlsVolume2 = 1.0f;
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
CoreMIDIDLSSynthFontFilenameForPort(int port)
{
	static char name[2][256];
	const char *base;

	if (port < 0 || port > 1)
		port = 0;
	pthread_mutex_lock(&sSynthLock);
	base = strrchr(sDLSFontFile[port], '/');
	if (base != NULL)
		base++;
	else
		base = sDLSFontFile[port];
	strlcpy(name[port], base, sizeof name[port]);
	pthread_mutex_unlock(&sSynthLock);
	return name[port];
}

const char *
CoreMIDIDLSSynthFontFilename(void)
{
	return CoreMIDIDLSSynthFontFilenameForPort(0);
}

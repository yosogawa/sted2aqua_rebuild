#import "InternalSynth.h"
#import "AmySynth.h"

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/MusicDevice.h>
#include <string.h>
#include <strings.h>
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
#include "rcp_player_engine.h"
#include "midi_in.h"
#include "CoreMIDIRCPPlayer.h"

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
/* Empty: follow the system default output. Otherwise a Core Audio device UID. */
static char sOutputUID[DLS_OUTPUT_UID_MAX];
/* One start retry must not pin a device that just failed to start. */
static int sOutputPinSuspended;
/* Set around an in-app output change so that start does not raise the
   device-removed alert, including a change back to the system default. */
static int sOutputSwitch;
/* Private aggregate wrapping the chosen output. AVAudioEngine only leaves the
   system default aggregate when its device is itself an aggregate. */
static AudioObjectID sPrivateAggregate = kAudioObjectUnknown;
static char sPrivateAggregateSource[DLS_OUTPUT_UID_MAX];
/* Host time until which a configuration notification is our own device switch. */
static uint64_t sConfigSuppressUntil;
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
	uint32_t frames;
} SynthAnchor;

static SynthAnchor sAnchor[2];

static void
StoreAnchor(int idx, int64_t sampleTime, uint64_t hostTime, uint32_t frames)
{
	SynthAnchor *a = &sAnchor[idx];
	uint32_t s = atomic_load_explicit(&a->seq, memory_order_relaxed);
	atomic_store_explicit(&a->seq, s + 1, memory_order_relaxed);
	a->sampleTime = sampleTime;
	a->hostTime = hostTime;
	a->frames = frames;
	atomic_store_explicit(&a->seq, s + 2, memory_order_release);
}

static int
LoadAnchor(int idx, int64_t *sampleTime, uint64_t *hostTime, uint32_t *frames)
{
	SynthAnchor *a = &sAnchor[idx];
	uint32_t s1, s2;
	int64_t sample;
	uint64_t host;
	uint32_t nframes;
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
		nframes = a->frames;
		s2 = atomic_load_explicit(&a->seq, memory_order_acquire);
	} while (s1 != s2);
	if (s1 == 0 || host == 0)
		return 0;
	*sampleTime = sample;
	*hostTime = host;
	if (frames != NULL)
		*frames = nframes;
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
	StoreAnchor(idx, (int64_t)inTimeStamp->mSampleTime, (uint64_t)inTimeStamp->mHostTime,
		inNumberFrames);
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

/* DLS drops note-offs that land inside the buffer being rendered.
   Stamps earlier than the end of that buffer are placed 20 ms after the end.
   Internal to this synth. */
static const double kDlsReservePastBufferEndSeconds = 0.020;

/*  INT prefetches RCP_PREFETCH_MICROSECONDS. Accept twice that
    (RCP_TIMESTAMP_LEAD_MICROSECONDS). Farther timestamps are not from that clock. */
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
	double leadSeconds;
	uint32_t frames;

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
	sampleTime = 0;
	deltaSeconds = 0.0;
	frames = 0;
	leadSeconds = (double)RCP_TIMESTAMP_LEAD_MICROSECONDS / 1000000.0;
	if (atTime != 0 && sSampleRate[synth] > 0 && sHostTicksPerSecond > 0
		&& LoadAnchor(synth, &sampleTime, &hostTime, &frames)) {
		deltaTicks = (int64_t)(atTime - hostTime);
		deltaSeconds = (double)deltaTicks / sHostTicksPerSecond;
		if (frames > 0) {
			double floorSec = ((double)frames / sSampleRate[synth])
				+ kDlsReservePastBufferEndSeconds;
			if (deltaSeconds < floorSec)
				deltaSeconds = floorSec;
		}
		if (deltaSeconds >= 0.0 && deltaSeconds <= leadSeconds)
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
	    OutputPortSend writes the chosen synth directly so host time is not
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
		sAnchor[i].frames = 0;
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

static void RepinDriftedOutputDevice(void);

/*  Switching the system default between the built-in speaker and headphones
    moves a running engine onto that device and posts no configuration
    notification. The engine is still on the pinned device when the listener
    runs, and follows about a fifth of a second later. Look again then.
    The listener only hops to the main queue. */
static int sWatchingDefaultOutput;

static OSStatus
DefaultOutputDeviceChanged(AudioObjectID objectID, UInt32 addressCount,
	const AudioObjectPropertyAddress *addresses, void *client)
{
	(void)objectID;
	(void)addressCount;
	(void)addresses;
	(void)client;
	dispatch_async(dispatch_get_main_queue(), ^{
		RepinDriftedOutputDevice();
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC),
			dispatch_get_main_queue(), ^{
				RepinDriftedOutputDevice();
			});
	});
	return noErr;
}

static void
WatchDefaultOutputDevice(void)
{
	AudioObjectPropertyAddress addr;

	if (sWatchingDefaultOutput)
		return;
	addr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMain;
	if (AudioObjectAddPropertyListener(kAudioObjectSystemObject, &addr,
			DefaultOutputDeviceChanged, NULL) == noErr)
		sWatchingDefaultOutput = 1;
}

static void
UnwatchDefaultOutputDevice(void)
{
	AudioObjectPropertyAddress addr;

	if (!sWatchingDefaultOutput)
		return;
	addr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMain;
	AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &addr,
		DefaultOutputDeviceChanged, NULL);
	sWatchingDefaultOutput = 0;
}

/*  Drops in-flight configuration notifications for the engine being released.
    The block only hops to the main queue; it must not run on the audio queue. */
static void
UnwatchEngineConfiguration(void)
{
	sEngineGeneration++;
	UnwatchDefaultOutputDevice();
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
	WatchDefaultOutputDevice();
}

/*  Caller holds sSynthLock. Render callbacks do not take that lock. */
static void DestroyPrivateAggregate(void);

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
	DestroyPrivateAggregate();
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

/* macOS builds CADefaultDeviceAggregate-* for the default output. Audio MIDI Setup
   hides it (transport type AutoAggregate, 'fgrp'). User multi-output devices stay. */
static int
OutputDeviceIsPrivate(AudioDeviceID dev)
{
	AudioObjectPropertyAddress prop;
	UInt32 value = 0;
	UInt32 size = sizeof value;

	prop.mScope = kAudioObjectPropertyScopeGlobal;
	prop.mElement = kAudioObjectPropertyElementMain;
	prop.mSelector = kAudioDevicePropertyIsHidden;
	if (AudioObjectGetPropertyData(dev, &prop, 0, NULL, &size, &value) == noErr && value != 0)
		return 1;
	value = 0;
	size = sizeof value;
	prop.mSelector = kAudioDevicePropertyTransportType;
	if (AudioObjectGetPropertyData(dev, &prop, 0, NULL, &size, &value) == noErr && value == 'fgrp')
		return 1;
	return 0;
}

static int
OutputChannelCount(AudioDeviceID dev)
{
	AudioObjectPropertyAddress addr;
	UInt32 size = 0;
	AudioBufferList *list;
	UInt32 i;
	int channels = 0;

	addr.mSelector = kAudioDevicePropertyStreamConfiguration;
	addr.mScope = kAudioDevicePropertyScopeOutput;
	addr.mElement = kAudioObjectPropertyElementMain;
	if (AudioObjectGetPropertyDataSize(dev, &addr, 0, NULL, &size) != noErr || size == 0)
		return 0;
	list = (AudioBufferList *)malloc(size);
	if (list == NULL)
		return 0;
	if (AudioObjectGetPropertyData(dev, &addr, 0, NULL, &size, list) != noErr) {
		free(list);
		return 0;
	}
	for (i = 0; i < list->mNumberBuffers; i++)
		channels += (int)list->mBuffers[i].mNumberChannels;
	free(list);
	return channels;
}

static void
CopyCFString(char *dst, size_t dstLen, CFStringRef src)
{
	if (dst == NULL || dstLen == 0)
		return;
	dst[0] = 0;
	if (src == NULL)
		return;
	CFStringGetCString(src, dst, (CFIndex)dstLen, kCFStringEncodingUTF8);
}

static AudioDeviceID
OutputDeviceForUID(const char *uid)
{
	AudioObjectPropertyAddress addr;
	CFStringRef cf;
	AudioDeviceID dev = kAudioObjectUnknown;
	AudioValueTranslation trans;
	UInt32 size;

	if (uid == NULL || uid[0] == 0)
		return kAudioObjectUnknown;
	cf = CFStringCreateWithCString(NULL, uid, kCFStringEncodingUTF8);
	if (cf == NULL)
		return kAudioObjectUnknown;
	trans.mInputData = (void *)&cf;
	trans.mInputDataSize = (UInt32)sizeof(cf);
	trans.mOutputData = &dev;
	trans.mOutputDataSize = (UInt32)sizeof(dev);
	size = (UInt32)sizeof(trans);
	addr.mSelector = kAudioHardwarePropertyDeviceForUID;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMain;
	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, &trans) != noErr)
		dev = kAudioObjectUnknown;
	CFRelease(cf);
	if (dev == kAudioObjectUnknown || OutputChannelCount(dev) <= 0)
		return kAudioObjectUnknown;
	return dev;
}

static int sConfigRestarting;

static void
SuppressConfigRecovery(void)
{
	double freq = AudioGetHostClockFrequency();

	if (freq <= 0.0)
		freq = 1.0e9;
	sConfigSuppressUntil = AudioGetCurrentHostTime() + (uint64_t)(freq * 1.0);
}

static int
ConfigRecoverySuppressed(void)
{
	return sConfigSuppressUntil != 0 && AudioGetCurrentHostTime() < sConfigSuppressUntil;
}

static void
DestroyPrivateAggregate(void)
{
	AudioDeviceID stale;

	if (sPrivateAggregate != kAudioObjectUnknown) {
		AudioHardwareDestroyAggregateDevice(sPrivateAggregate);
		sPrivateAggregate = kAudioObjectUnknown;
	}
	sPrivateAggregateSource[0] = 0;
	stale = OutputDeviceForUID("com.github.STed2.private-output");
	if (stale != kAudioObjectUnknown)
		AudioHardwareDestroyAggregateDevice(stale);
}

/*  An output-only device is wrapped in a private aggregate. AVAudioEngine
    stays on the system default aggregate unless its device is an aggregate. */
static AudioDeviceID
EngineOutputDeviceForUID(const char *uid)
{
	AudioDeviceID raw;
	UInt32 transport = 0;
	UInt32 size;
	AudioObjectPropertyAddress prop;
	NSString *uidStr;
	NSDictionary *desc;
	AudioObjectID agg = kAudioObjectUnknown;
	OSStatus st;

	if (uid == NULL || uid[0] == 0) {
		DestroyPrivateAggregate();
		return kAudioObjectUnknown;
	}
	if (sPrivateAggregate != kAudioObjectUnknown && strcmp(sPrivateAggregateSource, uid) == 0)
		return (AudioDeviceID)sPrivateAggregate;
	raw = OutputDeviceForUID(uid);
	if (raw == kAudioObjectUnknown) {
		NSLog(@"sound output \"%s\" is not available; using the default output", uid);
		return kAudioObjectUnknown;
	}
	prop.mSelector = kAudioDevicePropertyTransportType;
	prop.mScope = kAudioObjectPropertyScopeGlobal;
	prop.mElement = kAudioObjectPropertyElementMain;
	size = sizeof transport;
	if (AudioObjectGetPropertyData(raw, &prop, 0, NULL, &size, &transport) == noErr
	 && transport == kAudioDeviceTransportTypeAggregate) {
		DestroyPrivateAggregate();
		return raw;
	}
	DestroyPrivateAggregate();
	uidStr = [NSString stringWithUTF8String:uid];
	if (uidStr == nil)
		return raw;
	desc = @{
		@"name": @"STed2 Output",
		@"uid": @"com.github.STed2.private-output",
		@"subdevices": @[ @{ @"uid": uidStr } ],
		@"master": uidStr,
		@"private": @1
	};
	st = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)desc, &agg);
	if (st != noErr || agg == kAudioObjectUnknown) {
		NSLog(@"private output aggregate failed (%d) for %s", (int)st, uid);
		return raw;
	}
	sPrivateAggregate = agg;
	strlcpy(sPrivateAggregateSource, uid, sizeof sPrivateAggregateSource);
	return (AudioDeviceID)agg;
}

/*  Caller holds sSynthLock. The engine's output node starts on the system
    aggregate (default input + default output). setDeviceID: before start
    selects another aggregate, or a private one built around the chosen device. */
static int
ApplyOutputDeviceLocked(AVAudioEngine *engine)
{
	AudioDeviceID dev;
	AUAudioUnit *au;
	NSError *error = nil;

	if (engine == nil || sOutputPinSuspended)
		return 0;
	if (sOutputUID[0] == 0) {
		DestroyPrivateAggregate();
		return 0;
	}
	dev = EngineOutputDeviceForUID(sOutputUID);
	if (dev == kAudioObjectUnknown)
		return 0;
	[engine prepare];
	au = engine.outputNode.AUAudioUnit;
	if (au == nil)
		return 0;
	if (![au setDeviceID:(AUAudioObjectID)dev error:&error]) {
		NSLog(@"sound output setDeviceID failed: %@ (%s)", error, sOutputUID);
		return 0;
	}
	return 1;
}

/*  Caller holds sSynthLock. start can put the node back on the default aggregate. */
static void
ConfirmOutputDeviceLocked(AVAudioEngine *engine)
{
	AudioDeviceID dev;
	AUAudioUnit *au;
	NSError *error = nil;
	NSError *startErr = nil;

	if (engine == nil || sOutputUID[0] == 0 || sOutputPinSuspended)
		return;
	dev = EngineOutputDeviceForUID(sOutputUID);
	if (dev == kAudioObjectUnknown)
		return;
	au = engine.outputNode.AUAudioUnit;
	if (au == nil)
		return;
	if (au.deviceID == (AUAudioObjectID)dev)
		return;
	NSLog(@"sound output reverted to device %u; selecting %u (%s)",
		(unsigned)au.deviceID, (unsigned)dev, sOutputUID);
	if ([engine isRunning])
		[engine stop];
	if (![au setDeviceID:(AUAudioObjectID)dev error:&error]) {
		NSLog(@"sound output setDeviceID failed: %@", error);
		[engine startAndReturnError:&startErr];
		return;
	}
	if (![engine startAndReturnError:&startErr])
		NSLog(@"sound output restart failed: %@", startErr);
	else if (au.deviceID != (AUAudioObjectID)dev)
		NSLog(@"sound output is device %u, wanted %u", (unsigned)au.deviceID, (unsigned)dev);
}

static void
NormalizeOutputUID(const char *src, char *dst, size_t dstLen)
{
	size_t n;

	if (dst == NULL || dstLen == 0)
		return;
	dst[0] = 0;
	if (src == NULL)
		return;
	while (*src == ' ' || *src == '\t')
		src++;
	n = strlen(src);
	while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\t' || src[n - 1] == '\n' || src[n - 1] == '\r'))
		n--;
	if (n == 0 || (n == 7 && strncasecmp(src, "default", 7) == 0))
		return;
	if (n >= dstLen)
		n = dstLen - 1;
	memcpy(dst, src, n);
	dst[n] = 0;
}

static int
OutputDeviceNameOrder(const void *a, const void *b)
{
	const DLSOutputDeviceInfo *da = (const DLSOutputDeviceInfo *)a;
	const DLSOutputDeviceInfo *db = (const DLSOutputDeviceInfo *)b;
	return strcasecmp(da->name, db->name);
}

int
CoreMIDIListOutputDevices(DLSOutputDeviceInfo *devices, int maxCount)
{
	AudioObjectPropertyAddress addr;
	UInt32 size = 0;
	AudioDeviceID *ids;
	UInt32 i, n;
	int count = 0;

	if (devices == NULL || maxCount <= 0)
		return 0;
	addr.mSelector = kAudioHardwarePropertyDevices;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMain;
	if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, NULL, &size) != noErr || size == 0)
		return 0;
	ids = (AudioDeviceID *)malloc(size);
	if (ids == NULL)
		return 0;
	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, ids) != noErr) {
		free(ids);
		return 0;
	}
	n = size / (UInt32)sizeof(AudioDeviceID);
	for (i = 0; i < n && count < maxCount; i++) {
		CFStringRef name = NULL;
		CFStringRef uid = NULL;
		UInt32 sz;
		AudioObjectPropertyAddress prop;

		if (OutputChannelCount(ids[i]) <= 0 || OutputDeviceIsPrivate(ids[i]))
			continue;
		prop.mScope = kAudioObjectPropertyScopeGlobal;
		prop.mElement = kAudioObjectPropertyElementMain;
		prop.mSelector = kAudioDevicePropertyDeviceUID;
		sz = (UInt32)sizeof(uid);
		if (AudioObjectGetPropertyData(ids[i], &prop, 0, NULL, &sz, &uid) != noErr || uid == NULL)
			continue;
		prop.mSelector = kAudioObjectPropertyName;
		sz = (UInt32)sizeof(name);
		if (AudioObjectGetPropertyData(ids[i], &prop, 0, NULL, &sz, &name) != noErr || name == NULL) {
			CFRelease(uid);
			continue;
		}
		CopyCFString(devices[count].uid, sizeof devices[count].uid, uid);
		CopyCFString(devices[count].name, sizeof devices[count].name, name);
		CFRelease(uid);
		CFRelease(name);
		if (devices[count].uid[0] == 0)
			continue;
		if (strncmp(devices[count].uid, "CADefaultDeviceAggregate", 24) == 0
		 || strncmp(devices[count].name, "CADefaultDeviceAggregate", 24) == 0
		 || strcmp(devices[count].uid, "com.github.STed2.private-output") == 0)
			continue;
		if (devices[count].name[0] == 0)
			strlcpy(devices[count].name, devices[count].uid, sizeof devices[count].name);
		count++;
	}
	free(ids);
	if (count > 1)
		qsort(devices, (size_t)count, sizeof devices[0], OutputDeviceNameOrder);
	return count;
}

void
CoreMIDIGetDLSOutputDevice(char *uid, size_t uidLen)
{
	if (uid == NULL || uidLen == 0)
		return;
	pthread_mutex_lock(&sSynthLock);
	strlcpy(uid, sOutputUID, uidLen);
	pthread_mutex_unlock(&sSynthLock);
}

static void StartInternalSynthEngine(void);

static void
ApplyOutputDeviceNow(const char *uid)
{
	char next[DLS_OUTPUT_UID_MAX];
	int rebuild;

	NormalizeOutputUID(uid, next, sizeof next);
	pthread_mutex_lock(&sSynthLock);
	if (strcmp(sOutputUID, next) == 0) {
		pthread_mutex_unlock(&sSynthLock);
		return;
	}
	rebuild = (sEngine != nil);
	pthread_mutex_unlock(&sSynthLock);
	/* Note-offs need the running engine, and the player takes sSynthLock.
	   Rewind before the rebuild. Channel state is gone, so the song
	   returns to the top. No alert: the locator shows the new position. */
	if (rebuild) {
		rcp_transport_return_to_start();
		/* External gear is not inside the rebuilt engine. Its #init_exc=
		   is the reset the user stored. A song-header reset may follow. */
		CoreMIDISendInitExcToExternalPorts();
	}
	pthread_mutex_lock(&sSynthLock);
	if (strcmp(sOutputUID, next) != 0) {
		memcpy(sOutputUID, next, sizeof sOutputUID);
		if (sEngine != nil) {
			sOutputSwitch = 1;
			StopInternalSynthEngine();
			StartInternalSynthEngine();
			sOutputSwitch = 0;
		}
	}
	pthread_mutex_unlock(&sSynthLock);
}

void
CoreMIDISetDLSOutputDevice(const char *uid)
{
	if ([NSThread isMainThread])
		ApplyOutputDeviceNow(uid);
	else
		dispatch_sync(dispatch_get_main_queue(), ^{ ApplyOutputDeviceNow(uid); });
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
	{
		int pinned = ApplyOutputDeviceLocked(sEngine);
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
			if (pinned) {
				NSLog(@"internal synth retrying with the default sound output");
				sOutputPinSuspended = 1;
				StartInternalSynthEngine();
				sOutputPinSuspended = 0;
			}
			return;
		}
	}
	ConfirmOutputDeviceLocked(sEngine);
	if (sOutputUID[0] != 0 || sOutputSwitch)
		SuppressConfigRecovery();
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
	/* The apply before start skipped DLS gains: sDLSUnit was still nil.
	   start can also put the effect units back to their defaults. */
	sAppliedReverbPreset = -1;
	ApplyEffectsLocked();
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

static int
ApplyAmyPortLocked(int port)
{
	if (sAmyPort == port)
		return 0;
	sAmyPort = port;
	return sEngine != nil;
}

static int
ApplyAmyPortMain(const MIDIEndpointRef *dests)
{
	int port = -1;
	int i;
	int rebuild;
	for (i = 0; i < kMaxNumberOfPorts; i++) {
		if (dests[i] != 0 && dests[i] == sAmyDest)
			port = i;
	}
	pthread_mutex_lock(&sSynthLock);
	rebuild = ApplyAmyPortLocked(port);
	pthread_mutex_unlock(&sSynthLock);
	if (!rebuild)
		return 0;
	/* AMY moved, so the whole graph is rebuilt and channel state is gone.
	   Stop the player before taking the lock again. */
	rcp_transport_return_to_start();
	pthread_mutex_lock(&sSynthLock);
	if (sEngine != nil) {
		sOutputSwitch = 1;
		StopInternalSynthEngine();
		StartInternalSynthEngine();
		sOutputSwitch = 0;
	}
	pthread_mutex_unlock(&sSynthLock);
	return 1;
}

int
InternalSynthApplyAmyPort(const MIDIEndpointRef *dests)
{
	if ([NSThread isMainThread])
		return ApplyAmyPortMain(dests);
	__block int rebuilt = 0;
	dispatch_sync(dispatch_get_main_queue(), ^{
		rebuilt = ApplyAmyPortMain(dests);
	});
	return rebuilt;
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
		ApplyOutputDeviceLocked(sEngine);
		if ([sEngine startAndReturnError:&err] && [sEngine isRunning]) {
			ConfirmOutputDeviceLocked(sEngine);
			if ([sEngine isRunning])
				WatchEngineConfiguration(sEngine);
		}
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
		/* The rebuilt synth has lost channel state. Same as a combo change:
		   stop a live player before leaving Held, then put the locator at
		   the top. A stopped song still moves back to bar 1. */
		if (rcp_transport_is_active())
			sRecoveryStoppedTransport = 1;
		rcp_transport_return_to_start();
		if (!sEngineWanted)
			break;
		pthread_mutex_lock(&sSynthLock);
		if (sEngine != nil)
			StopInternalSynthEngine();
		if (sEngineWanted)
			StartInternalSynthEngine();
		sRecoveryStartOK = (sEngine != nil);
		pthread_mutex_unlock(&sSynthLock);
		CoreMIDISendInitExcToExternalPorts();
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

/*  A pinned device must not follow a later change of the system default.
    The engine can keep running and move onto that default. Put the same
    device back once. The suppress window keeps this stop and start from
    rebuilding the graph. A device that is gone is left for the recovery
    that runs when the engine has stopped. */
static void
RepinDriftedOutputDevice(void)
{
	AUAudioUnit *au;
	AudioDeviceID want;

	if (sConfigRestarting || ConfigRecoverySuppressed())
		return;
	if (sOutputUID[0] == 0 || sOutputPinSuspended || sEngine == nil)
		return;
	pthread_mutex_lock(&sSynthLock);
	if (sEngine == nil || ![sEngine isRunning] || sOutputUID[0] == 0 || sOutputPinSuspended) {
		pthread_mutex_unlock(&sSynthLock);
		return;
	}
	if (OutputDeviceForUID(sOutputUID) == kAudioObjectUnknown) {
		pthread_mutex_unlock(&sSynthLock);
		return;
	}
	want = EngineOutputDeviceForUID(sOutputUID);
	au = sEngine.outputNode.AUAudioUnit;
	if (want == kAudioObjectUnknown || au == nil || au.deviceID == (AUAudioObjectID)want) {
		pthread_mutex_unlock(&sSynthLock);
		return;
	}
	SuppressConfigRecovery();
	ConfirmOutputDeviceLocked(sEngine);
	pthread_mutex_unlock(&sSynthLock);
}

static void
InternalSynthNoteConfigurationChange(int generation)
{
	NSError *err = nil;

	/* The engine stops itself, then posts. If it is still running, look
	   once more: the stop can land without a second notification. */
	if (generation != sEngineGeneration || !sEngineWanted || sEngine == nil)
		return;
	/* A device we just selected posts this too. Restarting in place keeps
	   the graph and does not stop the sequencer. */
	if (ConfigRecoverySuppressed()) {
		if (sConfigRestarting || [sEngine isRunning])
			return;
		sConfigRestarting = 1;
		pthread_mutex_lock(&sSynthLock);
		if (sEngine != nil && sEngineWanted && ![sEngine isRunning]) {
			ApplyOutputDeviceLocked(sEngine);
			if (![sEngine startAndReturnError:&err] || ![sEngine isRunning])
				NSLog(@"sound output restart after device change failed: %@", err);
			else
				ConfirmOutputDeviceLocked(sEngine);
		}
		pthread_mutex_unlock(&sSynthLock);
		sConfigRestarting = 0;
		return;
	}
	if ([sEngine isRunning]) {
		dispatch_async(dispatch_get_main_queue(), ^{
			if (generation != sEngineGeneration || !sEngineWanted || sEngine == nil)
				return;
			if ([sEngine isRunning]) {
				RepinDriftedOutputDevice();
				return;
			}
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

/*  Caller holds sSynthLock. Stores the paths. Returns 1 when a live engine
    must be rebuilt outside the lock: the player takes sSynthLock for its
    last note-offs. */
static int
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
	return changed && sEngine != nil;
}

/*  The new bank drops channel state, same as an output change. Rewind first,
    then reset an external port that shares the song. No alert. */
static void
RebuildEngineAfterFontChange(void)
{
	rcp_transport_return_to_start();
	CoreMIDISendInitExcToExternalPorts();
	pthread_mutex_lock(&sSynthLock);
	if (sEngine != nil) {
		sOutputSwitch = 1;
		StopInternalSynthEngine();
		StartInternalSynthEngine();
		sOutputSwitch = 0;
	}
	pthread_mutex_unlock(&sSynthLock);
}

static void
ApplyDLSFontPathForPort(int port, const char *path)
{
	int rebuild;

	if (port < 0 || port > 1)
		return;
	if (path == NULL)
		path = "";
	pthread_mutex_lock(&sSynthLock);
	if (port == 0)
		rebuild = ApplyDLSFontPathsLocked(path, sDLSFontFile[1]);
	else
		rebuild = ApplyDLSFontPathsLocked(sDLSFontFile[0], path);
	pthread_mutex_unlock(&sSynthLock);
	if (rebuild)
		RebuildEngineAfterFontChange();
}

static void
ApplyDLSFontPathBoth(const char *path)
{
	int rebuild;

	if (path == NULL)
		path = "";
	pthread_mutex_lock(&sSynthLock);
	rebuild = ApplyDLSFontPathsLocked(path, path);
	pthread_mutex_unlock(&sSynthLock);
	if (rebuild)
		RebuildEngineAfterFontChange();
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

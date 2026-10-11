//
//  CoreMIDIRCPPlayer.m
//  STed2_aqua
//
//  Created by Toshi Nagata on Sun Jun 08 2003.
//  Copyright (c) 2003 Toshi Nagata. All rights reserved.
//

#import "rcp_player.h"
#import "CoreMIDIRCPPlayer.h"

#include <CoreMIDI/CoreMIDI.h>				/*  for MIDI input/output  */
#include <CoreAudio/CoreAudio.h>			/*  for timing functions  */
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/MusicDevice.h>
#include <unistd.h>
#include <stdatomic.h>
#include <math.h>
#include <pthread.h>
#include <sys/stat.h>
#include <limits.h>
#include <CoreServices/CoreServices.h>

#import <AudioToolbox/AUAudioUnit.h>
#import <AVFAudio/AVAudioEngine.h>
#import <AVFAudio/AVAudioFormat.h>
#import <AVFAudio/AVAudioMixerNode.h>
#import <AVFAudio/AVAudioUnit.h>

#include "sted.h"

extern int STed_InitMeasureConversion(void);

#define USE_INTERNALSYNTH 1

#define kMaxNumberOfPorts 2
#define kMaxPrefetchMicroseconds 1000000

#define kMaxMIDIMessageSize  256  /* Should match RCP_MAX_RESULT_SMF_SIZE in rcp.h */

#define GetHostTimeInMDTimeType()	((MDTimeType)(AudioConvertHostTimeToNanos(AudioGetCurrentHostTime()) / 1000))
#define ConvertMDTimeTypeToHostTime(tm)	AudioConvertNanosToHostTime((tm) * 1000)

void rcpplayer_engine(void);

#define MIDI_IN_RING_BUFFER_SIZE 4096
static uint8_t sMIDIInRingBuffer[MIDI_IN_RING_BUFFER_SIZE];
static volatile int sMIDIInHead = 0;
static volatile int sMIDIInTail = 0;

void push_midi_in_ring_buffer(uint8_t byte) {
    int next = (sMIDIInHead + 1) % MIDI_IN_RING_BUFFER_SIZE;
    if (next != sMIDIInTail) {
        sMIDIInRingBuffer[sMIDIInHead] = byte;
        sMIDIInHead = next;
    }
}

int pop_midi_in_ring_buffer(uint8_t *byte) {
    if (sMIDIInHead == sMIDIInTail) {
        return 0;
    }
    *byte = sMIDIInRingBuffer[sMIDIInTail];
    sMIDIInTail = (sMIDIInTail + 1) % MIDI_IN_RING_BUFFER_SIZE;
    return 1;
}

int has_midi_in_ring_buffer(void) {
    return (sMIDIInHead != sMIDIInTail) ? 1 : 0;
}

static MIDIPortRef sMIDIInputPortRef = 0;
static MIDIEndpointRef sMIDIInputSourceRef = 0;

static void CoreMIDIReadProc(const MIDIPacketList *pktlist, void *readProcRefCon, void *srcConnRefCon) {
    const MIDIPacket *packet = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; i++) {
        for (UInt16 j = 0; j < packet->length; j++) {
            push_midi_in_ring_buffer(packet->data[j]);
        }
        packet = MIDIPacketNext(packet);
    }
}

static CoreMIDIRCPPlayer *sCurrentPlayer;
static MIDIClientRef	sMIDIClientRef = NULL;
static MIDIPortRef		sMIDIOutputPortRef = NULL;
static MIDIEndpointRef	sDest[kMaxNumberOfPorts];

static MIDISysexSendRequest sSysexRequest[kMaxNumberOfPorts];
static uint8_t sSysexSendBuffer[kMaxNumberOfPorts][kMaxMIDIMessageSize];

static uint8_t sStopPlaying;
static volatile uint8_t sRecording;

void set_realtime_recording(int on)
{
	sRecording = on ? 1 : 0;
}

static MIDITimeStamp sStartTimeStamp;
static MIDITimeStamp sLastTimeStamp;
static char sNoteOnTable[kMaxNumberOfPorts][16][128];

//  Next earliest timestamp for each port (to avoid too thick MIDI byte sequence)
static MIDITimeStamp sNextEarliestTimeForPort[kMaxNumberOfPorts];

//  Microsecond wait per MIDI byte (it depends on the hardware feature, so it should be 
//  assigned per port)
static int sWaitPerMIDIByte[kMaxNumberOfPorts];

//  Microsecond offset to avoid 'simultaneous event' in the different port
static int sOffsetMIDITime = 1;

#define DEBUG_RCP 0

#if DEBUG_RCP
#define sRCPDebugSize 200000
#include <stddef.h>
static size_t sRCPDebugCount;
static uint8_t *sRCPDebugBuffer;
#endif

@implementation CoreMIDIRCPPlayer

+ (CoreMIDIRCPPlayer *)currentPlayer
{
	if (sCurrentPlayer == nil)
		sCurrentPlayer = [[CoreMIDIRCPPlayer alloc] init];
	return sCurrentPlayer;
}

- (NSString *)deviceNameAtIndex: (int)index
{
	return nil;
}

- (void)setDevice: (NSString *)device forPort: (int)port
{
}

- (void)startPlaying:(id)argument
{
	playing = YES;
	sStopPlaying = 0;
	rcpplayer_engine();
	playing = NO;
	sStopPlaying = 0;
	if (rcd != NULL) {
		rcd->act = 0;
	}
}

- (BOOL)isPlaying
{
	return playing;
}

@end

#ifdef USE_INTERNALSYNTH
static AVAudioEngine *sEngine = nil;
static AVAudioUnit *sDLSUnit[2];
static AudioUnit sDLS[2];
static MIDIEndpointRef sInternalDest[2];
static AUScheduleMIDIEventBlock sScheduleMIDI[2];
static double sSampleRate[2];
static double sHostTicksPerSecond;
static pthread_mutex_t sSynthLock = PTHREAD_MUTEX_INITIALIZER;
static char sDLSFontFile[PATH_MAX];

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

/*  Playback prefetches at most one second. Timestamps farther out are not from that clock. */
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

static int
AddInternalDestination(int port, CFStringRef name)
{
	OSStatus sts;
	/*  The name stays visible as a destination. Playback does not arrive here:
	    CoreMIDISend writes the DLS unit directly so host time is not mistaken
	    for a sample offset. */
	sts = MIDIDestinationCreateWithProtocol(sMIDIClientRef, name, kMIDIProtocol_1_0,
		&sInternalDest[port], ^(const MIDIEventList *evtlist, void *srcConnRefCon) {
			(void)evtlist;
			(void)srcConnRefCon;
		});
	if (sts != noErr) {
		NSLog(@"MIDI destination %@ failed: %d", name, (int)sts);
		sInternalDest[port] = 0;
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

/*  Caller holds sSynthLock. Render callbacks do not take that lock. */
static void
StopInternalSynthEngine(void)
{
	AVAudioEngine *engine;
	AVAudioUnit *u0, *u1;

	if (sEngine == nil)
		return;
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
	[engine release];
	[u0 release];
	[u1 release];
}

static void LoadDLSSoundBank(AudioUnit au);

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
	sEngine = [[AVAudioEngine alloc] init];
	[sEngine attachNode:u0];
	[sEngine attachNode:u1];
	AudioUnitSetProperty(u0.audioUnit, kAudioUnitProperty_HostMIDIProtocol,
		kAudioUnitScope_Global, 0, &proto, sizeof proto);
	AudioUnitSetProperty(u1.audioUnit, kAudioUnitProperty_HostMIDIProtocol,
		kAudioUnitScope_Global, 0, &proto, sizeof proto);
	{
		AVAudioNodeBus bus0 = sEngine.mainMixerNode.nextAvailableInputBus;
		AVAudioNodeBus bus1;
		NSError *connectErr = nil;
		BOOL connected = NO;
		if (@available(macOS 27.0, *)) {
			connected = [sEngine connect:u0 to:sEngine.mainMixerNode fromBus:0 toBus:bus0 format:nil error:&connectErr];
			bus1 = sEngine.mainMixerNode.nextAvailableInputBus;
			if (connected)
				connected = [sEngine connect:u1 to:sEngine.mainMixerNode fromBus:0 toBus:bus1 format:nil error:&connectErr];
		} else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			[sEngine connect:u0 to:sEngine.mainMixerNode fromBus:0 toBus:bus0 format:nil];
			bus1 = sEngine.mainMixerNode.nextAvailableInputBus;
			[sEngine connect:u1 to:sEngine.mainMixerNode fromBus:0 toBus:bus1 format:nil];
#pragma clang diagnostic pop
			connected = YES;
		}
		if (!connected) {
			NSLog(@"internal synth connect failed: %@", connectErr);
			[sEngine release];
			sEngine = nil;
			[u0 release];
			[u1 release];
			return;
		}
	}
	CacheScheduleBlock(0, u0);
	CacheScheduleBlock(1, u1);
	AudioUnitAddRenderNotify(u0.audioUnit, InternalSynthRenderNotify, (void *)(intptr_t)0);
	AudioUnitAddRenderNotify(u1.audioUnit, InternalSynthRenderNotify, (void *)(intptr_t)1);
	sHostTicksPerSecond = AudioGetHostClockFrequency();
	if (![sEngine startAndReturnError:&err]) {
		NSLog(@"internal synth start failed: %@", err);
		AudioUnitRemoveRenderNotify(u0.audioUnit, InternalSynthRenderNotify, (void *)(intptr_t)0);
		AudioUnitRemoveRenderNotify(u1.audioUnit, InternalSynthRenderNotify, (void *)(intptr_t)1);
		ReleaseScheduleBlocks();
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
}

static int
AnyInternalDest(const MIDIEndpointRef *dests)
{
	int i, j;
	for (i = 0; i < kMaxNumberOfPorts; i++) {
		if (dests[i] == 0)
			continue;
		for (j = 0; j < 2; j++) {
			if (sInternalDest[j] != 0 && dests[i] == sInternalDest[j])
				return 1;
		}
	}
	return 0;
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

static void
EnsureInternalSynthRunning(void)
{
	if ([NSThread isMainThread])
		EnsureInternalSynthRunningMain();
	else
		dispatch_sync(dispatch_get_main_queue(), ^{ EnsureInternalSynthRunningMain(); });
}

static void
EnsureInternalSynthStopped(void)
{
	if ([NSThread isMainThread])
		EnsureInternalSynthStoppedMain();
	else
		dispatch_sync(dispatch_get_main_queue(), ^{ EnsureInternalSynthStoppedMain(); });
}

static void
CreateInternalDestinations(void)
{
	if (sInternalDest[0] == 0)
		AddInternalDestination(0, CFSTR("Apple DLS Synth"));
	if (sInternalDest[1] == 0)
		AddInternalDestination(1, CFSTR("Apple DLS Synth 2"));
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
	FSRef ref;
	if (au == NULL || sDLSFontFile[0] == 0)
		return;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	if (FSPathMakeRef((const UInt8 *)sDLSFontFile, &ref, NULL) != noErr)
		return;
	AudioUnitSetProperty(au, kMusicDeviceProperty_SoundBankFSRef,
		kAudioUnitScope_Global, 0, &ref, sizeof ref);
#pragma clang diagnostic pop
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
#endif

void CoreMIDIConnectDefaultInputs(void);

void
CoreMIDIInitIfNeeded(void)
{
	int i;
	if (sMIDIClientRef == NULL) {
		MIDIClientCreate(CFSTR("STed2 Mac"), NULL, NULL, &sMIDIClientRef);
		if (sMIDIOutputPortRef == NULL)
			MIDIOutputPortCreate(sMIDIClientRef, CFSTR("STed2 output port"), &sMIDIOutputPortRef);
		if (sMIDIInputPortRef == NULL)
			MIDIInputPortCreate(sMIDIClientRef, CFSTR("STed2 input port"), CoreMIDIReadProc, NULL, &sMIDIInputPortRef);
#ifdef USE_INTERNALSYNTH
		CreateInternalDestinations();
#endif
		for (i = 0; i < sizeof(sDest) / sizeof(sDest[0]); i++)
			sDest[i] = NULL;

		CoreMIDIConnectDefaultInputs();
	}
	for (i = 0; i < kMaxNumberOfPorts; i++)
		sWaitPerMIDIByte[i] = 300;
	sOffsetMIDITime = 1;
}
void CoreMIDIConnectDefaultInputs(void) {
	if (sMIDIInputPortRef == 0) return;

	NSUserDefaults *def = [NSUserDefaults standardUserDefaults];
	NSString *savedInputName = [def stringForKey:@"midiin"];

	if (savedInputName != nil && [savedInputName length] > 0) {
		CoreMIDISelectInputDeviceByName([savedInputName UTF8String]);
	}
}


static NSString *
CoreMIDIEndpointDisplayName(MIDIEndpointRef eref)
{
	CFStringRef name, name2;
	NSString *string;
	SInt32 i;
	MIDIObjectType objType;
	MIDIObjectRef oref;
	MIDIEntityRef enref;
	MIDIDeviceRef dref;
	if (eref == 0)
		return nil;
	name = name2 = NULL;
	if (MIDIObjectGetIntegerProperty(eref, kMIDIPropertyConnectionUniqueID, &i) == noErr
	&& i != 0
	&& MIDIObjectFindByUniqueID(i, &oref, &objType) == noErr) {
		MIDIObjectGetStringProperty(oref, kMIDIPropertyName, &name);
	} else {
		MIDIObjectGetStringProperty(eref, kMIDIPropertyName, &name);
		if (MIDIEndpointGetEntity(eref, &enref) == noErr
			&& enref != NULL
			&& MIDIEntityGetDevice(enref, &dref) == noErr
			&& dref != NULL) {
			if (MIDIObjectGetStringProperty(dref, kMIDIPropertyName, &name2) != noErr)
				name2 = NULL;
		}
	}
	if (name == NULL)
		return nil;
	if (name2 != NULL && [(NSString *)name2 isEqualToString:(NSString *)name]) {
		/*  Some MIDI device has the same name for entity and port (like NSX-39)  */
		CFRelease(name2);
		name2 = NULL;
	}
	string = [NSString stringWithString:(NSString *)name];
	CFRelease(name);
	if (name2 != NULL) {
		string = [NSString stringWithFormat:@"%@-%@", (NSString *)name2, string];
		CFRelease(name2);
	}
	return string;
}

NSString *
CoreMIDIDeviceNameAtIndex(int index)
{
	MIDIEndpointRef eref;
	CoreMIDIInitIfNeeded();
	eref = MIDIGetDestination(index);
	if (eref == 0)
		return nil;
	return CoreMIDIEndpointDisplayName(eref);
}

void
CoreMIDISelectDeviceForPort(int index, int port)
{
	MIDIEndpointRef dest;
#ifdef USE_INTERNALSYNTH
	MIDIEndpointRef next[kMaxNumberOfPorts];
	int i;
#endif
	CoreMIDIInitIfNeeded();
	port = port % kMaxNumberOfPorts;
	dest = MIDIGetDestination(index);
#ifdef USE_INTERNALSYNTH
	for (i = 0; i < kMaxNumberOfPorts; i++)
		next[i] = sDest[i];
	next[port] = dest;
	if (AnyInternalDest(next))
		EnsureInternalSynthRunning();
#endif
	sDest[port] = dest;
#ifdef USE_INTERNALSYNTH
	if (!AnyInternalDest(sDest))
		EnsureInternalSynthStopped();
#endif
}

const char *
CoreMIDICanonicalOutputName(const char *name)
{
	if (name == NULL)
		return NULL;
	if (strcasecmp(name, "Internal Synth") == 0)
		return "Apple DLS Synth";
	if (strcasecmp(name, "Internal Synth 2") == 0)
		return "Apple DLS Synth 2";
	return name;
}

void
CoreMIDISelectDeviceByNameForPort(const char *name, int port)
{
	int n;
	NSString *str;
	CoreMIDIInitIfNeeded();
	name = CoreMIDICanonicalOutputName(name);
	if (name == NULL)
		return;
	n = 0;
	while ((str = CoreMIDIDeviceNameAtIndex(n)) != nil) {
		if (strcasecmp(name, [str UTF8String]) == 0) {
			CoreMIDISelectDeviceForPort(n, port);
			return;
		}
		n++;
	}
}

NSString *
CoreMIDIInDeviceNameAtIndex(int index)
{
	MIDIEndpointRef eref;
	CoreMIDIInitIfNeeded();
	eref = MIDIGetSource(index);
	if (eref == 0)
		return nil;
	return CoreMIDIEndpointDisplayName(eref);
}

int
coremidi_connected_input_name(char *buf, int buflen)
{
	NSString *name;
	NSData *bytes;
	NSUInteger n;
	if (buf == NULL || buflen <= 0)
		return 0;
	buf[0] = 0;
	if (sMIDIInputSourceRef == 0)
		return 0;
	name = CoreMIDIEndpointDisplayName(sMIDIInputSourceRef);
	if (name == nil || [name length] == 0)
		return 0;
	bytes = [name dataUsingEncoding:NSShiftJISStringEncoding allowLossyConversion:YES];
	if (bytes == nil || [bytes length] == 0)
		return 0;
	n = [bytes length];
	if (n > (NSUInteger)buflen - 1)
		n = (NSUInteger)buflen - 1;
	memcpy(buf, [bytes bytes], n);
	buf[n] = 0;
	return 1;
}

void
CoreMIDISelectInputDeviceByName(const char *name)
{
	int n = 0;
	NSString *str;
	CoreMIDIInitIfNeeded();

	if (sMIDIInputSourceRef != 0) {
		MIDIPortDisconnectSource(sMIDIInputPortRef, sMIDIInputSourceRef);
		sMIDIInputSourceRef = 0;
	}

	if (name == NULL || name[0] == '\0')
		return;

	while ((str = CoreMIDIInDeviceNameAtIndex(n)) != nil) {
		if (strcasecmp(name, [str UTF8String]) == 0) {
			sMIDIInputSourceRef = MIDIGetSource(n);
			if (sMIDIInputSourceRef != 0) {
				MIDIPortConnectSource(sMIDIInputPortRef, sMIDIInputSourceRef, NULL);
			}
			return;
		}
		n++;
	}
}

OSStatus
CoreMIDISend(uint8_t *ptr, int length, int port, MIDITimeStamp atTime)
{
	MIDIPacketList	packetList;
	MIDIPacket *	packetPtr;
	OSStatus sts;
	port = port % kMaxNumberOfPorts;
	if (length <= 0 || sDest[port] == NULL)
		return 0;
#ifdef USE_INTERNALSYNTH
	if (sDest[port] == sInternalDest[0] || sDest[port] == sInternalDest[1]) {
		int synth = (sDest[port] == sInternalDest[1]) ? 1 : 0;
		uint8_t b;
		sts = ScheduleToInternalSynth(synth, ptr, length, atTime);
		b = ptr[0] & 0xf0;
		if (ptr[0] != 0xf0 && (b == 0x80 || b == 0x90)) {
			char *pp = &sNoteOnTable[port][ptr[0] & 0x0f][ptr[1]];
			if (b == 0x80 || ptr[2] == 0)
				(*pp)--;
			else
				(*pp)++;
		}
		return sts;
	}
#endif
	if (ptr[0] == 0xf0) {
		/*  Sysex  */
		MIDISysexSendRequest *rp = &sSysexRequest[port];
		while (rp->bytesToSend > 0)
			usleep(10000);  /*  Wait until the last sysex sent to the same port has completed */
		if (length > kMaxMIDIMessageSize)
			length = kMaxMIDIMessageSize;
		memmove(sSysexSendBuffer[port], ptr, length);
		rp->destination = sDest[port];
		rp->data = sSysexSendBuffer[port];
		rp->bytesToSend = length;
		rp->complete = 0;
		rp->completionProc = NULL;
		rp->completionRefCon = NULL;
		sts = MIDISendSysex(rp);
	} else {
		uint8_t b;
		packetPtr = MIDIPacketListInit(&packetList);
		packetPtr = MIDIPacketListAdd(&packetList, sizeof(packetList), packetPtr, atTime, length, ptr);
		sts = MIDISend(sMIDIOutputPortRef, sDest[port], &packetList);
		b = ptr[0] & 0xf0;
		if (b == 0x80 || b == 0x90) {
			char *pp = &sNoteOnTable[port][ptr[0] & 0x0f][ptr[1]];
			if (b == 0x80 || ptr[2] == 0) {
				/*  Note off  */
				(*pp)--;
			} else {
				/*  Note on  */
				(*pp)++;
			}
		}
	}
	return sts;
}

int
send_midi_message(uint8_t *data, int length)
{
	if (data == NULL || length <= 0)
		return 0;
	return (int)CoreMIDISend(data, length, 0, 0);
}

/*
 * WORKAROUND / quick hack. md_put() だけがまだ 1 バイトずつ渡してくる。
 * MIDI IN の生バイト（モニターやエディタの THRU）用で、mix_out は
 * put_midi_message() を通り、ここには来ない。
 * 共通の MIDI スタックをメッセージ単位にするとき、このバッファは消す。
 * UNIX の 1 バイト write には広げない。
 */
static uint8_t sMidiByteBuf[kMaxMIDIMessageSize];
static int sMidiByteLen;
static int sMidiByteNeed; /* 0: idle, >0: message size, -1: sysex until F7 */
static uint8_t sMidiRunningStatus;

static int
MidiVoiceLength(uint8_t status)
{
	switch (status & 0xf0) {
	case 0x80: case 0x90: case 0xa0: case 0xb0: case 0xe0:
		return 3;
	case 0xc0: case 0xd0:
		return 2;
	default:
		return 0;
	}
}

static int
MidiSystemLength(uint8_t status)
{
	switch (status) {
	case 0xf1: case 0xf3:
		return 2;
	case 0xf2:
		return 3;
	default:
		return 1;
	}
}

static int
SendAssembledMidi(void)
{
	int n = sMidiByteLen;

	sMidiByteLen = 0;
	sMidiByteNeed = 0;
	if (n <= 0)
		return 0;
	return (int)CoreMIDISend(sMidiByteBuf, n, 0, 0);
}

int
send_midi_byte(uint8_t byte)
{
	int voice;

	/* Realtime may arrive inside another message. Send it and keep assembling. */
	if (byte >= 0xf8)
		return (int)CoreMIDISend(&byte, 1, 0, 0);

	if (byte >= 0x80) {
		if (sMidiByteNeed < 0 && byte == 0xf7) {
			if (sMidiByteLen < kMaxMIDIMessageSize)
				sMidiByteBuf[sMidiByteLen++] = byte;
			return SendAssembledMidi();
		}
		sMidiByteBuf[0] = byte;
		sMidiByteLen = 1;
		if (byte == 0xf0) {
			sMidiByteNeed = -1;
			sMidiRunningStatus = 0;
			return 0;
		}
		if (byte < 0xf0) {
			sMidiRunningStatus = byte;
			sMidiByteNeed = MidiVoiceLength(byte);
		} else {
			sMidiRunningStatus = 0;
			sMidiByteNeed = MidiSystemLength(byte);
		}
		if (sMidiByteNeed <= 1)
			return SendAssembledMidi();
		return 0;
	}

	if (sMidiByteNeed < 0) {
		if (sMidiByteLen >= kMaxMIDIMessageSize - 1) {
			sMidiByteLen = 0;
			sMidiByteNeed = 0;
			return 0;
		}
		sMidiByteBuf[sMidiByteLen++] = byte;
		return 0;
	}

	/* Data without a status continues the previous channel message. */
	if (sMidiByteLen == 0) {
		voice = MidiVoiceLength(sMidiRunningStatus);
		if (voice == 0)
			return 0;
		sMidiByteBuf[0] = sMidiRunningStatus;
		sMidiByteLen = 1;
		sMidiByteNeed = voice;
	}
	if (sMidiByteLen >= kMaxMIDIMessageSize)
		return 0;
	sMidiByteBuf[sMidiByteLen++] = byte;
	if (sMidiByteNeed > 0 && sMidiByteLen >= sMidiByteNeed)
		return SendAssembledMidi();
	return 0;
}


void
CoreMIDIAllSoundOff(void)
{
	int port, ch;
	
	/*  Flush all output that are scheduled for future delivery  */
	MIDIFlushOutput(NULL);
	/*  All sound off  */
	for (port = 0; port < 2; port++) {
		for (ch = 0; ch < 16; ch++) {
			uint8_t buf[4];
			/*  All sound off  */
			buf[0] = 0xb0 + ch;
			buf[1] = 0x78;
			buf[2] = 0;
			CoreMIDISend(buf, 3, port, 0);
		}
	}
}

#if DEBUG_RCP
static void
dump_debug_rcp(void)
{
	FILE *fp = fopen("sted2_debug_rcp.log", "w");
	int n = 0;
	int m;
	uint8_t *p;
	static uint8_t tbl[16][128];
	uint8_t *pp;
	memset(tbl, 0, sizeof(tbl));
	while ((size_t)n < sRCPDebugCount) {
		UInt32 t1, t2;
		uint8_t c;
		p = sRCPDebugBuffer + n;
		t1 = p[0] + ((UInt32)p[1] << 8) + ((UInt32)p[2] << 16) + ((UInt32)p[3] << 24);
		t2 = p[4] + ((UInt32)p[5] << 8) + ((UInt32)p[6] << 16) + ((UInt32)p[7] << 24);
		fprintf(fp, "%-10d %-10d", (int)t1, (int)t2);
		m = p[8];
		c = p[9];
		if ((c & 0xf0) == 0x80 || ((c & 0xf0) == 0x90 && p[11] == 0)) {
			pp = &tbl[c & 0x0f][p[10] & 0x7f];
			(*pp)--;
			fprintf(fp, "%2d OFF %3d   0 [%d]\n", c & 0x0f, p[10], *pp);
		} else if ((c & 0xf0) == 0x90) {
			pp = &tbl[c & 0x0f][p[10] & 0x7f];
			(*pp)++;
			fprintf(fp, "%2d ON  %3d %3d [%d]\n", c & 0x0f, p[10], p[11], *pp);
		} else {
			int i;
			for (i = 0; i < m && i < 8; i++) {
				fprintf(fp, "%02x ", p[9 + i]);
			}
			fprintf(fp, "\n");
		}
		n += 9 + m;
	}
	for (n = 0; n < 16; n++) {
		for (m = 0; m < 128; m++) {
			if (tbl[n][m] != 0) {
				fprintf(fp, "%d:%d(%d) ", n, m, tbl[n][m]);
			}
		}
	}
	fprintf(fp, "\n");
	fclose(fp);
}

#endif

#define ConvertMicrosToHostTime(t) (AudioConvertNanosToHostTime((t) * 1000))

static void rcpplayer_update_clock(void)
{
	MIDITimeStamp now = AudioGetCurrentHostTime();
	if (now >= sStartTimeStamp) {
		UInt64 elapsed_micros = AudioConvertHostTimeToNanos(now - sStartTimeStamp) / 1000;
		int32_t current_step = rcpplayer_microseconds_to_step(elapsed_micros);
		if (rcd != NULL) {
			rcd->stepcount = current_step;
			rcd->act = 1;
		}
	}
}

/*  Player engine; to be called after rcpplayer_init() in a separate thread  */
void
rcpplayer_engine(void)
{
	MIDITimeStamp currentTime, nextTime, nextEventTime, offsetTime, prefetchTime;
	int32_t step;
	uint8_t *ptr;
	int length, port;
	OSStatus sts;
	MIDITimeStamp waitPerMIDIByte[kMaxNumberOfPorts];
	
	sStartTimeStamp = AudioGetCurrentHostTime();
	currentTime = sStartTimeStamp;

	for (port = 0; port < kMaxNumberOfPorts; port++) {
		waitPerMIDIByte[port] = ConvertMicrosToHostTime(sWaitPerMIDIByte[port]);
		sNextEarliestTimeForPort[port] = sStartTimeStamp;
	}
	
	prefetchTime = ConvertMicrosToHostTime(kMaxPrefetchMicroseconds);
	offsetTime = ConvertMicrosToHostTime(sOffsetMIDITime);

	while (!sStopPlaying) {
		rcpplayer_update_clock();

		if (rcpplayer_next_midi_event(&step, &ptr, &length, &port)) {
			if (length == 0 || ptr[0] == 0xff)
				continue;

			nextEventTime = sStartTimeStamp + ConvertMicrosToHostTime(rcpplayer_step_to_microseconds(step));
			if (nextEventTime > currentTime + prefetchTime) {
				usleep(AudioConvertHostTimeToNanos(nextEventTime - currentTime - prefetchTime) / 1000);
				currentTime = AudioGetCurrentHostTime();
			}

			nextTime = nextEventTime;
			if (nextTime < sNextEarliestTimeForPort[port])
				nextTime = sNextEarliestTimeForPort[port];
			if (nextTime < sLastTimeStamp + offsetTime)
				nextTime = sLastTimeStamp + offsetTime;
			sLastTimeStamp = nextTime;
			sts = CoreMIDISend(ptr, length, port, nextTime);
			sNextEarliestTimeForPort[port] = nextTime + waitPerMIDIByte[port] * length;
		} else if (!sRecording) {
			break;
		} else {
			/* Do not call rcpplayer_next_midi_event() again. The next
			   call would add the million-step sentinel to the clock. */
			while (sRecording && !sStopPlaying) {
				rcpplayer_update_clock();
				usleep(5000);
			}
			break;
		}
	}
	if (rcd != NULL) {
		rcd->act = 0;
	}
}

void
stop_coremidi_rcpplayer(void)
{
	int port, ch, i, j;
	UInt64 nextTime;
	CoreMIDIRCPPlayer *player = [CoreMIDIRCPPlayer currentPlayer];
	if ([player isPlaying]) {
		sStopPlaying = 1;
		while ([player isPlaying])
			usleep(100000);
	}
	/*  Send Note-off  */
	nextTime = sLastTimeStamp;
	for (port = 0; port < 2; port++) {
		for (ch = 0; ch < 16; ch++) {
			for (i = 0; i < 128; i++) {
				for (j = sNoteOnTable[port][ch][i]; j > 0; j--) {
					uint8_t buf[4];
					buf[0] = 0x80 + ch;
					buf[1] = i;
					buf[2] = 0;
					CoreMIDISend(buf, 3, port, AudioConvertNanosToHostTime(nextTime));
					nextTime += 1000000;  /*  Wait 1 millisecond (= 1000000 nanoseconds) */
				}
				sNoteOnTable[port][ch][i] = 0;
			}
		}
	}
	if (rcd != NULL) {
		rcd->act = 0;
	}
	
#if DEBUG_RCP
	dump_debug_rcp();
#endif
	
}

void
play_coremidi_rcpplayer(uint8_t *rcp_data_ptr, int data_length)
{
	CoreMIDIRCPPlayer *player;
	STed_InitMeasureConversion();
	rcpplayer_init(rcp_data_ptr, data_length);
#if DEBUG_RCP
	sRCPDebugBuffer = (uint8_t *)realloc(sRCPDebugBuffer, sRCPDebugSize);
	sRCPDebugCount = 0;
#endif
	player = [CoreMIDIRCPPlayer currentPlayer];
	if ([player isPlaying]) {
		stop_coremidi_rcpplayer();
	}
	sLastTimeStamp = 0;
	[NSThread detachNewThreadSelector:@selector(startPlaying:) toTarget:player withObject:nil];
}

int
is_coremidi_playing(void)
{
	if ([[CoreMIDIRCPPlayer currentPlayer] isPlaying])
		return 1;
	else return 0;
}

void
exit_coremidi_rcpplayer(void)
{
	/* Do nothing */
}

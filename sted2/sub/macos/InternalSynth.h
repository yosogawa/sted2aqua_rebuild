#ifndef INTERNAL_SYNTH_H
#define INTERNAL_SYNTH_H

#include <CoreMIDI/CoreMIDI.h>

#ifdef __cplusplus
extern "C" {
#endif

void InternalSynthCreateDestinations(MIDIClientRef client);
int InternalSynthAnyDestination(const MIDIEndpointRef *dests);
void InternalSynthEnsureRunning(void);
void InternalSynthEnsureStopped(void);
int InternalSynthSchedule(MIDIEndpointRef dest, const uint8_t *bytes, int length,
	MIDITimeStamp atTime, OSStatus *status);

void CoreMIDISetDLSSynthFont(const char *filename);
const char *CoreMIDIDLSSynthFontFilename(void);

typedef struct {
	float globalGain;     /* dB, -36 .. 0. EQ globalGain, used as trim. */
	float bassGain;       /* dB, -6 .. +12. LowShelf at 60 Hz. */
	float trebleGain;     /* dB, -6 .. +12. HighShelf at 5500 Hz. */
	int reverbPreset;     /* AVAudioUnitReverbPreset, 0 .. 12. */
	float reverbMix;      /* wet/dry percent, 0 .. 100. */
	float limiterBoost;   /* dB, 0 .. 6. Peak limiter preGain. */
	float volume;         /* main mixer outputVolume, 0 .. 1. */
	int totalEffect;      /* 0 bypasses EQ, reverb, and limiter. 1 runs them. */
} DLSEffectSettings;

void CoreMIDIGetDLSEffects(DLSEffectSettings *settings);
void CoreMIDISetDLSEffects(const DLSEffectSettings *settings);

typedef struct {
	int useReverb; /* 0 or 1. kMusicDeviceProperty_UsesInternalReverb. */
	int roomType;  /* kAudioUnitProperty_ReverbRoomType, 0 .. 12. */
} DLSInternalReverb;

void CoreMIDIGetDLSInternalReverb(DLSInternalReverb *settings);
void CoreMIDISetDLSInternalReverb(const DLSInternalReverb *settings);

#ifdef __cplusplus
}
#endif

#endif

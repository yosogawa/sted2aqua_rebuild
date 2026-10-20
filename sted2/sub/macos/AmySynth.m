#import "AmySynth.h"

#include "../../../third_party/amy/src/amy.h"

#include <CoreAudio/CoreAudio.h>
#include <math.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

static int sAmyStarted = 0;
static int16_t sLeft[AMY_BLOCK_SIZE];
static int16_t sRight[AMY_BLOCK_SIZE];
static int sPos = AMY_BLOCK_SIZE;
static int64_t sGenerated = 0;
static _Atomic int64_t sAnchorSample = 0;
static _Atomic uint64_t sAnchorHost = 0;
/* -1 until this channel receives CC#0. 0 is Juno, so it cannot mean "unset". */
static int sBank[17];

extern void amy_host_use_gamma9001_drums(void);

static void
ResetRenderQueue(void)
{
	sPos = AMY_BLOCK_SIZE;
	sGenerated = 0;
	atomic_store_explicit(&sAnchorSample, 0, memory_order_relaxed);
	atomic_store_explicit(&sAnchorHost, 0, memory_order_relaxed);
	for (int i = 0; i < 17; i++)
		sBank[i] = -1;
}

void
AmySynthStart(void)
{
	amy_config_t config;

	if (sAmyStarted)
		return;
	ResetRenderQueue();
	config = amy_default_config();
	config.audio = AMY_AUDIO_IS_NONE;
	config.midi = AMY_MIDI_IS_NONE;
	/* Juno patch strings set the chorus. Echo and reverb stay available. */
	config.features.reverb = 1;
	config.features.echo = 1;
	config.features.chorus = 1;
	config.features.audio_in = 0;
	config.features.startup_bleep = 0;
	/* Channel 1 Juno, channel 2 DX7, channel 10 drums, so a song can sound
	 * before its own program changes land.
	 */
	config.features.default_synths = 1;
	config.platform.multithread = 0;
	config.platform.multicore = 0;
	config.overload_threshold = 0;
	config.max_sequencer_tags = 0;
	amy_host_use_gamma9001_drums();
	amy_start(config);
	amy_external_midi_sync(AMY_MIDI_SYNC_OFF);
	sAmyStarted = 1;
}

void
AmySynthStop(void)
{
	if (!sAmyStarted)
		return;
	sAmyStarted = 0;
	amy_stop();
	ResetRenderQueue();
}

static void
PullBlock(void)
{
	int16_t *block;

	if (!sAmyStarted) {
		memset(sLeft, 0, sizeof sLeft);
		memset(sRight, 0, sizeof sRight);
		sPos = 0;
		return;
	}
	block = amy_simple_fill_buffer();
	if (block == NULL) {
		memset(sLeft, 0, sizeof sLeft);
		memset(sRight, 0, sizeof sRight);
	} else {
		int i;

		for (i = 0; i < AMY_BLOCK_SIZE; i++) {
			sLeft[i] = block[(AMY_NCHANS * i) + 0];
			sRight[i] = block[(AMY_NCHANS * i) + 1];
		}
	}
	sGenerated += AMY_BLOCK_SIZE;
	sPos = 0;
}

void
AmySynthFill(float *left, float *right, int stride, int frames, uint64_t hostTime)
{
	int64_t first;
	int i;

	if (left == NULL || right == NULL || frames <= 0)
		return;
	if (sPos >= AMY_BLOCK_SIZE)
		first = sGenerated;
	else
		first = sGenerated - AMY_BLOCK_SIZE + sPos;
	if (hostTime != 0) {
		atomic_store_explicit(&sAnchorSample, first, memory_order_relaxed);
		atomic_store_explicit(&sAnchorHost, hostTime, memory_order_relaxed);
	}
	for (i = 0; i < frames; i++) {
		float l, r;
		if (sPos >= AMY_BLOCK_SIZE)
			PullBlock();
		l = (float)sLeft[sPos] * (1.0f / 32768.0f);
		r = (float)sRight[sPos] * (1.0f / 32768.0f);
		sPos++;
		left[i * stride] = l;
		right[i * stride] = r;
	}
}

static uint32_t
MsForHostTime(uint64_t atTime)
{
	uint64_t anchorHost;
	int64_t anchorSample;
	double freq, deltaSec, sample;

	if (!sAmyStarted)
		return 0;
	anchorHost = atomic_load_explicit(&sAnchorHost, memory_order_relaxed);
	anchorSample = atomic_load_explicit(&sAnchorSample, memory_order_relaxed);
	freq = AudioGetHostClockFrequency();
	if (freq <= 0.0)
		return amy_sysclock();
	if (anchorHost == 0 || atTime == 0) {
		deltaSec = 0.0;
		if (atTime != 0)
			deltaSec = ((double)atTime - (double)AudioGetCurrentHostTime()) / freq;
		if (deltaSec < 0.0)
			deltaSec = 0.0;
		return amy_sysclock() + (uint32_t)llround(deltaSec * 1000.0);
	}
	deltaSec = ((double)((int64_t)atTime - (int64_t)anchorHost)) / freq;
	sample = (double)anchorSample + deltaSec * (double)AMY_SAMPLE_RATE;
	if (sample < 0.0)
		sample = 0.0;
	return (uint32_t)llround(sample * 1000.0 / (double)AMY_SAMPLE_RATE);
}

static void
SendEvent(amy_event *event, uint64_t atTime)
{
	event->time = MsForHostTime(atTime);
	amy_add_event(event);
}

void
AmySynthSendMIDI(const uint8_t *bytes, int length, uint64_t atTime)
{
	uint8_t status, channel, data1, data2;
	amy_event event;

	if (!sAmyStarted || bytes == NULL || length < 1)
		return;
	if (bytes[0] == 0xf0) {
		/* AMY wire sysex is F0 00 03 45 <ASCII wire message> F7.
		 * Other sysex is left untouched for a later mapping.
		 */
		char *message;
		int n;
		if (length < 6 || bytes[length - 1] != 0xf7)
			return;
		if (bytes[1] != 0x00 || bytes[2] != 0x03 || bytes[3] != 0x45)
			return;
		n = length - 5;
		if (n <= 0)
			return;
		message = malloc((size_t)n + 1);
		if (message == NULL)
			return;
		memcpy(message, bytes + 4, (size_t)n);
		message[n] = 0;
		amy_send_wire_from_sysex(message);
		free(message);
		return;
	}
	status = bytes[0] & 0xf0;
	channel = (uint8_t)((bytes[0] & 0x0f) + 1);
	if (channel < 1 || channel > 16)
		return;
	data1 = length > 1 ? bytes[1] : 0;
	data2 = length > 2 ? bytes[2] : 0;
	event = amy_default_event();
	event.synth = channel;
	/* Note-ons must not claim they already came through AMY's MIDI map.
	 * Drum kits use that map (one sample per note). Marking the source
	 * channel skips it, and every key pitches the same sample.
	 */
	if (status != 0x80 && status != 0x90)
		event.note_source_channel = channel;
	if (status == 0x80 || (status == 0x90 && data2 == 0)) {
		if (length < 2)
			return;
		event.midi_note = (float)data1;
		event.velocity = 0;
		SendEvent(&event, atTime);
		return;
	}
	if (status == 0x90) {
		if (length < 3)
			return;
		event.midi_note = (float)data1;
		event.velocity = (float)data2 / 127.0f;
		SendEvent(&event, atTime);
		return;
	}
	if (status == 0xc0) {
		int bank, patch;
		if (length < 2)
			return;
		/* A program change with no CC#0 stays in the bank already sounding.
		 * Channel 10 starts on patch 384, so a bare PC keeps the drum kits
		 * instead of falling through to Juno at bank 0.
		 */
		bank = sBank[channel];
		if (bank < 0)
			bank = instrument_bank_number(channel);
		if (bank < 0) {
			patch = instrument_get_patch_number(channel);
			if (patch >= 0)
				bank = (patch & 0xFF80) >> 7;
			else
				bank = 0;
			/* Patches 256-383 are not a full bank. AMY treats that as Juno. */
			if (bank == 2)
				bank = 0;
		}
		event.patch_number = (uint16_t)(data1 + 128 * bank);
		SendEvent(&event, atTime);
		return;
	}
	if (status == 0xe0) {
		int bend;
		if (length < 3)
			return;
		bend = (int)((data2 << 7) | data1) - 8192;
		event.pitch_bend = (float)bend / (6.0f * 8192.0f);
		SendEvent(&event, atTime);
		return;
	}
	if (status == 0xb0) {
		if (length < 3)
			return;
		if (data1 == 0) {
			/* The part's own bank table already picked this number.
			 * Port A and port B do not share it, so nothing is rewritten.
			 */
			sBank[channel] = data2;
			instrument_set_bank_number(channel, data2);
			return;
		}
		if (data1 == 64) {
			event.pedal = data2;
			SendEvent(&event, atTime);
			return;
		}
		if (data1 == 123) {
			event.midi_note = 0;
			event.velocity = 0;
			SendEvent(&event, atTime);
		}
	}
}

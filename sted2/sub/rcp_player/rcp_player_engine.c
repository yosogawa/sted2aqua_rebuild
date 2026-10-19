#include "rcp_player_engine.h"
#include "rcp_player.h"
#include "rcp_transport.h"

#include "sted.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreMIDI/CoreMIDI.h>
#include <string.h>
#include <unistd.h>

#define kMaxNumberOfPorts 2
#define kMaxPrefetchMicroseconds 1000000
#define kMaxMIDIMessageSize 256

extern OSStatus CoreMIDISend(uint8_t *ptr, int length, int port, MIDITimeStamp atTime);

static uint8_t sStopPlaying;
static volatile uint8_t sRecording;

void set_realtime_recording(int on)
{
	sRecording = on ? 1 : 0;
}

static MIDITimeStamp sStartTimeStamp;
static MIDITimeStamp sLastTimeStamp;
/* Held. After leaving it, events are timed from sResumeHost,
   not from the original song-start host time. */
static int sHoldActive;
static int sClockFrozen;
static int sUseResumeClock;
static MIDITimeStamp sResumeHost;
static double sResumeUs;
/* Musical time captured on entry to Held. Resume is measured from here. */
static double sHeldUs;
/* Event already taken from the parser. Kept across Held so resume does not skip it. */
static int sParked;
static int32_t sParkStep;
static int sParkLen;
static int sParkPort;
static uint8_t sParkMsg[kMaxMIDIMessageSize];
static char sNoteOnTable[kMaxNumberOfPorts][16][128];
static unsigned char sNoteVelocity[kMaxNumberOfPorts][16][128];
/* Notes that were sounding when Held began. The live table is cleared by note-off. */
static char sHeldCount[kMaxNumberOfPorts][16][128];

//  Next earliest timestamp for each port (to avoid too thick MIDI byte sequence)
static MIDITimeStamp sNextEarliestTimeForPort[kMaxNumberOfPorts];

//  Microsecond wait per MIDI byte (it depends on the hardware feature, so it should be 
//  assigned per port)
static int sWaitPerMIDIByte[kMaxNumberOfPorts];

//  Microsecond offset to avoid 'simultaneous event' in the different port
static int sOffsetMIDITime = 1;

#define ConvertMicrosToHostTime(t) (AudioConvertNanosToHostTime((t) * 1000))

static void rcpplayer_update_clock(void)
{
	MIDITimeStamp now;

	/* Pause, and the gap until the first resumed event is anchored.
	   Advancing here would skip the frozen musical position. */
	if (sClockFrozen || sHoldActive)
		return;
	now = AudioGetCurrentHostTime();
	if (now >= sStartTimeStamp) {
		UInt64 elapsed_micros = AudioConvertHostTimeToNanos(now - sStartTimeStamp) / 1000;
		/* rcpplayer_microseconds_to_step takes uint32_t. The value wraps after 2^32 us (about 71.6 minutes). */
		int32_t current_step = rcpplayer_microseconds_to_step((uint32_t)elapsed_micros);
		if (rcd != NULL) {
			rcd->stepcount = current_step;
			rcp_transport_set_active(1);
		}
	}
}

static int
rcpplayer_is_stop(void)
{
	if (rcd == NULL)
		return 0;
	return rcp_transport_get_phase() == RCP_TRANSPORT_HELD;
}

static MIDITimeStamp
rcpplayer_us_to_host(double us)
{
	if (us <= 0)
		return 0;
	return ConvertMicrosToHostTime(us);
}

/* Note-offs for whatever this play already sent. Does not stop the engine.
   The counts are kept in sHeldCount so leaving Held can sound them again. */
static void
rcpplayer_release_held_notes(void)
{
	int port, ch, note;
	uint8_t buf[3];

	for (port = 0; port < kMaxNumberOfPorts; port++) {
		for (ch = 0; ch < 16; ch++) {
			for (note = 0; note < 128; note++) {
				int count = sNoteOnTable[port][ch][note];
				int j;
				sHeldCount[port][ch][note] = (char)count;
				if (count <= 0)
					continue;
				sNoteOnTable[port][ch][note] = 0;
				buf[0] = (uint8_t)(0x80 + ch);
				buf[1] = (uint8_t)note;
				buf[2] = 0;
				for (j = 0; j < count; j++)
					CoreMIDISend(buf, 3, port, 0);
				sNoteOnTable[port][ch][note] = 0;
			}
		}
	}
}

/* Note-ons for the notes release_held_notes silenced. */
static void
rcpplayer_restore_held_notes(void)
{
	int port, ch, note;
	uint8_t buf[3];

	for (port = 0; port < kMaxNumberOfPorts; port++) {
		for (ch = 0; ch < 16; ch++) {
			for (note = 0; note < 128; note++) {
				int count = sHeldCount[port][ch][note];
				int j;
				unsigned char vel;
				if (count <= 0)
					continue;
				sHeldCount[port][ch][note] = 0;
				vel = sNoteVelocity[port][ch][note];
				if (vel == 0)
					vel = 100;
				buf[0] = (uint8_t)(0x90 + ch);
				buf[1] = (uint8_t)note;
				buf[2] = vel;
				for (j = 0; j < count; j++)
					CoreMIDISend(buf, 3, port, 0);
			}
		}
	}
}

/* Pause only while the phase is Held. Search keeps running.
   Returns 1 while the clock is held. Leaving Held sounds the held notes
   again and times the next event from the musical position at entry. */
static int
rcpplayer_hold_if_stopped(void)
{
	int port;

	if (!rcpplayer_is_stop()) {
		if (sHoldActive) {
			MIDITimeStamp back;
			sLastTimeStamp = 0;
			for (port = 0; port < kMaxNumberOfPorts; port++)
				sNextEarliestTimeForPort[port] = 0;
			sHoldActive = 0;
			sUseResumeClock = 1;
			sResumeUs = sHeldUs;
			sResumeHost = AudioGetCurrentHostTime();
			back = rcpplayer_us_to_host(sResumeUs);
			if (sResumeHost > back)
				sStartTimeStamp = sResumeHost - back;
			sClockFrozen = 0;
			rcpplayer_restore_held_notes();
		}
		return 0;
	}
	if (!sHoldActive) {
		MIDITimeStamp now = AudioGetCurrentHostTime();
		uint32_t elapsed = 0;
		sHeldUs = 0;
		if (now >= sStartTimeStamp)
			elapsed = (uint32_t)(AudioConvertHostTimeToNanos(now - sStartTimeStamp) / 1000);
		/* Same unit as event times. Raw wall time is not. */
		sHeldUs = rcpplayer_step_to_microseconds(rcpplayer_microseconds_to_step(elapsed));
		sHoldActive = 1;
		sClockFrozen = 1;
		rcpplayer_release_held_notes();
	}
	return 1;
}

/* Returns 1 if STOP or shutdown happened before the event entered the prefetch window. */
static int
rcpplayer_wait_prefetch(MIDITimeStamp nextEventTime, MIDITimeStamp prefetchTime)
{
	while (!sStopPlaying && !rcpplayer_is_stop()) {
		MIDITimeStamp now = AudioGetCurrentHostTime();
		rcpplayer_update_clock();
		if (nextEventTime <= now + prefetchTime)
			return 0;
		usleep(5000);
	}
	return 1;
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
	sHoldActive = 0;
	sClockFrozen = 0;
	sUseResumeClock = 0;
	sResumeHost = 0;
	sResumeUs = 0;
	sHeldUs = 0;
	sParked = 0;
	memset(sHeldCount, 0, sizeof sHeldCount);

	for (port = 0; port < kMaxNumberOfPorts; port++) {
		waitPerMIDIByte[port] = ConvertMicrosToHostTime(sWaitPerMIDIByte[port]);
		sNextEarliestTimeForPort[port] = sStartTimeStamp;
	}
	
	prefetchTime = ConvertMicrosToHostTime(kMaxPrefetchMicroseconds);
	offsetTime = ConvertMicrosToHostTime(sOffsetMIDITime);

	while (!sStopPlaying) {
		if (rcpplayer_hold_if_stopped()) {
			usleep(5000);
			continue;
		}
		currentTime = AudioGetCurrentHostTime();

		if (!sParked) {
			if (!rcpplayer_next_midi_event(&step, &ptr, &length, &port)) {
				if (!sRecording)
					break;
				/* Do not call rcpplayer_next_midi_event() again. The next
				   call would add the million-step sentinel to the clock. */
				while (sRecording && !sStopPlaying) {
					if (rcpplayer_hold_if_stopped()) {
						usleep(5000);
						continue;
					}
					rcpplayer_update_clock();
					usleep(5000);
				}
				break;
			}
			if (length == 0 || ptr == NULL || ptr[0] == 0xff) {
				rcpplayer_update_clock();
				continue;
			}
			if (length > kMaxMIDIMessageSize)
				length = kMaxMIDIMessageSize;
			memcpy(sParkMsg, ptr, (size_t)length);
			sParkLen = length;
			sParkPort = port;
			sParkStep = step;
			sParked = 1;
		}

		/* After Held, sResumeUs is the position at entry, not this event.
		   A future note-off stays in the future by its remaining gate. */
		rcpplayer_update_clock();

		if (sUseResumeClock) {
			double eventUs = rcpplayer_step_to_microseconds(sParkStep);
			double delta = eventUs - sResumeUs;
			/* Behind the parser would flush the rest of the song.
			   Far ahead would wait out the whole position. Either way
			   XF3 never sounds like a resume. Keep this event's gap. */
			if (delta < 0 || delta > 30000000.0) {
				sResumeUs = eventUs;
				delta = 0;
			}
			nextEventTime = sResumeHost + rcpplayer_us_to_host(delta);
		} else
			nextEventTime = sStartTimeStamp + rcpplayer_us_to_host(rcpplayer_step_to_microseconds(sParkStep));
		if (rcpplayer_wait_prefetch(nextEventTime, prefetchTime))
			continue;

		currentTime = AudioGetCurrentHostTime();
		nextTime = nextEventTime;
		if (nextTime < currentTime)
			nextTime = currentTime;
		if (sParkPort >= 0 && sParkPort < kMaxNumberOfPorts
		    && nextTime < sNextEarliestTimeForPort[sParkPort])
			nextTime = sNextEarliestTimeForPort[sParkPort];
		if (sLastTimeStamp != 0 && nextTime < sLastTimeStamp + offsetTime)
			nextTime = sLastTimeStamp + offsetTime;
		if (sParkPort < 0 || sParkPort >= kMaxNumberOfPorts)
			sParkPort = 0;
		sLastTimeStamp = (nextTime != 0) ? nextTime : currentTime;
		sts = CoreMIDISend(sParkMsg, sParkLen, sParkPort, nextTime);
		sNextEarliestTimeForPort[sParkPort] = sLastTimeStamp + waitPerMIDIByte[sParkPort] * sParkLen;
		sParked = 0;
		(void)sts;
	}
	rcp_transport_set_active(0);
}


void
rcpplayer_request_stop(void)
{
	sStopPlaying = 1;
}

void
rcpplayer_clear_stop(void)
{
	sStopPlaying = 0;
}

void
rcpplayer_reset_last_timestamp(void)
{
	sLastTimeStamp = 0;
}

void
rcpplayer_set_output_timing(int waitPerByteMicros, int offsetMicros)
{
	int i;

	for (i = 0; i < kMaxNumberOfPorts; i++)
		sWaitPerMIDIByte[i] = waitPerByteMicros;
	sOffsetMIDITime = offsetMicros;
}

void
rcpplayer_record_note(int port, const uint8_t *msg, int length)
{
	uint8_t b;
	char *pp;

	(void)length;
	if (msg == NULL || port < 0 || port >= kMaxNumberOfPorts)
		return;
	b = msg[0] & 0xf0;
	if (msg[0] == 0xf0 || (b != 0x80 && b != 0x90))
		return;
	pp = &sNoteOnTable[port][msg[0] & 0x0f][msg[1]];
	if (b == 0x80 || msg[2] == 0)
		(*pp)--;
	else {
		(*pp)++;
		sNoteVelocity[port][msg[0] & 0x0f][msg[1]] = msg[2];
	}
}

void
rcpplayer_send_pending_note_offs(void)
{
	int port, ch, i, j;
	UInt64 nextTime;

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
}

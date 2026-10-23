//
//  CoreMIDIRCPPlayer.m
//  STed2_aqua
//
//  Created by Toshi Nagata on Sun Jun 08 2003.
//  Copyright (c) 2003 Toshi Nagata. All rights reserved.
//

#import "rcp_player.h"
#import "rcp_player_engine.h"
#import "CoreMIDIRCPPlayer.h"
#import "InternalSynth.h"

#include <CoreMIDI/CoreMIDI.h>
#include <unistd.h>
#include <string.h>

#include "sted.h"
#include "rcp_transport.h"

extern int STed_InitMeasureConversion(void);

#define kMaxNumberOfPorts 2

#define kMaxMIDIMessageSize  256  /* Should match RCP_MAX_RESULT_SMF_SIZE in rcp.h */


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
static MIDIClientRef	sMIDIClientRef = 0;
static MIDIPortRef		sMIDIOutputPortRef = 0;
static MIDIEndpointRef	sDest[kMaxNumberOfPorts];

static MIDISysexSendRequest sSysexRequest[kMaxNumberOfPorts];
static uint8_t sSysexSendBuffer[kMaxNumberOfPorts][kMaxMIDIMessageSize];


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
	rcpplayer_clear_stop();
	rcpplayer_engine();
	playing = NO;
	rcpplayer_clear_stop();
	rcp_transport_set_active(0);
}

- (BOOL)isPlaying
{
	return playing;
}

@end


void CoreMIDIConnectDefaultInputs(void);

void
CoreMIDIInitIfNeeded(void)
{
	int i;
	if (sMIDIClientRef == 0) {
		MIDIClientCreate(CFSTR("STed2 Mac"), NULL, NULL, &sMIDIClientRef);
		if (sMIDIOutputPortRef == 0)
			MIDIOutputPortCreate(sMIDIClientRef, CFSTR("STed2 output port"), &sMIDIOutputPortRef);
		if (sMIDIInputPortRef == 0)
			MIDIInputPortCreate(sMIDIClientRef, CFSTR("STed2 input port"), CoreMIDIReadProc, NULL, &sMIDIInputPortRef);
		InternalSynthCreateDestinations(sMIDIClientRef);
		for (i = 0; i < sizeof(sDest) / sizeof(sDest[0]); i++)
			sDest[i] = 0;

		CoreMIDIConnectDefaultInputs();
	}
	rcpplayer_set_output_timing(300, 1);
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
			&& enref != 0
			&& MIDIEntityGetDevice(enref, &dref) == noErr
			&& dref != 0) {
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
	MIDIEndpointRef next[kMaxNumberOfPorts];
	int i;

	CoreMIDIInitIfNeeded();
	port = port % kMaxNumberOfPorts;
	dest = MIDIGetDestination(index);
	if (InternalSynthIsAmyDestination(dest)
		&& InternalSynthIsAmyDestination(sDest[(port + 1) % kMaxNumberOfPorts]))
		return;
	for (i = 0; i < kMaxNumberOfPorts; i++)
		next[i] = sDest[i];
	next[port] = dest;
	InternalSynthApplyAmyPort(next);
	if (InternalSynthAnyDestination(next))
		InternalSynthEnsureRunning();
	sDest[port] = dest;
	if (!InternalSynthAnyDestination(sDest))
		InternalSynthEnsureStopped();
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

int
CoreMIDISelectDeviceByNameForPort(const char *name, int port)
{
	int n;
	NSString *str;
	CoreMIDIInitIfNeeded();
	name = CoreMIDICanonicalOutputName(name);
	if (name == NULL)
		return 0;
	port = port % kMaxNumberOfPorts;
	n = 0;
	while ((str = CoreMIDIDeviceNameAtIndex(n)) != nil) {
		if (strcasecmp(name, [str UTF8String]) == 0) {
			MIDIEndpointRef dest = MIDIGetDestination(n);
			MIDIEndpointRef other = sDest[1 - port];
			if (InternalSynthIsAmyDestination(dest) && InternalSynthIsAmyDestination(other))
				return -1;
			CoreMIDISelectDeviceForPort(n, port);
			return 1;
		}
		n++;
	}
	return 0;
}

#ifdef __OBJC__
NSString *
CoreMIDISelectedOutputName(int port)
{
	CoreMIDIInitIfNeeded();
	port = port % kMaxNumberOfPorts;
	if (sDest[port] == 0)
		return nil;
	return CoreMIDIEndpointDisplayName(sDest[port]);
}
#endif

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
	if (length <= 0 || sDest[port] == 0)
		return 0;
	if (InternalSynthSchedule(sDest[port], ptr, length, atTime, &sts)) {
		rcpplayer_record_note(port, ptr, length);
		return sts;
	}
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
		/* Short messages only (1–3 bytes from callers). Size check omitted on purpose. */
		packetPtr = MIDIPacketListInit(&packetList);
		packetPtr = MIDIPacketListAdd(&packetList, sizeof(packetList), packetPtr, atTime, length, ptr);
		sts = MIDISend(sMIDIOutputPortRef, sDest[port], &packetList);
		rcpplayer_record_note(port, ptr, length);
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
	MIDIFlushOutput(0);
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


void
stop_coremidi_rcpplayer(void)
{
	CoreMIDIRCPPlayer *player = [CoreMIDIRCPPlayer currentPlayer];
	if ([player isPlaying]) {
		rcpplayer_request_stop();
		while ([player isPlaying])
			usleep(100000);
	}
	rcpplayer_send_pending_note_offs();
	rcp_transport_set_active(0);
	/* The player thread has exited. Recording Stopped now keeps a live
	   Held thread from taking the continue path. */
	rcp_transport_full_stop();
	
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
	rcp_transport_start();
	rcpplayer_reset_last_timestamp();
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

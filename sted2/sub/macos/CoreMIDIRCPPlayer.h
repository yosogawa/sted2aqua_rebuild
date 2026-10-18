/*
 *  CoreMIDIRCPPlayer.h
 *  STed2_aqua
 *
 *  Created by Toshi Nagata on Sun Jun 08 2003.
 *  Copyright (c) 2003 Toshi Nagata. All rights reserved.
 *
 */

#ifndef __CoreMIDIRCPPlayer_H__
#define __CoreMIDIRCPPlayer_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void play_coremidi_rcpplayer(uint8_t *rcp_data_ptr, int data_length);
void stop_coremidi_rcpplayer(void);
void exit_coremidi_rcpplayer(void);

void CoreMIDIInitIfNeeded(void);
void CoreMIDISelectDeviceForPort(int index, int port);
const char *CoreMIDICanonicalOutputName(const char *name);
void CoreMIDISelectDeviceByNameForPort(const char *name, int port);
void CoreMIDISelectInputDeviceByName(const char *name);
void CoreMIDIAllSoundOff(void);

void push_midi_in_ring_buffer(uint8_t byte);
int pop_midi_in_ring_buffer(uint8_t *byte);
int has_midi_in_ring_buffer(void);
int send_midi_byte(uint8_t byte);
int send_midi_message(uint8_t *data, int length);

#ifdef __cplusplus
}
#endif

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
@interface CoreMIDIRCPPlayer : NSObject
{
	BOOL playing;
}
+ (CoreMIDIRCPPlayer *)currentPlayer;
- (BOOL)isPlaying;
@end

NSString *CoreMIDIDeviceNameAtIndex(int index);
NSString *CoreMIDIInDeviceNameAtIndex(int index);
#endif

#endif

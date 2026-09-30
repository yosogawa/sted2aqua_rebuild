/*
 *  rcp_player.h
 *  STed2_aqua
 *
 *  Created by Toshi Nagata on Tue Jun 10 2003.
 *  Copyright (c) 2003 Toshi Nagata. All rights reserved.
 *
 */

#ifndef __rcp_player_h__
#define __rcp_player_h__

#include <stdint.h>
#include <stddef.h>

#define kMaxMIDIMessageLength 256

extern double rcpplayer_step_to_microseconds(int32_t step);
extern int rcpplayer_next_midi_event(int32_t *step, uint8_t **ptr, int *length, int *port);
extern int rcpplayer_init(uint8_t *data_ptr, size_t data_length);

#endif

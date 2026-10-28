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

double rcpplayer_step_to_microseconds(int32_t step);
int32_t rcpplayer_microseconds_to_step(uint32_t micros);
int rcpplayer_next_midi_event(int32_t *step, uint8_t **ptr, int *length, int *port);
int rcpplayer_init(uint8_t *data_ptr, size_t data_length);
/* Re-init from the current image and advance to the first event at or
   after target_step. Clamps to the last event when the target is past
   the end (never leaves the parser on the million-step EOF sentinel).
   On success writes the landed step to *landed_step when non-NULL.
   Returns 0 on success, -1 if there is no image. */
int rcpplayer_seek_to_step(int32_t target_step, int32_t *landed_step);

#endif

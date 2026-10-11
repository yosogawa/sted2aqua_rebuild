/*
  midi_in.h

  prototype dec.s for functions midi port access

  Made by Studio Breeze. 1998

 */

#ifndef _MIDI_IN_H_
#define _MIDI_IN_H_

#ifdef __cplusplus
extern "C" {
#endif

int is_coremidi_playing(void);
void stop_coremidi_rcpplayer(void);
void set_realtime_recording(int on);


#if defined(__APPLE__)
int coremidi_connected_input_name(char *buf, int buflen);
#endif

int open_midi_port( char * );
void close_midi_port( void );
int get_midi_data( void );
int is_midi_in( void );
void put_midi_data( uint8_t );

#endif /* _MIDI_IN_H */


#ifdef __cplusplus
}
#endif

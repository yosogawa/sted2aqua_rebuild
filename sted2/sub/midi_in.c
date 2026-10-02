/* midi_in.c

   midi-port access functions

   Made by Studio Breeze. 1998

*/

#include "sted.h"

#if defined(__APPLE__)
#include "aqua/CoreMIDIRCPPlayer.h"
#endif

static int midi_dev = -1;
static int ismididevopened = 0;

int open_midi_port( char *dev_name ) {
#if defined(__APPLE__)
  return 0;
#else
  int ret=1;

  if ( ismididevopened!=0 ) return ret;

  ismididevopened=-1;
  midi_dev=-1;

  if ( dev_name[0] != 0 ) {

    midi_dev = open( dev_name, O_RDWR | O_NDELAY );
    if ( midi_dev != -1 ) {
      ismididevopened=1;
      ret = 0;
    }
  }

  return ret;
#endif
}

void close_midi_port( void ) {
#if defined(__APPLE__)
  return;
#else
  if ( ismididevopened > 0 ) {
    close(midi_dev);
  }

  return;
#endif
}

int get_midi_data( void ) {
#if defined(__APPLE__)
  CoreMIDIInitIfNeeded();
  uint8_t byte;
  if (pop_midi_in_ring_buffer(&byte)) {
    return (int)byte;
  }
  return -1;
#else
  int ret=-1;
  static fd_set x;
  static struct timeval tp;

  open_midi_port( midi_port_name );

  if ( ismididevopened > 0 ) {

    FD_ZERO(&x);
    FD_SET( midi_dev, &x );
    tp.tv_sec = 0;
    tp.tv_usec= 0;
    select( midi_dev+1, &x, (fd_set *)NULL, (fd_set *)NULL, &tp );

    if ( FD_ISSET( midi_dev, &x) ) {
      uint8_t buf[16];
      read( midi_dev, buf, 1);
      ret = (int)buf[0];
    }
  }

  return ret;
#endif
}

int is_midi_in( void ) {
#if defined(__APPLE__)
  CoreMIDIInitIfNeeded();
  return has_midi_in_ring_buffer();
#else
  int ret=0;
  static fd_set x;
  static struct timeval tp;

  open_midi_port( midi_port_name );

  if ( ismididevopened > 0 ) {

    FD_ZERO(&x);
    FD_SET( midi_dev, &x );
    tp.tv_sec = 0;
    tp.tv_usec= 0;
    select( FD_SETSIZE, &x, (fd_set *)NULL, (fd_set *)NULL, &tp );

    if ( FD_ISSET( midi_dev, &x) ) ret=1;
  }

  return ret;
#endif
}

void put_midi_data ( uint8_t data ) {
#if defined(__APPLE__)
  send_midi_byte(data);
#else
  if ( ismididevopened > 0 ) {
    write( midi_dev, &data, 1 );
  }

  return;
#endif
}

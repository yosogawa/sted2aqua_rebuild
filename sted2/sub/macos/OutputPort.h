#ifndef OUTPUT_PORT_H
#define OUTPUT_PORT_H

#include <CoreMIDI/CoreMIDI.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One sequencer port. An internal synth is handed to InternalSynthSchedule.
   Every other destination goes to CoreMIDISend. */
OSStatus OutputPortSend(uint8_t *ptr, int length, int port, MIDITimeStamp atTime);

#ifdef __cplusplus
}
#endif

#endif

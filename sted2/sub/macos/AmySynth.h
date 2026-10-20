#ifndef AMY_SYNTH_H
#define AMY_SYNTH_H

#include <stdint.h>

/* Matches AMY's compiled sample rate. The source node uses this so the
 * graph inserts a converter when the device rate differs.
 */
#define AMY_SYNTH_SAMPLE_RATE 44100

/* One process-wide AMY instance. amy_start and amy_stop are global. */

void AmySynthStart(void);
void AmySynthStop(void);

/* Real-time. left/right are separate planes, or one interleaved buffer
 * when stride is 2 and right == left + 1. hostTime is the play time of
 * the first frame, or 0 when the graph did not supply one.
 */
void AmySynthFill(float *left, float *right, int stride, int frames, uint64_t hostTime);

/* Complete MIDI message for the single AMY instance. atTime is a host time.
 * Sysex F0 00 03 45 <wire> F7 is handed to amy_send_wire_from_sysex.
 */
void AmySynthSendMIDI(const uint8_t *bytes, int length, uint64_t atTime);

#endif

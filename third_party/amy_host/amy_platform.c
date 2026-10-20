/* Stand-ins for the AMY desktop platform layer.
 * libminiaudio-audio.c and macos_midi.m stay out of this build.
 * amy_start() still calls amy_platform_init() and run_midi(). With
 * AMY_AUDIO_IS_NONE and AMY_MIDI_IS_NONE those calls do nothing.
 * Rendered audio leaves through amy_simple_fill_buffer().
 */
#include <stddef.h>
#include <stdint.h>

/* This file is compiled with the app, which does not pass -DGAMMA9001.
 * libamy.a does, and gamma9001_pcm.bin is linked into the app. Hand the
 * blob over unconditionally. amy.h only declares amy_set_gamma9001_pcm
 * when that flag is set, so declare the symbol here.
 */
extern const int16_t *gamma9001_pcm_data;
extern void amy_set_gamma9001_pcm(const int16_t *data);

void
amy_host_use_gamma9001_drums(void)
{
	amy_set_gamma9001_pcm(gamma9001_pcm_data);
}

void amy_platform_init(void) {}
void amy_platform_deinit(void) {}
void amy_update_tasks(void) {}

int16_t *amy_render_audio(void) {
    return NULL;
}

size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes) {
    (void)buffer;
    return nbytes;
}

void run_midi(void) {}
void stop_midi(void) {}

/* examples.c calls this. The example objects are in the archive;
 * nothing in STed2 calls them.
 */
void delay_ms(uint32_t ms) {
    (void)ms;
}

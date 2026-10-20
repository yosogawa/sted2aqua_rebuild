/* Gamma9001 drum banks other than the baked TR-808 ROM.
 * Frame layout matches third_party/amy/src/pcm_gamma9001.h.
 */
#include <stdint.h>

#pragma clang diagnostic ignored "-Wc23-extensions"

__attribute__((aligned(2)))
static const unsigned char gamma9001_pcm_bytes[] = {
#embed "gamma9001_pcm.bin"
};

const int16_t *gamma9001_pcm_data = (const int16_t *)gamma9001_pcm_bytes;

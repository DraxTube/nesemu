#ifndef PSP_AUDIO_H
#define PSP_AUDIO_H

#include <stdint.h>

void    psp_audio_init(void);
void    psp_audio_shutdown(void);
void    psp_audio_submit(const int16_t *buf, int samples);

#endif /* PSP_AUDIO_H */

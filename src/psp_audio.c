/*
 * PSP Audio output using sceAudio
 * Double-buffered ring queue at 44100Hz mono->stereo
 */
#include "psp_audio.h"
#include "apu.h"
#include <pspaudio.h>
#include <pspthreadman.h>
#include <string.h>
#include <stdlib.h>

#define AUDIO_CHANNEL    0
#define AUDIO_SAMPLES    APU_BUFFER_SIZE   /* samples per callback */

/* Double buffer: while one is playing, fill the other */
static int16_t audio_buf[2][APU_BUFFER_SIZE * 2]; /* stereo */
static int     buf_write_idx = 0;
static int     buf_read_idx  = 0;
static SceUID  audio_mutex;
static SceUID  audio_thread;
static int     audio_running = 0;

static int audio_thread_func(SceSize args, void *argp) {
    (void)args; (void)argp;
    while (audio_running) {
        int play_idx;
        sceKernelLockMutex(audio_mutex, 1, NULL);
        play_idx = buf_read_idx;
        sceKernelUnlockMutex(audio_mutex, 1);

        sceAudioOutputBlocking(AUDIO_CHANNEL, PSP_AUDIO_VOLUME_MAX,
                               audio_buf[play_idx]);
    }
    sceKernelExitThread(0);
    return 0;
}

void psp_audio_init(void) {
    memset(audio_buf, 0, sizeof(audio_buf));
    buf_write_idx = 0;
    buf_read_idx  = 0;
    audio_running = 1;

    sceAudioChReserve(AUDIO_CHANNEL, AUDIO_SAMPLES, PSP_AUDIO_FORMAT_STEREO);

    audio_mutex = sceKernelCreateMutex("audio_mutex", 0, 0, NULL);
    audio_thread = sceKernelCreateThread("audio_thread", audio_thread_func,
                                          0x11, 0x10000, 0, NULL);
    sceKernelStartThread(audio_thread, 0, NULL);
}

void psp_audio_shutdown(void) {
    audio_running = 0;
    sceKernelWaitThreadEnd(audio_thread, NULL);
    sceKernelDeleteThread(audio_thread);
    sceKernelDeleteMutex(audio_mutex);
    sceAudioChRelease(AUDIO_CHANNEL);
}

void psp_audio_submit(const int16_t *buf, int samples) {
    /* Convert mono to stereo in the write buffer */
    int16_t *dst = audio_buf[buf_write_idx];
    for (int i = 0; i < samples && i < AUDIO_SAMPLES; i++) {
        dst[i * 2 + 0] = buf[i];
        dst[i * 2 + 1] = buf[i];
    }
    /* Swap write/read */
    sceKernelLockMutex(audio_mutex, 1, NULL);
    buf_read_idx  = buf_write_idx;
    buf_write_idx = buf_write_idx ^ 1;
    sceKernelUnlockMutex(audio_mutex, 1);
}

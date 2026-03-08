/*
 * PSP Audio output using sceAudio
 * Double-buffered at 44100Hz mono->stereo
 * Uses semaphore instead of mutex (PSPSDK compatible)
 */
#include "psp_audio.h"
#include "apu.h"
#include <pspaudio.h>
#include <pspthreadman.h>
#include <string.h>

#define AUDIO_CHANNEL    0
#define AUDIO_SAMPLES    APU_BUFFER_SIZE

static int16_t audio_buf[2][APU_BUFFER_SIZE * 2]; /* stereo interleaved */
static volatile int buf_play_idx  = 0;
static volatile int buf_write_idx = 1;
static SceUID  audio_sema;
static SceUID  audio_thread_id;
static volatile int audio_running = 0;

static int audio_thread_func(SceSize args, void *argp) {
    (void)args; (void)argp;
    while (audio_running) {
        sceKernelWaitSema(audio_sema, 1, NULL);
        if (!audio_running) break;
        sceAudioOutputBlocking(AUDIO_CHANNEL, PSP_AUDIO_VOLUME_MAX,
                               audio_buf[buf_play_idx]);
    }
    sceKernelExitThread(0);
    return 0;
}

void psp_audio_init(void) {
    memset(audio_buf, 0, sizeof(audio_buf));
    buf_play_idx  = 0;
    buf_write_idx = 1;
    audio_running = 1;

    sceAudioChReserve(AUDIO_CHANNEL, AUDIO_SAMPLES, PSP_AUDIO_FORMAT_STEREO);

    audio_sema = sceKernelCreateSema("audio_sema", 0, 0, 1, NULL);
    audio_thread_id = sceKernelCreateThread("audio_thread", audio_thread_func,
                                             0x11, 0x10000, 0, NULL);
    sceKernelStartThread(audio_thread_id, 0, NULL);
}

void psp_audio_shutdown(void) {
    audio_running = 0;
    sceKernelSignalSema(audio_sema, 1);
    sceKernelWaitThreadEnd(audio_thread_id, NULL);
    sceKernelDeleteThread(audio_thread_id);
    sceKernelDeleteSema(audio_sema);
    sceAudioChRelease(AUDIO_CHANNEL);
}

void psp_audio_submit(const int16_t *buf, int samples) {
    int16_t *dst = audio_buf[buf_write_idx];
    int n = (samples < AUDIO_SAMPLES) ? samples : AUDIO_SAMPLES;
    for (int i = 0; i < n; i++) {
        dst[i * 2 + 0] = buf[i];
        dst[i * 2 + 1] = buf[i];
    }
    buf_play_idx  = buf_write_idx;
    buf_write_idx ^= 1;
    sceKernelSignalSema(audio_sema, 1);
}

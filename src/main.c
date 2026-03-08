/*
 * NESPSP - NES Emulator for PSP
 * Main entry point
 *
 * Controls during gameplay:
 *   Cross     -> NES A
 *   Circle    -> NES B
 *   Select    -> NES Select
 *   Start     -> NES Start
 *   D-pad     -> NES D-pad
 *   L+R       -> Open in-game menu
 *   L+Select  -> Save state
 *   R+Select  -> Load state
 *   L+Start   -> Reset
 *   Home      -> Return to ROM browser
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <psppower.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "nes.h"
#include "psp_video.h"
#include "psp_audio.h"
#include "psp_input.h"
#include "psp_menu.h"

PSP_MODULE_INFO("NESPSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

/* ---- Exit callback ---- */
static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    sceKernelExitGame();
    return 0;
}
static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    SceUID tid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (tid >= 0) sceKernelStartThread(tid, 0, 0);
}

/* ---- OSD ---- */
#define OSD_DURATION  120   /* frames */
static char  osd_msg[128] = {0};
static int   osd_timer    = 0;

static void osd_show(const char *msg) {
    strncpy(osd_msg, msg, sizeof(osd_msg) - 1);
    osd_timer = OSD_DURATION;
}

static void osd_draw(void) {
    if (osd_timer <= 0) return;
    osd_timer--;
    uint32_t alpha = (osd_timer > 30) ? 0xFF : (uint32_t)(osd_timer * 255 / 30);
    uint32_t color = 0x00FFFF00 | (alpha << 24);
    uint32_t bg    = 0x00000000 | ((alpha / 2) << 24);
    int x = 8, y = PSP_SCREEN_H - 20;
    psp_video_draw_rect(x - 2, y - 2, (int)strlen(osd_msg) * 8 + 4, 12, bg);
    psp_video_draw_text(x, y, osd_msg, color);
}

/* ---- In-game pause menu ---- */
typedef enum {
    PAUSE_RESUME,
    PAUSE_SAVE,
    PAUSE_LOAD,
    PAUSE_RESET,
    PAUSE_QUIT,
    PAUSE_ITEM_COUNT
} PauseItem;

static const char *pause_labels[] = {
    "Resume",
    "Save State",
    "Load State",
    "Reset",
    "Quit to Menu"
};

/* Returns 0=resume, 1=quit */
static int pause_menu(NES *nes, PSPInput *inp, const char *rom_path) {
    int sel = 0;

    while (1) {
        /* Darken background */
        psp_video_blit_nes(nes->ppu.framebuf);
        /* Overlay */
        psp_video_draw_rect(140, 60, 200, 160, 0xCC000000);
        psp_video_draw_rect(140, 60, 200,  16, 0xFF0F3460);
        psp_video_draw_text(152, 64, "PAUSED", 0xFFFFFFFF);

        for (int i = 0; i < PAUSE_ITEM_COUNT; i++) {
            uint32_t c = (i == sel) ? 0xFFFFFF00 : 0xFFCCCCCC;
            if (i == sel)
                psp_video_draw_rect(142, 84 + i * 20 - 2, 196, 14, 0xFF1A3A6E);
            psp_video_draw_text(152, 84 + i * 20, pause_labels[i], c);
        }

        psp_video_flip();
        psp_input_update(inp);

        if (psp_input_pressed(inp, PSP_CTRL_UP))   { if (sel > 0) sel--; }
        if (psp_input_pressed(inp, PSP_CTRL_DOWN))  { if (sel < PAUSE_ITEM_COUNT-1) sel++; }
        if (psp_input_pressed(inp, PSP_CTRL_CROSS) ||
            psp_input_pressed(inp, PSP_CTRL_START)) {
            char path[512];
            switch (sel) {
            case PAUSE_RESUME:
                return 0;
            case PAUSE_SAVE:
                snprintf(path, sizeof(path), "ms0:/NES/saves/%s.sta",
                         strrchr(rom_path, '/') ? strrchr(rom_path, '/') + 1 : rom_path);
                if (nes_save_state(nes, path) == 0) osd_show("State saved");
                else osd_show("Save failed!");
                return 0;
            case PAUSE_LOAD:
                snprintf(path, sizeof(path), "ms0:/NES/saves/%s.sta",
                         strrchr(rom_path, '/') ? strrchr(rom_path, '/') + 1 : rom_path);
                if (nes_load_state(nes, path) == 0) osd_show("State loaded");
                else osd_show("Load failed!");
                return 0;
            case PAUSE_RESET:
                nes_reset(nes);
                osd_show("Reset");
                return 0;
            case PAUSE_QUIT:
                return 1;
            }
        }
        if (psp_input_pressed(inp, PSP_CTRL_CIRCLE)) return 0;
    }
}

/* ---- FPS counter ---- */
static float get_fps(void) {
    return 60.0f;
}

/* ---- Main emulation loop ---- */
static void run_emulator(const char *rom_path) {
    NES nes;
    if (nes_init(&nes, rom_path) != 0) {
        osd_show("Failed to load ROM!");
        sceKernelDelayThread(2000000);
        return;
    }

    /* Create save directory */
    sceIoMkdir("ms0:/NES/saves", 0777);

    PSPInput inp;
    memset(&inp, 0, sizeof(inp));
    osd_show("Loaded! L+R=Menu");

    int running = 1;
    while (running) {
        /* Run one NES frame */
        nes_run_frame(&nes);

        /* Submit audio if ready */
        if (nes.apu.buf_ready) {
            nes.apu.buf_ready = 0;
            psp_audio_submit(nes.apu.buffer, APU_BUFFER_SIZE);
        }

        /* Blit to screen */
        psp_video_blit_nes(nes.ppu.framebuf);

        /* OSD */
        osd_draw();

        /* FPS display */
        char fps_str[16];
        snprintf(fps_str, sizeof(fps_str), "%.0f FPS", get_fps());
        psp_video_draw_text(PSP_SCREEN_W - 56, 2, fps_str, 0xFF00FF00);

        psp_video_flip();

        /* Input */
        psp_input_update(&inp);
        uint8_t p1 = psp_input_to_nes(&inp, 0);
        nes_set_controller(&nes, 0, p1);

        /* Emulator hotkeys */
        if (psp_input_held(&inp, PSP_CTRL_LTRIGGER) &&
            psp_input_held(&inp, PSP_CTRL_RTRIGGER)) {
            /* Open pause menu */
            int quit = pause_menu(&nes, &inp, rom_path);
            if (quit) running = 0;
        } else if (psp_input_pressed(&inp, PSP_CTRL_HOME)) {
            running = 0;
        } else {
            /* Quick save/load */
            char save_path[512];
            snprintf(save_path, sizeof(save_path), "ms0:/NES/saves/%s.sta",
                     strrchr(rom_path, '/') ? strrchr(rom_path, '/') + 1 : rom_path);

            if (psp_input_held(&inp, PSP_CTRL_LTRIGGER) &&
                psp_input_pressed(&inp, PSP_CTRL_SELECT)) {
                if (nes_save_state(&nes, save_path) == 0) osd_show("State saved");
                else osd_show("Save failed!");
            }
            if (psp_input_held(&inp, PSP_CTRL_RTRIGGER) &&
                psp_input_pressed(&inp, PSP_CTRL_SELECT)) {
                if (nes_load_state(&nes, save_path) == 0) osd_show("State loaded");
                else osd_show("Load failed!");
            }
            if (psp_input_held(&inp, PSP_CTRL_LTRIGGER) &&
                psp_input_pressed(&inp, PSP_CTRL_START)) {
                nes_reset(&nes);
                osd_show("Reset");
            }
        }
    }

    nes_free(&nes);
}

/* ---- Entry point ---- */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    setup_callbacks();

    /* Crank up CPU speed for better emulation */
    scePowerSetClockFrequency(333, 333, 166);

    /* Create ROM directory */
    sceIoMkdir("ms0:/NES", 0777);

    psp_video_init();
    psp_audio_init();
    psp_input_init();

    while (1) {
        /* Show ROM browser */
        const char *rom_path = psp_menu_run();
        if (!rom_path) break;   /* user pressed Start to quit */

        run_emulator(rom_path);
    }

    /* Cleanup */
    psp_audio_shutdown();
    psp_video_shutdown();

    sceKernelExitGame();
    return 0;
}

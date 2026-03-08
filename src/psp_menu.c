/*
 * PSP ROM file browser
 * Scans ms0:/NES/ for .nes files
 */
#include "psp_menu.h"
#include "psp_video.h"
#include "psp_input.h"
#include <pspiofilemgr.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define ROM_PATH     "ms0:/NES/"
#define MAX_FILES    256
#define MAX_FILENAME 256
#define VISIBLE_ROWS 20

static char file_list[MAX_FILES][MAX_FILENAME];
static int  file_count = 0;
static char selected_path[512];

static int scan_roms(void) {
    file_count = 0;
    SceUID dir = sceIoDopen(ROM_PATH);
    if (dir < 0) {
        /* Try alternate paths */
        dir = sceIoDopen("ms0:/PSP/NES/");
        if (dir < 0) dir = sceIoDopen("ms0:/");
        if (dir < 0) return 0;
    }

    SceIoDirent entry;
    while (sceIoDread(dir, &entry) > 0 && file_count < MAX_FILES) {
        if (entry.d_stat.st_attr & FIO_SO_IFDIR) continue;
        const char *name = entry.d_name;
        size_t len = strlen(name);
        if (len > 4 && (strcasecmp(name + len - 4, ".nes") == 0)) {
            strncpy(file_list[file_count], name, MAX_FILENAME - 1);
            file_list[file_count][MAX_FILENAME - 1] = '\0';
            file_count++;
        }
    }
    sceIoDclose(dir);

    /* Simple bubble sort */
    for (int i = 0; i < file_count - 1; i++) {
        for (int j = i + 1; j < file_count; j++) {
            if (strcasecmp(file_list[i], file_list[j]) > 0) {
                char tmp[MAX_FILENAME];
                strcpy(tmp, file_list[i]);
                strcpy(file_list[i], file_list[j]);
                strcpy(file_list[j], tmp);
            }
        }
    }
    return file_count;
}

const char *psp_menu_run(void) {
    PSPInput inp;
    memset(&inp, 0, sizeof(inp));

    int sel      = 0;
    int scroll   = 0;
    int rescan   = 1;

    while (1) {
        if (rescan) {
            scan_roms();
            rescan = 0;
            sel    = 0;
            scroll = 0;
        }

        /* Draw */
        psp_video_clear(0xFF1A1A2E);

        /* Title bar */
        psp_video_draw_rect(0, 0, 480, 16, 0xFF16213E);
        psp_video_draw_text(4, 4, "NESPSP - ROM Browser", 0xFFFFFFFF);
        psp_video_draw_text(4, 4 + 8, ROM_PATH, 0xFFAAAAAA);

        if (file_count == 0) {
            psp_video_draw_text(20, 80, "No .nes files found in:", 0xFFFF6666);
            psp_video_draw_text(20, 96, ROM_PATH, 0xFFFFAAAA);
            psp_video_draw_text(20, 120, "Please copy ROM files and press X to rescan", 0xFFAAAAFF);
        } else {
            /* File list */
            for (int i = 0; i < VISIBLE_ROWS && (i + scroll) < file_count; i++) {
                int idx = i + scroll;
                int y   = 24 + i * 12;
                uint32_t bg    = (idx == sel) ? 0xFF0F3460 : 0x00000000;
                uint32_t color = (idx == sel) ? 0xFFFFFFFF : 0xFFCCCCCC;

                if (idx == sel)
                    psp_video_draw_rect(0, y - 1, 480, 12, bg);

                char line[MAX_FILENAME + 8];
                if (idx == sel)
                    snprintf(line, sizeof(line), "> %s", file_list[idx]);
                else
                    snprintf(line, sizeof(line), "  %s", file_list[idx]);

                psp_video_draw_text(4, y, line, color);
            }

            /* Scrollbar */
            if (file_count > VISIBLE_ROWS) {
                int bar_h = PSP_SCREEN_H - 48;
                int pos   = (scroll * bar_h) / file_count;
                int sz    = (VISIBLE_ROWS * bar_h) / file_count;
                psp_video_draw_rect(474, 24, 4, bar_h, 0xFF333333);
                psp_video_draw_rect(474, 24 + pos, 4, sz, 0xFF0F3460);
            }
        }

        /* Bottom status bar */
        psp_video_draw_rect(0, 256, 480, 16, 0xFF16213E);
        char status[128];
        snprintf(status, sizeof(status), "%d ROMs  |  X:Load  O:Rescan  Start:Exit", file_count);
        psp_video_draw_text(4, 258, status, 0xFFAAAAAA);

        psp_video_flip();

        /* Input */
        psp_input_update(&inp);

        if (psp_input_pressed(&inp, PSP_CTRL_UP)) {
            if (sel > 0) {
                sel--;
                if (sel < scroll) scroll = sel;
            }
        }
        if (psp_input_pressed(&inp, PSP_CTRL_DOWN)) {
            if (sel < file_count - 1) {
                sel++;
                if (sel >= scroll + VISIBLE_ROWS) scroll = sel - VISIBLE_ROWS + 1;
            }
        }
        if (psp_input_pressed(&inp, PSP_CTRL_LTRIGGER)) {
            sel    = (sel > VISIBLE_ROWS) ? sel - VISIBLE_ROWS : 0;
            scroll = (scroll > VISIBLE_ROWS) ? scroll - VISIBLE_ROWS : 0;
        }
        if (psp_input_pressed(&inp, PSP_CTRL_RTRIGGER)) {
            sel    = (sel + VISIBLE_ROWS < file_count) ? sel + VISIBLE_ROWS : file_count - 1;
            scroll = sel - VISIBLE_ROWS + 1;
            if (scroll < 0) scroll = 0;
        }
        if (psp_input_pressed(&inp, PSP_CTRL_CROSS)) {
            if (file_count > 0) {
                snprintf(selected_path, sizeof(selected_path), "%s%s",
                         ROM_PATH, file_list[sel]);
                return selected_path;
            }
        }
        if (psp_input_pressed(&inp, PSP_CTRL_CIRCLE)) {
            rescan = 1;
        }
        if (psp_input_pressed(&inp, PSP_CTRL_START)) {
            return NULL;
        }
    }
}

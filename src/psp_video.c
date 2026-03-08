/*
 * PSP Video output via GU (Graphics Unit)
 */
#include "psp_video.h"
#include <pspgu.h>
#include <pspdisplay.h>
#include <pspgum.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* 4MB VRAM layout:
 *   0x00000000 : draw buffer  (PSP_BUF_W*PSP_SCREEN_H*4)
 *   0x00088000 : display buffer
 *   0x00110000 : depth buffer
 *   0x00198000 : NES texture
 */
#define VRAM_BASE       0x04000000
#define DRAW_BUF_ADDR   0x00000000
#define DISP_BUF_ADDR   0x00088000
#define DEPTH_BUF_ADDR  0x00110000
#define TEX_ADDR        0x00198000

#define BUF_BYTES       (PSP_BUF_W * PSP_SCREEN_H * 4)

static unsigned int __attribute__((aligned(16))) disp_list[0x40000];
static int buf_idx = 0;

typedef struct {
    float u, v;
    float x, y, z;
} Vertex;

static Vertex __attribute__((aligned(16))) verts[2];

/* Embedded 8x8 font (ASCII 32-127), 1 bit per pixel, row-major */
#include "font_data.h"

static uint32_t *draw_buf(void) {
    return (uint32_t *)(VRAM_BASE + (buf_idx ? DISP_BUF_ADDR : DRAW_BUF_ADDR));
}
static uint32_t *disp_buf(void) {
    return (uint32_t *)(VRAM_BASE + (buf_idx ? DRAW_BUF_ADDR : DISP_BUF_ADDR));
}

void psp_video_init(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, disp_list);

    sceGuDrawBuffer(GU_PSM_8888, (void *)DRAW_BUF_ADDR, PSP_BUF_W);
    sceGuDispBuffer(PSP_SCREEN_W, PSP_SCREEN_H, (void *)DISP_BUF_ADDR, PSP_BUF_W);
    sceGuDepthBuffer((void *)DEPTH_BUF_ADDR, PSP_BUF_W);
    sceGuOffset(2048 - PSP_SCREEN_W/2, 2048 - PSP_SCREEN_H/2);
    sceGuViewport(2048, 2048, PSP_SCREEN_W, PSP_SCREEN_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, PSP_SCREEN_W, PSP_SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void psp_video_shutdown(void) {
    sceGuTerm();
}

void psp_video_flip(void) {
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    buf_idx ^= 1;
    sceGuStart(GU_DIRECT, disp_list);
}

void psp_video_clear(uint32_t color) {
    sceGuClearColor(color);
    sceGuClear(GU_COLOR_BUFFER_BIT);
}

void psp_video_blit_nes(const uint32_t *framebuf) {
    /* Copy NES framebuffer into VRAM texture area */
    uint32_t *tex = (uint32_t *)(VRAM_BASE + TEX_ADDR);

    /* NES is 256x240, texture stride must be power-of-2 (256) */
    for (int y = 0; y < 240; y++) {
        const uint32_t *src = framebuf + y * 256;
        uint32_t *dst = tex + y * 256;
        memcpy(dst, src, 256 * 4);
    }

    sceKernelDcacheWritebackAll();

    sceGuTexImage(0, 256, 256, 256, (void *)TEX_ADDR);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);

    verts[0].u = 0.0f;
    verts[0].v = 0.0f;
    verts[0].x = (float)NES_DISP_X;
    verts[0].y = (float)NES_DISP_Y;
    verts[0].z = 0.0f;

    verts[1].u = 256.0f;
    verts[1].v = 240.0f;
    verts[1].x = (float)(NES_DISP_X + NES_DISP_W);
    verts[1].y = (float)(NES_DISP_Y + NES_DISP_H);
    verts[1].z = 0.0f;

    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, verts);
}

uint32_t *psp_video_get_vram(void) {
    return draw_buf();
}

void psp_video_draw_rect(int x, int y, int w, int h, uint32_t color) {
    uint32_t *buf = draw_buf();
    for (int row = y; row < y + h && row < PSP_SCREEN_H; row++) {
        for (int col = x; col < x + w && col < PSP_SCREEN_W; col++) {
            buf[row * PSP_BUF_W + col] = color;
        }
    }
}

void psp_video_draw_text(int x, int y, const char *text, uint32_t color) {
    uint32_t *buf = draw_buf();
    int cx = x;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c < 32 || c > 127) c = '?';
        int glyph = c - 32;
        const uint8_t *bmp = font_data + glyph * 8;
        for (int row = 0; row < 8; row++) {
            uint8_t bits = bmp[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    int px = cx + col;
                    int py = y + row;
                    if (px >= 0 && px < PSP_SCREEN_W && py >= 0 && py < PSP_SCREEN_H)
                        buf[py * PSP_BUF_W + px] = color;
                }
            }
        }
        cx += 8;
    }
}

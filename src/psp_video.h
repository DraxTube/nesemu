#ifndef PSP_VIDEO_H
#define PSP_VIDEO_H

#include <stdint.h>

#define PSP_SCREEN_W  480
#define PSP_SCREEN_H  272
#define PSP_BUF_W     512   /* must be power of 2 */

/* NES output region on PSP screen */
#define NES_DISP_W    256
#define NES_DISP_H    240
/* Centered position */
#define NES_DISP_X    ((PSP_SCREEN_W - NES_DISP_W) / 2)
#define NES_DISP_Y    ((PSP_SCREEN_H - NES_DISP_H) / 2)

void psp_video_init(void);
void psp_video_shutdown(void);
void psp_video_flip(void);
void psp_video_blit_nes(const uint32_t *framebuf);
void psp_video_clear(uint32_t color);

/* Draw text for OSD/menu */
void psp_video_draw_text(int x, int y, const char *text, uint32_t color);
void psp_video_draw_rect(int x, int y, int w, int h, uint32_t color);

/* Returns current draw buffer as 32-bit pointer */
uint32_t *psp_video_get_vram(void);

#endif /* PSP_VIDEO_H */

/**
 * fxtk_font.c — 文本绘制 (中英混排, 从 esp32-tester ui_renderer 移植)
 *
 * 字库:
 *   - cn_gray: 16px 微软雅黑灰度 (2bit/像素, 4 级抗锯齿, unicode 二分查找)
 *   - ascii_gray16: Arial 16px 灰度 (比例字形, adv 前进量)
 * 混排规则: ASCII 按 adv 前进; 中文 17px 前进, 顶部对齐微调 1px;
 * 像素出口走 fxtk_put_px (支持裁剪与 band 帧缓冲)。
 */
#include "fxtk.h"
#include "cn_gray.h"
#include "ascii_gray16.h"

void fxtk_put_px(int x, int y, uint16_t c);   /* fxtk_draw.c */

#define CN_ADV 17        /* 中文前进量 */
#define CN_ALIGN_DY 1    /* 中英混排垂直对齐修正 (实测中文格顶下移 1px 对齐) */
#define LINE_H 18        /* 换行行高 */

/* RGB565 分量 Alpha 混合: (fg*a + bg*(255-a)) >> 8 */
static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a)
{
    int fr = (fg >> 11) & 31, fgc = (fg >> 5) & 63, fb = fg & 31;
    int br = (bg >> 11) & 31, bgc = (bg >> 5) & 63, bb = bg & 31;
    fr = (fr * a + br * (255 - a)) >> 8;
    fgc = (fgc * a + bgc * (255 - a)) >> 8;
    fb = (fb * a + bb * (255 - a)) >> 8;
    return (uint16_t)((fr << 11) | (fgc << 5) | fb);
}

static const uint16_t cn_a4[4] = { 0, 85, 170, 255 };

/* unicode 二分查找中文字形 */
static int cn_gray_find(uint16_t code, const cn_gray_glyph_t **out)
{
    int lo = 0, hi = (int)cn_gray_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t c = cn_gray[mid].code;
        if (c == code) { *out = &cn_gray[mid]; return 1; }
        else if (c < code) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

/* 灰度中文: 未收录返回 0 */
static int cn_gray_draw(int x, int y, uint16_t code, uint16_t fg, uint16_t bg)
{
    const cn_gray_glyph_t *g;
    if (!cn_gray_find(code, &g) || !g->w || !g->h)
        return 0;
    for (int row = 0; row < g->h; row++)
        for (int col = 0; col < g->w; col++) {
            uint32_t pix = g->pixoff + (uint32_t)row * g->w + col;
            uint8_t v = (cn_gray_data[pix >> 2] >> ((pix & 3) << 1)) & 3;
            fxtk_put_px(x + g->xoff + col, y + g->yoff + row,
                        blend565(fg, bg, cn_a4[v]));
        }
    return 1;
}

/* ASCII 灰度字形 */
static void draw_gray_char(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    if (c < 0x20 || c > 0x7E)
        return;
    const gray_glyph_t *g = &ascii_gray16[c - 0x20];
    if (!g->w || !g->h)
        return;
    int baseline = y + ascii_gray16_ascent;
    for (int row = 0; row < g->h; row++)
        for (int col = 0; col < g->w; col++) {
            uint32_t pix = (uint32_t)row * g->w + col;
            uint8_t v = (g->px[pix >> 2] >> ((pix & 3) << 1)) & 3;
            fxtk_put_px(x + g->xoff + col, baseline + g->yoff + row,
                        blend565(fg, bg, cn_a4[v]));
        }
}

/* UTF-8 解码: 返回码点, *plen=字节数 */
static uint32_t utf8_decode(const char *s, int *plen)
{
    uint8_t c = (uint8_t)*s;
    if (c < 0x80) { *plen = 1; return c; }
    if ((c & 0xE0) == 0xC0 && ((uint8_t)s[1] & 0xC0) == 0x80) {
        *plen = 2;
        return ((c & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && ((uint8_t)s[1] & 0xC0) == 0x80 &&
        ((uint8_t)s[2] & 0xC0) == 0x80) {
        *plen = 3;
        return ((c & 0x0F) << 12) | (((uint8_t)s[1] & 0x3F) << 6) |
               ((uint8_t)s[2] & 0x3F);
    }
    *plen = 1;
    return c;
}

/* 混排文本, \n 换行 */
void fx_draw_text_c(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    int cx = x;
    while (*s) {
        uint8_t c = *s;
        if (c < 0x80) {
            if (c == '\n') { cx = x; y += LINE_H; s++; continue; }
            draw_gray_char(cx, y, (char)c, fg, bg);
            cx += ascii_gray16[c - 0x20].adv;
            s++;
        } else if ((c & 0xE0) == 0xC0) {
            uint16_t code = ((c & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F);
            if (!cn_gray_draw(cx, y - CN_ALIGN_DY, code, fg, bg))
                fx_draw_rect(cx, y, cx + 15, y + 15);  /* 缺字方框 */
            cx += CN_ADV;
            s += 2;
        } else if ((c & 0xF0) == 0xE0) {
            uint16_t code = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) |
                            (s[2] & 0x3F);
            if (!cn_gray_draw(cx, y - CN_ALIGN_DY, code, fg, bg))
                fx_draw_rect(cx, y, cx + 15, y + 15);
            cx += CN_ADV;
            s += 3;
        } else {
            s++;
        }
    }
}

void fx_draw_text(int x, int y, const char *s)
{
    fx_draw_text_c(x, y, s, FX_WHITE, FX_BLACK);
}

int fx_text_width(const char *s)
{
    int w = 0;
    while (*s) {
        uint8_t c = *s;
        if (c < 0x80) {
            if (c != '\n') w += ascii_gray16[c - 0x20].adv;
            s++;
        } else {
            int len;
            utf8_decode(s, &len);
            w += CN_ADV;
            s += len;
        }
    }
    return w;
}

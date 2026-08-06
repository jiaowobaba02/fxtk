/**
 * fxtk_widgets.c — 控件绘制实现 (按钮/文本/网格/画布/滑条/进度条/复选框/面板)
 *
 * 注意: 控件绘制**不自己设置 clip** — 由 fxtk.c 的 draw_widget 统一设置
 * (支持脏区局部重绘时 clip = 控件矩形 ∩ 脏区)。canvas 回调除外
 * (fx_canvas_begin 内部设置原点 + 相对 clip)。
 */
#include "fxtk_internal.h"
#include <string.h>

/* 按下态颜色加深 (RGB565 分量减半) */
static fx_color_t darken(fx_color_t c)
{
    return (fx_color_t)((((c >> 11) & 31) / 2 << 11) |
                        (((c >> 5) & 63) / 2 << 5) |
                        ((c & 31) / 2));
}

/* ---------- 按钮 ---------- */
void fxtk_draw_button(fx_widget_t *w)
{
    int pressed = (w->flags & FX_F_PRESSED) != 0;
    fx_color_t bg = pressed ? darken(w->bg) : w->bg;
    fx_set_color(bg);                              /* ⚠️ 忘记设色 = 背景黑 */
    if (w->radius > 0)
        fx_fill_rect_round(w->x1, w->y1, w->x2, w->y2, w->radius);
    else
        fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    /* 边框 */
    if (w->border > 0) {
        fx_color_t bd = darken(bg);
        fx_set_color(bd);
        if (w->radius > 0)
            fx_draw_rect_round(w->x1, w->y1, w->x2, w->y2, w->radius);
        else
            fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
    }
    /* 标题居中 */
    if (w->title[0]) {
        int tw = fx_text_width(w->title);
        int tx = (w->x1 + w->x2 - tw) / 2;
        int ty = (w->y1 + w->y2 - 16) / 2;
        fx_draw_text_c(tx, ty, w->title, w->fg, bg);
    }
}

/* ---------- 文本标签 ---------- */
void fxtk_draw_label(fx_widget_t *w)
{
    if (!w->title[0])
        return;
    if (w->bg != FX_BLACK) {                   /* 默认透明; 指定 color() 填充 */
        fx_set_color(w->bg);
        fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    }
    fx_draw_text_c(w->x1, w->y1, w->title, w->fg, FX_BLACK);
}

/* ---------- 网格容器 ---------- */
void fxtk_draw_gird(fx_widget_t *w)
{
    fx_set_color(w->bg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    /* 淡网格线 */
    if (w->lines > 0 && w->rows > 0) {
        fx_set_color(darken(w->bg));
        int cellw = (w->x2 - w->x1 + 1) / w->rows;
        int cellh = (w->y2 - w->y1 + 1) / w->lines;
        for (int r = 1; r < w->lines; r++)
            fx_draw_hline(w->x1, w->x2, w->y1 + r * cellh);
        for (int c = 1; c < w->rows; c++)
            fx_draw_vline(w->x1 + c * cellw, w->y1, w->y2);
    }
    /* 边框 */
    if (w->border > 0) {
        fx_set_color(darken(w->bg));
        fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
    }
}

/* ---------- 画布 ---------- */
void fxtk_draw_canvas(fx_widget_t *w)
{
    fx_set_color(w->bg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    if (w->border > 0) {
        fx_set_color(darken(w->bg));
        fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
    }
}

/* ---------- 滑条 ---------- */
void fxtk_draw_slider(fx_widget_t *w)
{
    int h = w->y2 - w->y1 + 1;
    /* 铺满整矩形 (轨道色): 滑块移动无残留 */
    fx_set_color(w->fg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    int track_y = w->y1 + h / 2 - 2;
    /* 轨道 */
    fx_set_color(w->fg);
    fx_fill_rect(w->x1, track_y, w->x2, track_y + 3);
    /* 已滑部分高亮 */
    int rw = w->x2 - w->x1 + 1;
    int filled = rw * w->value / 100;
    fx_set_color(w->bg);
    fx_fill_rect(w->x1, track_y, w->x1 + filled - 1, track_y + 3);
    /* 滑块 */
    int kx = w->x1 + filled - 3;
    if (kx < w->x1) kx = w->x1;
    if (kx > w->x2 - 6) kx = w->x2 - 6;
    fx_set_color((w->flags & FX_F_PRESSED) ? darken(w->bg) : w->bg);
    fx_fill_rect(kx, w->y1, kx + 6, w->y2);
}

/* ---------- 进度条 ---------- */
void fxtk_draw_progress(fx_widget_t *w)
{
    fx_set_color(w->fg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);          /* 轨道 */
    int rw = w->x2 - w->x1 + 1;
    int filled = rw * w->value / 100;
    if (filled > 0) {
        fx_set_color(w->bg);
        fx_fill_rect(w->x1, w->y1, w->x1 + filled - 1, w->y2);
    }
    if (w->border > 0) {
        fx_set_color(darken(w->fg));
        fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
    }
}

/* ---------- 复选框 ---------- */
void fxtk_draw_checkbox(fx_widget_t *w)
{
    fx_set_color(w->bg);                     /* 铺底 (默认黑): 覆盖型无残留 */
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    int box = w->y2 - w->y1 + 1;
    if (box > 20) box = 20;
    fx_set_color(w->fg);
    fx_draw_rect(w->x1, w->y1, w->x1 + box - 1, w->y1 + box - 1);
    if (w->value) {
        /* 勾 */
        fx_draw_line(w->x1 + 3, w->y1 + box / 2,
                     w->x1 + box / 2 - 1, w->y1 + box - 4);
        fx_draw_line(w->x1 + box / 2 - 1, w->y1 + box - 4,
                     w->x1 + box - 4, w->y1 + 2);
    }
    if (w->title[0])
        fx_draw_text_c(w->x1 + box + 6, w->y1, w->title, w->fg, FX_BLACK);
}

/* ---------- 面板 (容器) ---------- */
void fxtk_draw_panel(fx_widget_t *w)
{
    fx_set_color(w->bg);
    fx_fill_rect(w->x1, w->y1, w->x2, w->y2);
    if (w->border > 0) {
        fx_set_color(darken(w->bg));
        fx_draw_rect(w->x1, w->y1, w->x2, w->y2);
    }
}

/* ---------- 标签页容器 ---------- */
void fxtk_draw_tab(fx_widget_t *w)
{
    /* 内容区底色 */
    fx_set_color(w->bg);
    fx_fill_rect(w->x1, w->y1 + FX_TAB_H, w->x2, w->y2);
    /* 标签行: 页名 = title 逗号分隔 */
    int n = w->lines > 0 ? w->lines : 1;
    int tw = (w->x2 - w->x1 + 1) / n;
    const char *p = w->title;
    for (int i = 0; i < n && p[0]; i++) {
        int tx1 = w->x1 + i * tw;
        int tx2 = (i == n - 1) ? w->x2 : tx1 + tw - 1;
        int sel = (i == w->value);
        fx_color_t bg = sel ? 0x5D7C : darken(w->bg);
        fx_set_color(bg);
        fx_fill_rect(tx1, w->y1, tx2, w->y1 + FX_TAB_H - 1);
        /* 页名段 */
        const char *comma = strchr(p, ',');
        char seg[24];
        int len = comma ? (int)(comma - p) : (int)strlen(p);
        if (len > 23) len = 23;
        memcpy(seg, p, (size_t)len);
        seg[len] = 0;
        int sw = fx_text_width(seg);
        fx_draw_text_c(tx1 + (tw - sw) / 2, w->y1 + (FX_TAB_H - 16) / 2,
                       seg, sel ? FX_WHITE : FX_LGRAY, bg);
        p = comma ? comma + 1 : p + strlen(p);
    }
    /* 标签行下分隔线 */
    fx_set_color(darken(w->bg));
    fx_draw_hline(w->x1, w->x2, w->y1 + FX_TAB_H);
}
/**
 * fxtk_draw.c — 矢量绘制 + 渲染管线 (fxtk 核心)
 *
 * 像素出口统一走 fxtk_put_px():
 *   - 矩形填充: 驱动级单色直写 (fill_rect, 1 次窗口 + 分块传输, 最快)
 *   - 逐像素图元 (线/圆/文字): 行缓冲 + 脏区间局部刷新,
 *     同一行多次绘制互不覆盖 (只刷受影响区间)
 * 整帧重绘 = 清屏 (驱动直写) + 各控件顺序绘制, 直接写屏, 简单可靠。
 * (无撕裂整帧需要全屏双缓冲, ESP32 无 PSRAM 放不下 261KB, 故放弃 vsync 方案)
 *
 * 所有绘制先过 clip 矩形, 控件/画布绘制天然支持裁剪。
 */
#include "fxtk.h"
#include "fxtk_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------- 内部状态 ---------- */
static const fx_driver_t *s_drv;
static fx_color_t s_color = FX_WHITE;
static int16_t s_clip_x1 = 0, s_clip_y1 = 0, s_clip_x2 = 32767, s_clip_y2 = 32767;

#define FX_MAX_W 512
static int s_ox = 0, s_oy = 0;                 /* 绘制原点偏移 (canvas 相对坐标) */
static int s_framing = 0;                      /* 手动帧模式标志 */

static uint16_t s_line[FX_MAX_W];              /* 行缓冲 (逐像素图元) */
static int s_line_y = -1;
static int s_line_x0 = 0, s_line_x1 = 0;       /* 行内脏区间 */

/* 画布离屏缓冲 (fx_canvas_enable_buf): 内容画到内存, 整帧一次刷出 → 无闪烁 */
static uint16_t *s_offbuf = NULL;
static int s_offing = 0;                       /* 当前处于离屏渲染 */
static int s_offw = 0, s_offh = 0;

/* 兼容保留: 旧 band 方案已移除, 恒返回 -1
 * (demo 动画推进条件 fx_band_index() <= 0 恒真 → 每帧推进一次) */
int fx_band_index(void)
{
    return -1;
}

/* 行缓冲刷新: 只刷脏区间 [s_line_x0, s_line_x1] */
static void flush_line(void)
{
    if (s_line_y < 0)
        return;
    s_drv->set_window((uint16_t)s_line_x0, (uint16_t)s_line_y,
                      (uint16_t)s_line_x1, (uint16_t)s_line_y);
    s_drv->push_pixels(&s_line[s_line_x0], (uint32_t)(s_line_x1 - s_line_x0 + 1));
    s_line_y = -1;
}

/* ---------- 像素出口 ---------- */
void fxtk_put_px(int x, int y, uint16_t c)
{
    if (x < s_clip_x1 || x > s_clip_x2 || y < s_clip_y1 || y > s_clip_y2)
        return;
    x += s_ox; y += s_oy;
    if (s_offing) {                            /* 离屏: 写入画布缓冲 */
        if (x < 0 || y < 0 || x >= s_offw || y >= s_offh)
            return;
        s_offbuf[y * s_offw + x] = c;
        return;
    }
    if (x < 0 || y < 0 || x >= s_drv->width || y >= s_drv->height)
        return;
    /* 行缓冲 + 脏区间 */
    if (y != s_line_y) {
        flush_line();
        s_line_y = y;
        s_line_x0 = s_line_x1 = x;
    } else {
        if (x < s_line_x0) s_line_x0 = x;
        if (x > s_line_x1) s_line_x1 = x;
    }
    s_line[x] = c;
}

/* ---------- 内部库接口 (fxtk.c 调用) ---------- */
void fxtk_draw_set_driver(const fx_driver_t *drv) { s_drv = drv; }

void fxtk_draw_flush_all(void)
{
    flush_line();
}

void fx_frame_begin(void)
{
    if (!s_drv) return;
    s_framing = 1;
}

void fx_frame_end(void)
{
    if (!s_drv) return;
    flush_line();
    s_framing = 0;
}

void fx_set_color(fx_color_t c) { s_color = c; }

void fx_set_clip(int x1, int y1, int x2, int y2)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    s_clip_x1 = (int16_t)x1; s_clip_y1 = (int16_t)y1;
    s_clip_x2 = (int16_t)x2; s_clip_y2 = (int16_t)y2;
}

void fx_reset_clip(void)
{
    s_clip_x1 = 0; s_clip_y1 = 0;
    s_clip_x2 = 32767; s_clip_y2 = 32767;
}

/* canvas 绘制上下文: 原点移到画布左上角, clip 设为画布 (相对坐标) */
void fx_canvas_begin(fx_widget_t *cv)
{
    if (!cv) return;
    if (s_offing) {                            /* 离屏: 相对坐标直写缓冲 */
        s_ox = 0; s_oy = 0;
        fx_set_clip(0, 0, s_offw - 1, s_offh - 1);
        return;
    }
    s_ox = cv->x1; s_oy = cv->y1;
    fx_set_clip(0, 0, cv->x2 - cv->x1, cv->y2 - cv->y1);
}

void fx_canvas_end(void)
{
    s_ox = 0; s_oy = 0;
    fx_reset_clip();
}

/* 离屏渲染: 目标切到画布缓冲 (fxtk.c 画布绘制时调用) */
void fxtk_off_begin(fx_widget_t *cv)
{
    if (!s_offbuf) return;
    s_offing = 1;
    s_offw = cv->x2 - cv->x1 + 1;
    s_offh = cv->y2 - cv->y1 + 1;
    s_ox = -cv->x1; s_oy = -cv->y1;            /* 绝对坐标 → 缓冲相对 */
    fx_set_clip(0, 0, s_offw - 1, s_offh - 1);
}

void fxtk_off_end(fx_widget_t *cv)
{
    if (!s_offbuf) return;
    /* 整块一次刷出 (无中间态, 无闪烁) */
    s_drv->set_window((uint16_t)cv->x1, (uint16_t)cv->y1,
                      (uint16_t)cv->x2, (uint16_t)cv->y2);
    s_drv->push_pixels(s_offbuf, (uint32_t)(s_offw * s_offh));
    s_offing = 0;
    s_ox = 0; s_oy = 0;
    fx_reset_clip();
}

/* 给画布启用离屏缓冲 (malloc 一次, 失败返回 -1 降级直画) */
int fx_canvas_enable_buf(fx_widget_t *cv)
{
    if (!cv || cv->type != FX_W_CANVAS)
        return -1;
    if (s_offbuf)
        return 0;
    int w = cv->x2 - cv->x1 + 1, h = cv->y2 - cv->y1 + 1;
    s_offbuf = malloc((size_t)w * (size_t)h * 2);
    if (!s_offbuf)
        return -1;
    cv->flags |= FX_F_BUF;
    return 0;
}

/* ================================================================
 * 基础图元
 * ================================================================ */
void fx_draw_pixel(int x, int y) { fxtk_put_px(x, y, s_color); }

void fx_draw_hline(int x1, int x2, int y)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    for (int x = x1; x <= x2; x++)
        fxtk_put_px(x, y, s_color);
}

void fx_draw_vline(int x, int y1, int y2)
{
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (int y = y1; y <= y2; y++)
        fxtk_put_px(x, y, s_color);
}

void fx_draw_line(int x1, int y1, int x2, int y2)
{
    int dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int dy = y2 > y1 ? y2 - y1 : y1 - y2;
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        fxtk_put_px(x1, y1, s_color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx)  { err += dx; y1 += sy; }
    }
}

void fx_draw_rect(int x1, int y1, int x2, int y2)
{
    fx_draw_hline(x1, x2, y1);
    fx_draw_hline(x1, x2, y2);
    fx_draw_vline(x1, y1, y2);
    fx_draw_vline(x2, y1, y2);
}

void fx_fill_rect(int x1, int y1, int x2, int y2)
{
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    /* clip 求交 */
    if (x1 < s_clip_x1) x1 = s_clip_x1;
    if (y1 < s_clip_y1) y1 = s_clip_y1;
    if (x2 > s_clip_x2) x2 = s_clip_x2;
    if (y2 > s_clip_y2) y2 = s_clip_y2;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= s_drv->width) x2 = s_drv->width - 1;
    if (y2 >= s_drv->height) y2 = s_drv->height - 1;
    if (x1 > x2 || y1 > y2)
        return;
    /* 驱动级单色直写 (最快): 先冲掉行缓冲保证绘制顺序 */
    if (!s_framing && !s_offing && s_drv->fill_rect) {
        flush_line();
        /* 直写路径手动加原点偏移 (回退路径由 put_px 内部偏移) */
        int ax1 = x1 + s_ox, ay1 = y1 + s_oy, ax2 = x2 + s_ox, ay2 = y2 + s_oy;
        if (ax2 >= s_drv->width)  ax2 = s_drv->width - 1;
        if (ay2 >= s_drv->height) ay2 = s_drv->height - 1;
        if (ax1 <= ax2 && ay1 <= ay2)
            s_drv->fill_rect((uint16_t)ax1, (uint16_t)ay1,
                             (uint16_t)ax2, (uint16_t)ay2, s_color);
        return;
    }
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            fxtk_put_px(x, y, s_color);
}

/* 整数平方根 (二分, 用于圆/椭圆) */
static int isqrt(int n)
{
    if (n <= 0) return 0;
    int lo = 0, hi = 65536;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (mid * mid <= n) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/* ---------- 圆角矩形 ---------- */
static void round_cut(int y, int y1, int y2, int r, int *lcut, int *rcut)
{
    *lcut = 0; *rcut = 0;
    int dy;
    if (y - y1 < r) dy = r - (y - y1);
    else if (y2 - y < r) dy = r - (y2 - y);
    else return;
    *lcut = *rcut = r - isqrt(r * r - dy * dy);
}

void fx_fill_rect_round(int x1, int y1, int x2, int y2, int r)
{
    if (r <= 0) { fx_fill_rect(x1, y1, x2, y2); return; }
    int w = x2 - x1 + 1, h = y2 - y1 + 1;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int y = y1; y <= y2; y++) {
        int lc, rc;
        round_cut(y, y1, y2, r, &lc, &rc);
        fx_draw_hline(x1 + lc, x2 - rc, y);
    }
}

void fx_draw_rect_round(int x1, int y1, int x2, int y2, int r)
{
    if (r <= 0) { fx_draw_rect(x1, y1, x2, y2); return; }
    /* 四条直边 + 四个 1/4 圆弧 */
    fx_draw_hline(x1 + r, x2 - r, y1);
    fx_draw_hline(x1 + r, x2 - r, y2);
    fx_draw_vline(x1, y1 + r, y2 - r);
    fx_draw_vline(x2, y1 + r, y2 - r);
    /* 角: 用角度 0-90 的弧绘制 */
    for (int a = 0; a <= 90; a += 3) {
        double rad = a * 3.14159265 / 180.0;
        int dx = (int)(r * cos(rad) + 0.5);
        int dy = (int)(r * sin(rad) + 0.5);
        fxtk_put_px(x1 + r - dx, y1 + r - dy, s_color);   /* 左上 */
        fxtk_put_px(x2 - r + dx, y1 + r - dy, s_color);   /* 右上 */
        fxtk_put_px(x1 + r - dx, y2 - r + dy, s_color);   /* 左下 */
        fxtk_put_px(x2 - r + dx, y2 - r + dy, s_color);   /* 右下 */
    }
}

/* ---------- 圆 / 椭圆 ---------- */
void fx_draw_circle(int cx, int cy, int r)
{
    int x = 0, y = r, d = 3 - 2 * r;
    while (x <= y) {
        fxtk_put_px(cx + x, cy + y, s_color);
        fxtk_put_px(cx - x, cy + y, s_color);
        fxtk_put_px(cx + x, cy - y, s_color);
        fxtk_put_px(cx - x, cy - y, s_color);
        fxtk_put_px(cx + y, cy + x, s_color);
        fxtk_put_px(cx - y, cy + x, s_color);
        fxtk_put_px(cx + y, cy - x, s_color);
        fxtk_put_px(cx - y, cy - x, s_color);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

void fx_fill_circle(int cx, int cy, int r)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = isqrt(r * r - dy * dy);
        fx_draw_hline(cx - dx, cx + dx, cy + dy);
    }
}

void fx_draw_ellipse(int cx, int cy, int rx, int ry)
{
    if (rx <= 0 || ry <= 0) return;
    int x = -rx;
    while (x <= rx) {
        double t = 1.0 - (double)(x * x) / (double)(rx * rx);
        int y = (int)(ry * sqrt(t > 0 ? t : 0) + 0.5);
        fxtk_put_px(cx + x, cy + y, s_color);
        fxtk_put_px(cx + x, cy - y, s_color);
        x++;
    }
}

void fx_fill_ellipse(int cx, int cy, int rx, int ry)
{
    if (rx <= 0 || ry <= 0) return;
    for (int dy = -ry; dy <= ry; dy++) {
        double t = 1.0 - (double)(dy * dy) / (double)(ry * ry);
        int dx = (int)(rx * sqrt(t > 0 ? t : 0) + 0.5);
        fx_draw_hline(cx - dx, cx + dx, cy + dy);
    }
}

/* ---------- 圆弧 / 扇形 ---------- */
void fx_draw_arc(int cx, int cy, int r, int a1, int a2)
{
    if (a1 > a2) { int t = a1; a1 = a2; a2 = t; }
    for (int a = a1; a <= a2; a++) {
        double rad = a * 3.14159265 / 180.0;
        fxtk_put_px(cx + (int)(r * cos(rad) + 0.5),
                    cy + (int)(r * sin(rad) + 0.5), s_color);
    }
}

void fx_fill_arc(int cx, int cy, int r, int a1, int a2)
{
    /* 近似: 逐角度放射线到圆心 (每 2°) */
    if (a1 > a2) { int t = a1; a1 = a2; a2 = t; }
    for (int a = a1; a <= a2; a += 2) {
        double rad = a * 3.14159265 / 180.0;
        int ex = cx + (int)(r * cos(rad) + 0.5);
        int ey = cy + (int)(r * sin(rad) + 0.5);
        fx_draw_line(cx, cy, ex, ey);
    }
}

/* ---------- 三角形 / 多边形 (扫描线填充) ---------- */
void fx_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3)
{
    fx_draw_line(x1, y1, x2, y2);
    fx_draw_line(x2, y2, x3, y3);
    fx_draw_line(x3, y3, x1, y1);
}

/* 多边形扫描线填充: 逐行求与各边交点, 成对填充 */
void fx_fill_polygon(const int16_t *pts, int n)
{
    if (n < 3) return;
    int ymin = 32767, ymax = -32768;
    for (int i = 0; i < n; i++) {
        if (pts[i * 2 + 1] < ymin) ymin = pts[i * 2 + 1];
        if (pts[i * 2 + 1] > ymax) ymax = pts[i * 2 + 1];
    }
    int xs[64];
    for (int y = ymin; y <= ymax; y++) {
        int m = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int x1 = pts[i * 2], y1 = pts[i * 2 + 1];
            int x2 = pts[j * 2], y2 = pts[j * 2 + 1];
            if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) {
                int64_t x = (int64_t)x1 + (int64_t)(y - y1) * (x2 - x1) / (y2 - y1);
                if (m < 64) xs[m++] = (int)x;
            }
        }
        /* 冒泡排序交点 */
        for (int i = 0; i < m - 1; i++)
            for (int j = 0; j < m - 1 - i; j++)
                if (xs[j] > xs[j + 1]) {
                    int t = xs[j]; xs[j] = xs[j + 1]; xs[j + 1] = t;
                }
        for (int i = 0; i + 1 < m; i += 2)
            fx_draw_hline(xs[i], xs[i + 1], y);
    }
}

void fx_fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3)
{
    int16_t pts[6] = { (int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2,
                       (int16_t)x3, (int16_t)y3 };
    fx_fill_polygon(pts, 3);
}

void fx_draw_polygon(const int16_t *pts, int n)
{
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        fx_draw_line(pts[i * 2], pts[i * 2 + 1], pts[j * 2], pts[j * 2 + 1]);
    }
}
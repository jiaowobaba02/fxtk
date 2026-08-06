/**
 * fxtk_internal.h — fxtk 内部共享定义 (fxtk.c / fxtk_widgets.c / fxtk_draw.c)
 * 用户代码不需要包含此文件。
 */
#ifndef FXTK_INTERNAL_H
#define FXTK_INTERNAL_H

#include "fxtk.h"

/* 控件池上限 (静态分配, 无 malloc) */
#define FX_MAX_WIDGETS 64

/* 标签页: 顶部标签行高度 */
#define FX_TAB_H 24

/* 控件标志 */
#define FX_F_VISIBLE  0x01
#define FX_F_PRESSED  0x02
#define FX_F_ANIM     0x04   /* canvas: 每帧动画重绘 */
#define FX_F_BUF      0x08   /* canvas: 使用离屏缓冲 */

/* 位置模式 */
#define FX_POS_PIXEL   0   /* 相对父容器像素偏移 (ox*) */
#define FX_POS_PERCENT 1   /* 相对父容器千分比 (px*) */
#define FX_POS_GIRD    2   /* 网格定位 (gird_ref + gr1..gc2) */

/* 控件结构 */
struct fx_widget {
    uint8_t type;             /* FX_W_* */
    uint8_t flags;            /* FX_F_* */
    /* 布局后绝对矩形 (渲染/命中测试用) */
    int16_t x1, y1, x2, y2;
    /* 原始布局定义 */
    uint8_t pos_mode;
    int16_t ox1, oy1, ox2, oy2;   /* PIXEL: 相对父容器偏移 */
    int16_t px1, py1, px2, py2;   /* PERCENT: 千分比 (0.1 → 100) */
    int16_t gr1, gc1, gr2, gc2;   /* GIRD: 行列范围 (1-based) */
    fx_widget_t *gird_ref;        /* GIRD: 所属网格 */
    char name[24];                /* 名字 (库内复制, 查找/删除用) */
    char title[40];               /* 标题 (库内复制, 不依赖调用方字符串) */
    fx_cb_t cb;                   /* 点击回调; canvas = 每帧重绘回调 */
    void *ud;
    fx_color_t bg, fg;
    uint8_t border, radius;
    int16_t value;                /* slider/progress/checkbox */
    int16_t page;                 /* tab 子控件: 所属页号 (0 起) */
    int16_t lines, rows;          /* gird: 行数/列数 */
    fx_widget_t *parent, *child, *sibling;
};

/* 根容器 (全屏) */
extern fx_widget_t s_root;

/* fxtk.c 内部 */
void fxtk_draw_all(void);
fx_widget_t *fxtk_alloc(void);
void fxtk_free(fx_widget_t *w);
void fxtk_link(fx_widget_t *parent, fx_widget_t *child);
/* 只重绘带每帧回调的 canvas (动画区域), 静态控件不动 */
void fxtk_draw_canvases(void);
void fxtk_off_begin(fx_widget_t *cv);   /* 离屏渲染: 目标切到画布缓冲 */
void fxtk_off_end(fx_widget_t *cv);     /* 整块刷出 + 恢复直写 */

/* fxtk_draw.c 内部 */
void fxtk_draw_set_driver(const fx_driver_t *drv);
void fxtk_draw_flush_all(void);

/* fxtk_widgets.c: 各控件绘制 */
void fxtk_draw_button(fx_widget_t *w);
void fxtk_draw_label(fx_widget_t *w);
void fxtk_draw_gird(fx_widget_t *w);
void fxtk_draw_canvas(fx_widget_t *w);
void fxtk_draw_slider(fx_widget_t *w);
void fxtk_draw_progress(fx_widget_t *w);
void fxtk_draw_checkbox(fx_widget_t *w);
void fxtk_draw_panel(fx_widget_t *w);
void fxtk_draw_tab(fx_widget_t *w);

#endif
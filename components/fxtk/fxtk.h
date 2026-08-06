/**
 * fxtk.h — FX Toolkit 公共头文件
 *
 * 一个面向 ESP32 的极简图形界面库 (GFX Tool Kit)。
 * 设计目标: 语法比 GTK 简单, 控件用"属性构造器"声明式创建, 例如:
 *
 *     fx_button_new(pixel("20,20","100,40"), title("button"), call(button));
 *
 * 特性:
 *   - 控件: 按钮/文本/网格容器/画布/滑条/进度条/复选框/面板
 *   - 布局: pixel() 绝对像素 / percent() 百分比 (自适应分辨率) / gird() 网格
 *   - 矢量绘制: 线/矩形/圆/椭圆/圆弧/三角形/多边形/文本 (canvas 或立即模式)
 *   - 帧渲染: banded 帧缓冲, 整帧逐带刷新 (垂直同步式, 无撕裂)
 *   - 驱动抽象: 换屏幕只需换驱动 (见 fxtk_driver.h), 布局用 percent 自动适配
 *
 * 注意: C 语言里事件回调传函数指针, 请写 call(button) (不要带括号),
 * 回调函数签名固定为 void fn(fx_widget_t *w, void *ud)。
 */
#ifndef FXTK_H
#define FXTK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 颜色 (RGB565, 16bit)
 * ================================================================ */
typedef uint16_t fx_color_t;

#define FX_RGB(r, g, b) \
    ((fx_color_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define FX_RED    0xF800
#define FX_GREEN  0x07E0
#define FX_BLUE   0x001F
#define FX_WHITE  0xFFFF
#define FX_BLACK  0x0000
#define FX_YELLOW 0xFFE0
#define FX_CYAN   0x07FF
#define FX_MAGENTA 0xF81F
#define FX_GRAY   0x8430
#define FX_LGRAY  0xC618

/* ================================================================
 * 事件回调
 * ================================================================ */
struct fx_widget;
typedef struct fx_widget fx_widget_t;
typedef void (*fx_cb_t)(fx_widget_t *w, void *ud);

/* ================================================================
 * 属性系统
 *
 * 每个"属性构造器"返回一个 fx_attr_t, 多个属性作为变参传给
 * fx_xxx_new(...)。宏把它展开成属性数组 (末尾自动加 FX_ATTR_END),
 * 因此参数的顺序无关紧要, 但位置属性 (pixel/percent/gird) 三选一。
 * ================================================================ */
typedef enum {
    FX_A_NONE = 0,
    FX_A_PIXEL,      /* pixel("x1,y1","x2,y2")      绝对像素矩形      */
    FX_A_PERCENT,    /* percent("0.1,0.1","0.9,0.9") 百分比矩形(千分比) */
    FX_A_GIRD,       /* gird("name",r1,c1,r2,c2)     网格容器内定位     */
    FX_A_TITLE,      /* title("文本")                标题/文本          */
    FX_A_NAME,       /* name("id")                   控件名字(唯一,查找用) */
    FX_A_CALL,       /* call(fn)                     点击/变更回调      */
    FX_A_LINE,       /* line(n)                      gird 行数          */
    FX_A_ROW,        /* row(n)                       gird 列数          */
    FX_A_COLOR,      /* color(c)                     背景色             */
    FX_A_FGCOLOR,    /* fgcolor(c)                   前景/文字色        */
    FX_A_BORDER,     /* border(n)                    边框宽度           */
    FX_A_RADIUS,     /* radius(n)                    圆角半径           */
    FX_A_VALUE,      /* value(n)                     slider/progress 值 */
    FX_A_WIDGET,     /* fx_wptr(w)                   直接传控件指针      */
    FX_A_PAGE,       /* page(n)                      tab 子控件所属页 (0起) */
    FX_A_ANIM,       /* anim(n)                      canvas: 1=每帧动画重绘 */
} fx_attr_tag_t;

typedef struct {
    fx_attr_tag_t tag;
    union {
        struct { int16_t x1, y1, x2, y2; } rect;    /* 像素矩形 */
        struct { int16_t p1, p2, p3, p4; } pct;     /* 千分比 (0.1→100) */
        struct { const char *name; int16_t r1, c1, r2, c2; } gird;
        struct { const char *s; } str;
        struct { fx_cb_t cb; } cb;
        struct { int16_t v; } iv;
        struct { fx_color_t c; } color;
        struct { fx_widget_t *w; } w;
    } v;
} fx_attr_t;

#define FX_ATTR_END { FX_A_NONE, { {0} } }

/* ---------- 属性构造器 ---------- */
fx_attr_t pixel(const char *a, const char *b);       /* "20,20" "100,40" */
fx_attr_t percent(const char *a, const char *b);     /* "0.1,0.1" "0.9,0.9" */
/* gird("name") 仅名字 (fx_delete 用) / gird("name",r1,c1,r2,c2) 网格定位.
 * 宏重载: 1 参 → gird1, 5 参 → gird5; grid 是 gird 的兼容拼写 */
fx_attr_t gird1(const char *name);
fx_attr_t gird5(const char *name, int r1, int c1, int r2, int c2);
fx_attr_t grid1(const char *name);
fx_attr_t grid5(const char *name, int r1, int c1, int r2, int c2);
#define FX_GIRD_SEL(_1,_2,_3,_4,_5,NAME,...) NAME
#define gird(...) FX_GIRD_SEL(__VA_ARGS__, gird5, gird5, gird5, gird5, gird1)(__VA_ARGS__)
#define grid(...) FX_GIRD_SEL(__VA_ARGS__, grid5, grid5, grid5, grid5, grid1)(__VA_ARGS__)
fx_attr_t title(const char *s);
fx_attr_t text(const char *s);                       /* title 别名 */
fx_attr_t name(const char *s);
fx_attr_t call(fx_cb_t cb);
fx_attr_t line(int n);
fx_attr_t row(int n);
fx_attr_t color(fx_color_t c);
fx_attr_t fgcolor(fx_color_t c);
fx_attr_t border(int n);
fx_attr_t radius(int n);
fx_attr_t value(int n);
fx_attr_t page(int n);                       /* tab 子控件: 所属页号 (0 起) */
fx_attr_t anim(int n);                       /* canvas: 1=每帧动画重绘 (默认静态) */
fx_attr_t fx_wptr(fx_widget_t *w);                  /* 传控件指针的属性 */

/* ================================================================
 * 控件创建 (宏展开为属性数组; gird 是 grid 的兼容拼写)
 * ================================================================ */
enum {
    FX_W_NONE = 0,
    FX_W_BUTTON,      /* 按钮    */
    FX_W_LABEL,       /* 文本    */
    FX_W_GIRD,        /* 网格容器 */
    FX_W_CANVAS,      /* 画布    */
    FX_W_SLIDER,      /* 滑条    */
    FX_W_PROGRESS,    /* 进度条  */
    FX_W_CHECKBOX,    /* 复选框  */
    FX_W_PANEL,       /* 面板容器 */
    FX_W_TAB,         /* 标签页容器 (title="页1,页2" 逗号分隔, 子控件=页) */
    FX_W_COUNT
};

fx_widget_t *fx_widget_new_impl(int type, fx_attr_t attrs[]);

#define fx_button_new(...)   fx_widget_new_impl(FX_W_BUTTON,   (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_label_new(...)    fx_widget_new_impl(FX_W_LABEL,    (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_gird_map(...)     fx_widget_new_impl(FX_W_GIRD,     (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_grid_map(...)     fx_gird_map(__VA_ARGS__)
#define fx_canvas_new(...)   fx_widget_new_impl(FX_W_CANVAS,   (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_slider_new(...)   fx_widget_new_impl(FX_W_SLIDER,   (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_progress_new(...) fx_widget_new_impl(FX_W_PROGRESS, (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_checkbox_new(...) fx_widget_new_impl(FX_W_CHECKBOX, (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_panel_new(...)    fx_widget_new_impl(FX_W_PANEL,    (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})
#define fx_tab_new(...)      fx_widget_new_impl(FX_W_TAB,      (fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})

/* 设置当前父容器: 之后的 fx_xxx_new 挂到它下面 (NULL=根容器) */
void fx_parent(fx_widget_t *p);

/* 查找 / 删除: fx_delete(gird("gird")) 或 fx_delete(fx_wptr(w)) */
fx_widget_t *fx_find(const char *name);
void fx_delete_impl(fx_attr_t attrs[]);
#define fx_delete(...) fx_delete_impl((fx_attr_t[]){__VA_ARGS__, FX_ATTR_END})

/* ================================================================
 * 控件属性读写
 * ================================================================ */
void fx_set_title(fx_widget_t *w, const char *s);
void fx_set_color_w(fx_widget_t *w, fx_color_t c);   /* 改控件背景色 */
void fx_set_value(fx_widget_t *w, int v);        /* slider/progress/checkbox */
int  fx_get_value(const fx_widget_t *w);
void fx_set_cb(fx_widget_t *w, fx_cb_t cb, void *ud);
void fx_set_visible(fx_widget_t *w, int vis);
int  fx_widget_type(const fx_widget_t *w);
const char *fx_widget_title(const fx_widget_t *w);
/* 回调里读取控件几何 (w 是公共头的不完整类型, 不能直接 w->x1) */
void fx_widget_rect(const fx_widget_t *w, int *x1, int *y1, int *x2, int *y2);

/* ================================================================
 * 布局 (分辨率变化 / 树结构变化后自动调用; 也可手动强制)
 * ================================================================ */
void fx_layout(void);

/* ================================================================
 * 事件 (触摸驱动调用 / 主循环轮询)
 * ================================================================ */
void fx_touch_press(int x, int y);
void fx_touch_release(int x, int y);
void fx_touch_move(int x, int y);

/* ================================================================
 * 帧渲染 (垂直同步式整帧)
 *
 * fx_frame_begin() 后所有绘制进入 band 缓冲, fx_frame_end() 一次性
 * 逐 band 刷屏。主循环固定节奏调用即可获得无撕裂整帧效果。
 * 不调用 begin/end 时为立即模式 (逐行缓冲直写, 适合局部刷新)。
 * ================================================================ */
void fx_frame_begin(void);
void fx_frame_end(void);
int  fx_band_index(void);             /* band 主循环中返回当前带 (0..N-1), 否则 -1 */
void fx_repaint(void);                    /* 请求下一轮全屏重绘 */
void fx_repaint_rect(int x1, int y1, int x2, int y2);  /* 请求局部重绘 (自动合并脏区) */

/* ================================================================
 * 矢量绘制 (立即模式; 帧模式下进 band 缓冲)
 * ================================================================ */
void fx_set_color(fx_color_t c);          /* 当前绘制色 */
void fx_set_clip(int x1, int y1, int x2, int y2);   /* 像素裁剪 */
void fx_reset_clip(void);

void fx_draw_pixel(int x, int y);
void fx_draw_hline(int x1, int x2, int y);
void fx_draw_vline(int x, int y1, int y2);
void fx_draw_line(int x1, int y1, int x2, int y2);      /* Bresenham */
void fx_draw_rect(int x1, int y1, int x2, int y2);      /* 边框 */
void fx_fill_rect(int x1, int y1, int x2, int y2);
void fx_draw_rect_round(int x1, int y1, int x2, int y2, int r);
void fx_fill_rect_round(int x1, int y1, int x2, int y2, int r);
void fx_draw_circle(int cx, int cy, int r);
void fx_fill_circle(int cx, int cy, int r);
void fx_draw_ellipse(int cx, int cy, int rx, int ry);
void fx_fill_ellipse(int cx, int cy, int rx, int ry);
void fx_draw_arc(int cx, int cy, int r, int a1, int a2); /* 0-360°, 逆时针 */
void fx_fill_arc(int cx, int cy, int r, int a1, int a2);
void fx_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3);
void fx_fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3);
void fx_draw_polygon(const int16_t *pts, int n);       /* n 个顶点, pts=x0,y0,... */
void fx_fill_polygon(const int16_t *pts, int n);

/* 文本: 中文 16px 灰度 + ASCII Arial 16px 混排, \n 换行 */
void fx_draw_text(int x, int y, const char *s);
void fx_draw_text_c(int x, int y, const char *s, fx_color_t fg, fx_color_t bg);
int  fx_text_width(const char *s);

/* canvas 控件内绘制: begin 后 fx_draw_* 自动裁剪到画布区域 */
void fx_canvas_begin(fx_widget_t *cv);
void fx_canvas_end(void);
int  fx_canvas_enable_buf(fx_widget_t *cv);  /* 离屏缓冲: 整帧一次刷出, 无闪烁 */

/* ================================================================
 * 驱动抽象 (自适应分辨率: 布局用 percent, 换屏只换驱动)
 * ================================================================ */
typedef struct {
    uint16_t width, height;               /* 屏幕分辨率 */
    int  (*init)(void);                   /* 初始化, 0=成功 */
    void (*set_window)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void (*push_pixels)(const uint16_t *px, uint32_t n);  /* RGB565, 大端 */
    void (*hold_begin)(void);             /* 连续事务 (可选) */
    void (*hold_end)(void);
    void (*fill_rect)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color); /* 可选: 单色填充直写 */
    int  (*touch_read)(int *x, int *y, int *pressed);     /* 0=无事件 */
} fx_driver_t;

/* ================================================================
 * 系统
 * ================================================================ */
void fx_init(const fx_driver_t *drv);    /* 初始化库 (驱动已 init) */
void fx_poll(void);                       /* 主循环: 触摸 + 重绘 */
uint16_t fx_width(void);
uint16_t fx_height(void);
void fx_set_autorepaint(int on);          /* 1=每轮 poll 整帧重绘 (默认) */
void fx_set_touch_debug(int on);          /* 1=屏幕左上角显示触摸坐标 (排查用) */
void fx_set_bg(fx_color_t c);             /* 清屏背景色 (默认黑色) */

/* ---------- 内置驱动 (fxtk_st6201.c / fxtk_touch.c) ---------- */
extern fx_driver_t fx_st6201_driver;      /* 480x272 ST6201 SPI 屏 */
int  fx_gt911_init(void);                 /* GT911 触摸初始化, 0=成功 */
int  fx_gt911_read(int *x, int *y, int *pressed);  /* 每轮轮询当前状态 */

#ifdef __cplusplus
}
#endif
#endif /* FXTK_H */
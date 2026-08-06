/**
 * fxtk.c — fxtk 核心: 属性解析 / 控件树 / 布局 / 事件分发 / 帧渲染
 *
 * 声明式创建流程:
 *   fx_button_new(pixel("20,20","100,40"), title("button"), call(button));
 *   1. 宏把属性列表展开成 fx_attr_t 数组 (末尾 FX_ATTR_END)
 *   2. fx_widget_new_impl 解析属性, 从静态池取控件, 挂到父容器
 *   3. fx_layout 重算全部控件像素矩形
 *   4. 每帧 fx_poll → 触摸事件分发 + 整帧重绘 (banded vsync)
 */
#include "fxtk_internal.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "fxtk";

fx_widget_t s_root;                      /* 根容器: 全屏, 不可删除 */
static fx_widget_t s_pool[FX_MAX_WIDGETS];
static const fx_driver_t *s_drv;
static fx_widget_t *s_pressed = NULL;    /* 当前按下的控件 */
static fx_widget_t *s_parent = NULL;     /* 当前父容器 (fx_parent 设置) */
static int s_touch_prev = 0;             /* 上一轮触摸状态 (边沿检测) */
static int s_last_tx = -1, s_last_ty = -1;  /* 最后触摸坐标 (抬起时复用) */
static int s_repaint = 1;
static int s_autorepaint = 1;
static fx_color_t s_bg = FX_BLACK;       /* 清屏背景色 */
#define FX_DIRTY_MAX 8
static int s_dirty[FX_DIRTY_MAX][4];     /* 脏区列表 (局部重绘) */
static int s_dirty_n = 0;
/* 触摸调试 overlay: 最近触摸状态 (屏幕左上角显示, 排查触摸问题用) */
static int s_tdbg_on = 0;
static int s_tdbg_x = -1, s_tdbg_y = -1, s_tdbg_p = 0;
static char s_tdbg_str[48] = "touch: -";
static void redraw_widget_now(fx_widget_t *w);   /* 定义在下方 (事件处理用) */

/* ================================================================
 * 控件池
 * ================================================================ */
fx_widget_t *fxtk_alloc(void)
{
    for (int i = 0; i < FX_MAX_WIDGETS; i++)
        if (s_pool[i].type == FX_W_NONE) {
            memset(&s_pool[i], 0, sizeof(s_pool[i]));
            return &s_pool[i];
        }
    ESP_LOGE(TAG, "widget pool full (%d)!", FX_MAX_WIDGETS);
    return NULL;
}

void fxtk_free(fx_widget_t *w)
{
    if (!w || w == &s_root)
        return;
    w->type = FX_W_NONE;
}

void fxtk_link(fx_widget_t *parent, fx_widget_t *child)
{
    child->parent = parent;
    child->sibling = parent->child;
    parent->child = child;
}

void fx_parent(fx_widget_t *p) { s_parent = p; }

/* ================================================================
 * 属性构造器
 * ================================================================ */
static int parse_xy(const char *s, int16_t *a, int16_t *b)
{
    int x, y;
    if (sscanf(s, "%d,%d", &x, &y) != 2)
        return 0;
    *a = (int16_t)x;
    *b = (int16_t)y;
    return 1;
}

static int parse_pct(const char *s, int16_t *a, int16_t *b)
{
    float x, y;
    if (sscanf(s, "%f,%f", &x, &y) != 2)
        return 0;
    *a = (int)(x * 1000.0f);
    *b = (int)(y * 1000.0f);
    return 1;
}

fx_attr_t pixel(const char *a, const char *b)
{
    fx_attr_t at = { FX_A_PIXEL, { {0} } };
    parse_xy(a, &at.v.rect.x1, &at.v.rect.y1);
    parse_xy(b, &at.v.rect.x2, &at.v.rect.y2);
    return at;
}

fx_attr_t percent(const char *a, const char *b)
{
    fx_attr_t at = { FX_A_PERCENT, { {0} } };
    parse_pct(a, &at.v.pct.p1, &at.v.pct.p2);
    parse_pct(b, &at.v.pct.p3, &at.v.pct.p4);
    return at;
}

/* gird("name") = 仅名字 (fx_delete 用); gird("name",r1,c1,r2,c2) = 网格定位.
 * 宏重载: 1 参 → gird1, 5 参 → gird5 */
fx_attr_t gird1(const char *gname)
{
    fx_attr_t at = { FX_A_GIRD, { {0} } };
    at.v.gird.name = gname;
    return at;
}

fx_attr_t gird5(const char *gname, int r1, int c1, int r2, int c2)
{
    fx_attr_t at = { FX_A_GIRD, { {0} } };
    at.v.gird.name = gname;
    at.v.gird.r1 = (int16_t)r1; at.v.gird.c1 = (int16_t)c1;
    at.v.gird.r2 = (int16_t)r2; at.v.gird.c2 = (int16_t)c2;
    return at;
}

fx_attr_t grid1(const char *gname) { return gird1(gname); }
fx_attr_t grid5(const char *gname, int r1, int c1, int r2, int c2)
{
    return gird5(gname, r1, c1, r2, c2);
}

fx_attr_t title(const char *s)  { fx_attr_t a = { FX_A_TITLE, { {0} } }; a.v.str.s = s; return a; }
fx_attr_t text(const char *s)   { fx_attr_t a = { FX_A_TITLE, { {0} } }; a.v.str.s = s; return a; }
fx_attr_t name(const char *s)   { fx_attr_t a = { FX_A_NAME, { {0} } }; a.v.str.s = s; return a; }
fx_attr_t call(fx_cb_t cb)      { fx_attr_t a = { FX_A_CALL, { {0} } }; a.v.cb.cb = cb; return a; }
fx_attr_t line(int n)           { fx_attr_t a = { FX_A_LINE, { {0} } }; a.v.iv.v = (int16_t)n; return a; }
fx_attr_t row(int n)            { fx_attr_t a = { FX_A_ROW, { {0} } }; a.v.iv.v = (int16_t)n; return a; }
fx_attr_t color(fx_color_t c)   { fx_attr_t a = { FX_A_COLOR, { {0} } }; a.v.color.c = c; return a; }
fx_attr_t fgcolor(fx_color_t c) { fx_attr_t a = { FX_A_FGCOLOR, { {0} } }; a.v.color.c = c; return a; }
fx_attr_t border(int n)         { fx_attr_t a = { FX_A_BORDER, { {0} } }; a.v.iv.v = (int16_t)n; return a; }
fx_attr_t radius(int n)         { fx_attr_t a = { FX_A_RADIUS, { {0} } }; a.v.iv.v = (int16_t)n; return a; }
fx_attr_t value(int n)          { fx_attr_t a = { FX_A_VALUE, { {0} } }; a.v.iv.v = (int16_t)n; return a; }
fx_attr_t page(int n)           { fx_attr_t a = { FX_A_PAGE, { {0} } }; a.v.iv.v = (int16_t)n; return a; }
fx_attr_t anim(int n)           { fx_attr_t a = { FX_A_ANIM, { {0} } }; a.v.iv.v = (int16_t)n; return a; }
fx_attr_t fx_wptr(fx_widget_t *w) { fx_attr_t a = { FX_A_WIDGET, { {0} } }; a.v.w.w = w; return a; }

/* ================================================================
 * 控件创建
 * ================================================================ */
fx_widget_t *fx_widget_new_impl(int type, fx_attr_t attrs[])
{
    fx_widget_t *w = fxtk_alloc();
    if (!w)
        return NULL;
    w->type = (uint8_t)type;
    w->flags = FX_F_VISIBLE;
    w->pos_mode = FX_POS_PIXEL;
    w->border = 1;
    w->radius = 4;
    w->fg = FX_WHITE;
    switch (type) {              /* 各控件默认主题 */
    case FX_W_LABEL:  w->bg = FX_BLACK;  break;   /* 透明语义 */
    case FX_W_CANVAS: w->bg = FX_BLACK;  break;
    case FX_W_GIRD:   w->bg = 0x0841;    break;
    case FX_W_PANEL:  w->bg = 0x0841;    break;
    case FX_W_TAB:    w->bg = 0x0841;    break;
    case FX_W_CHECKBOX: w->bg = FX_BLACK; break;
    case FX_W_SLIDER: w->bg = 0x5D7C; w->fg = 0x8430; break;
    case FX_W_PROGRESS: w->bg = 0x07E0; w->fg = 0x8430; break;
    default:          w->bg = 0x5D7C;    break;   /* 亮蓝 */
    }

    for (int i = 0; attrs[i].tag != FX_A_NONE; i++) {
        switch (attrs[i].tag) {
        case FX_A_PIXEL:
            w->pos_mode = FX_POS_PIXEL;
            w->ox1 = attrs[i].v.rect.x1; w->oy1 = attrs[i].v.rect.y1;
            w->ox2 = attrs[i].v.rect.x2; w->oy2 = attrs[i].v.rect.y2;
            break;
        case FX_A_PERCENT:
            w->pos_mode = FX_POS_PERCENT;
            w->px1 = attrs[i].v.pct.p1; w->py1 = attrs[i].v.pct.p2;
            w->px2 = attrs[i].v.pct.p3; w->py2 = attrs[i].v.pct.p4;
            break;
        case FX_A_GIRD:
            w->pos_mode = FX_POS_GIRD;
            w->gird_ref = fx_find(attrs[i].v.gird.name);
            w->gr1 = attrs[i].v.gird.r1; w->gc1 = attrs[i].v.gird.c1;
            w->gr2 = attrs[i].v.gird.r2; w->gc2 = attrs[i].v.gird.c2;
            if (!w->gird_ref)
                ESP_LOGW(TAG, "gird '%s' not found (widget 将落在左上角)",
                         attrs[i].v.gird.name);
            break;
        case FX_A_TITLE:
            strncpy(w->title, attrs[i].v.str.s ? attrs[i].v.str.s : "",
                    sizeof(w->title) - 1);
            break;
        case FX_A_NAME:
            strncpy(w->name, attrs[i].v.str.s ? attrs[i].v.str.s : "",
                    sizeof(w->name) - 1);
            break;
        case FX_A_CALL:   w->cb = attrs[i].v.cb.cb; break;
        case FX_A_LINE:   w->lines = attrs[i].v.iv.v; break;
        case FX_A_ROW:    w->rows = attrs[i].v.iv.v; break;
        case FX_A_COLOR:  w->bg = attrs[i].v.color.c; break;
        case FX_A_FGCOLOR: w->fg = attrs[i].v.color.c; break;
        case FX_A_BORDER: w->border = (uint8_t)attrs[i].v.iv.v; break;
        case FX_A_RADIUS: w->radius = (uint8_t)attrs[i].v.iv.v; break;
        case FX_A_VALUE:  w->value = attrs[i].v.iv.v; break;
        case FX_A_PAGE:   w->page = attrs[i].v.iv.v; break;
        case FX_A_ANIM:   if (attrs[i].v.iv.v) w->flags |= FX_F_ANIM; break;
        default: break;
        }
    }

    fx_widget_t *parent = w->gird_ref ? w->gird_ref :
                          (s_parent ? s_parent : &s_root);
    fxtk_link(parent, w);
    if (type == FX_W_TAB) {                /* 页数 = 标题逗号数 + 1 */
        int n = 1;
        for (const char *p = w->title; *p; p++)
            if (*p == ',') n++;
        w->lines = (int16_t)n;
    }
    fx_layout();
    fx_repaint();
    ESP_LOGI(TAG, "new type=%d '%s'", type, w->name ? w->name : "-");
    return w;
}

/* ================================================================
 * 查找 / 删除
 * ================================================================ */
fx_widget_t *fx_find(const char *wname)
{
    if (!wname)
        return NULL;
    for (int i = 0; i < FX_MAX_WIDGETS; i++)
        if (s_pool[i].type != FX_W_NONE && s_pool[i].name[0] &&
            strcmp(s_pool[i].name, wname) == 0)
            return &s_pool[i];
    return NULL;
}

/* 从树断开并释放 (含全部子控件) */
static void unlink_free(fx_widget_t *w)
{
    if (!w || w == &s_root)
        return;
    if (w->parent) {
        fx_widget_t **pp = &w->parent->child;
        while (*pp && *pp != w) pp = &(*pp)->sibling;
        if (*pp) *pp = w->sibling;
    }
    /* 递归释放子 */
    while (w->child)
        unlink_free(w->child);
    if (s_pressed == w)
        s_pressed = NULL;
    fxtk_free(w);
}

void fx_delete_impl(fx_attr_t attrs[])
{
    for (int i = 0; attrs[i].tag != FX_A_NONE; i++) {
        fx_widget_t *w = NULL;
        if (attrs[i].tag == FX_A_GIRD)
            w = fx_find(attrs[i].v.gird.name);
        else if (attrs[i].tag == FX_A_WIDGET)
            w = attrs[i].v.w.w;
        if (w) {
            ESP_LOGI(TAG, "delete '%s'", w->name ? w->name : "-");
            unlink_free(w);
        }
    }
    fx_layout();
    fx_repaint();
}

/* ================================================================
 * 布局 (percent 自适应分辨率 / gird 网格 / pixel 相对父容器)
 * ================================================================ */
static void layout_children(fx_widget_t *p);

void fx_layout(void)
{
    if (!s_drv)
        return;
    s_root.x1 = 0; s_root.y1 = 0;
    s_root.x2 = (int16_t)(s_drv->width - 1);
    s_root.y2 = (int16_t)(s_drv->height - 1);
    layout_children(&s_root);
}

static void layout_children(fx_widget_t *p)
{
    for (fx_widget_t *c = p->child; c; c = c->sibling) {
        if (!(c->flags & FX_F_VISIBLE))
            continue;
        switch (c->pos_mode) {
        case FX_POS_PIXEL:
            c->x1 = (int16_t)(p->x1 + c->ox1);
            c->y1 = (int16_t)(p->y1 + c->oy1);
            c->x2 = (int16_t)(p->x1 + c->ox2);
            c->y2 = (int16_t)(p->y1 + c->oy2);
            break;
        case FX_POS_PERCENT: {
            int pw = p->x2 - p->x1 + 1, ph = p->y2 - p->y1 + 1;
            c->x1 = (int16_t)(p->x1 + (int32_t)pw * c->px1 / 1000);
            c->y1 = (int16_t)(p->y1 + (int32_t)ph * c->py1 / 1000);
            c->x2 = (int16_t)(p->x1 + (int32_t)pw * c->px2 / 1000 - 1);
            c->y2 = (int16_t)(p->y1 + (int32_t)ph * c->py2 / 1000 - 1);
            break;
        }
        case FX_POS_GIRD: {
            fx_widget_t *g = c->gird_ref;
            if (g && g->lines > 0 && g->rows > 0) {
                int cellw = (g->x2 - g->x1 + 1) / g->rows;
                int cellh = (g->y2 - g->y1 + 1) / g->lines;
                c->x1 = (int16_t)(g->x1 + (c->gc1 - 1) * cellw);
                c->y1 = (int16_t)(g->y1 + (c->gr1 - 1) * cellh);
                c->x2 = (int16_t)(g->x1 + c->gc2 * cellw - 1);
                c->y2 = (int16_t)(g->y1 + c->gr2 * cellh - 1);
            }
            break;
        }
        }
        layout_children(c);      /* 容器递归 */
    }
}

/* ================================================================
 * 事件分发 (后序命中: 子控件优先)
 * ================================================================ */
static fx_widget_t *hit_test(fx_widget_t *w, int x, int y)
{
    fx_widget_t *r = NULL;
    if (w->type == FX_W_TAB) {
        /* 标签行: 命中 tab 自身 (release 时切页) */
        if (y >= w->y1 && y <= w->y1 + FX_TAB_H - 1 &&
            x >= w->x1 && x <= w->x2)
            return w;
        /* 内容区: 只测当前页的子控件 (隐藏页不可点) */
        for (fx_widget_t *c = w->child; c && !r; c = c->sibling)
            if (c->page == w->value && (c->flags & FX_F_VISIBLE))
                r = hit_test(c, x, y);
        return r;
    }
    for (fx_widget_t *c = w->child; c && !r; c = c->sibling)
        if (c->flags & FX_F_VISIBLE)
            r = hit_test(c, x, y);
    if (r)
        return r;
    if (!(w->flags & FX_F_VISIBLE) || w == &s_root)
        return NULL;
    if (w->type == FX_W_GIRD || w->type == FX_W_PANEL)
        return NULL;                          /* 容器不响应点击 */
    if (x >= w->x1 && x <= w->x2 && y >= w->y1 && y <= w->y2)
        return w;
    return NULL;
}

void fx_touch_press(int x, int y)
{
    s_pressed = hit_test(&s_root, x, y);
    if (s_pressed) {
        s_pressed->flags |= FX_F_PRESSED;
        redraw_widget_now(s_pressed);
    }
}

void fx_touch_release(int x, int y)
{
    fx_widget_t *w = hit_test(&s_root, x, y);
    fx_widget_t *p = s_pressed;
    s_pressed = NULL;                        /* 先置空: 回调里可能删除控件 */
    if (p) {
        p->flags &= (uint8_t)~FX_F_PRESSED;
        redraw_widget_now(p);
        if (w == p) {
            if (p->type == FX_W_TAB) {       /* 标签页: 点击标签行切页 */
                int n = p->lines > 0 ? p->lines : 1;
                int tw = (p->x2 - p->x1 + 1) / n;
                int pg = (x - p->x1) / tw;
                if (pg < 0) pg = 0;
                if (pg >= n) pg = n - 1;
                if (pg != p->value) {
                    p->value = (int16_t)pg;
                    fx_repaint_rect(p->x1, p->y1, p->x2, p->y2);
                }
            } else {
                if (p->type == FX_W_CHECKBOX)   /* 复选框: 点击切换 */
                    p->value = !p->value;
                if (p->cb)
                    p->cb(p, p->ud);         /* 按下并抬起在同一控件: 点击 */
            }
        }
    }
}

void fx_touch_move(int x, int y)
{
    if (s_pressed && s_pressed->type == FX_W_SLIDER) {
        int w = s_pressed->x2 - s_pressed->x1 + 1;
        int v = w > 0 ? (x - s_pressed->x1) * 100 / w : 0;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        if (v != s_pressed->value) {
            s_pressed->value = (int16_t)v;
            redraw_widget_now(s_pressed);
            if (s_pressed->cb)
                s_pressed->cb(s_pressed, s_pressed->ud);
        }
    }
}

/* 立即重画一个控件: 覆盖型 (整矩形填充) 直接画, 无"先黑后现"闪烁;
 * 透明型 (label 等) 走脏区重绘 */
static void redraw_widget_now(fx_widget_t *w)
{
    if (!w || !(w->flags & FX_F_VISIBLE))
        return;
    switch (w->type) {
    case FX_W_BUTTON:
    case FX_W_GIRD:
    case FX_W_CANVAS:
    case FX_W_SLIDER:
    case FX_W_PROGRESS:
    case FX_W_CHECKBOX:
    case FX_W_PANEL:
    case FX_W_TAB:
        fx_set_clip(w->x1, w->y1, w->x2, w->y2);
        switch (w->type) {
        case FX_W_BUTTON:   fxtk_draw_button(w); break;
        case FX_W_GIRD:     fxtk_draw_gird(w); break;
        case FX_W_CANVAS:   fxtk_draw_canvas(w); break;
        case FX_W_SLIDER:   fxtk_draw_slider(w); break;
        case FX_W_PROGRESS: fxtk_draw_progress(w); break;
        case FX_W_CHECKBOX: fxtk_draw_checkbox(w); break;
        case FX_W_PANEL:    fxtk_draw_panel(w); break;
        case FX_W_TAB:      fxtk_draw_tab(w); break;
        default: break;
        }
        fx_reset_clip();
        return;
    default:
        fx_repaint_rect(w->x1, w->y1, w->x2, w->y2);   /* 透明型 */
    }
}

/* ================================================================
 * 控件树绘制 (每帧)
 * ================================================================ */
/* cx1..cy2 = 外部裁剪区 (全屏重绘=全屏; 局部重绘=脏区) */
static void draw_widget(fx_widget_t *w, int cx1, int cy1, int cx2, int cy2)
{
    if (!(w->flags & FX_F_VISIBLE))
        return;
    int x1 = w->x1 > cx1 ? w->x1 : cx1;
    int y1 = w->y1 > cy1 ? w->y1 : cy1;
    int x2 = w->x2 < cx2 ? w->x2 : cx2;
    int y2 = w->y2 < cy2 ? w->y2 : cy2;
    if (x1 <= x2 && y1 <= y2) {
    fx_set_clip(x1, y1, x2, y2);          /* 控件矩形 ∩ 外部裁剪区 */
    switch (w->type) {
    case FX_W_TAB:
        fxtk_draw_tab(w);
        /* 只画当前页的子控件 (page == 当前页) */
        for (fx_widget_t *c = w->child; c; c = c->sibling)
            if (c->page == w->value)
                draw_widget(c, cx1, cy1, cx2, cy2);
        return;                             /* 子控件已在上面处理 */
    case FX_W_BUTTON:   fxtk_draw_button(w); break;
    case FX_W_LABEL:    fxtk_draw_label(w); break;
    case FX_W_GIRD:     fxtk_draw_gird(w); break;
    case FX_W_PANEL:    fxtk_draw_panel(w); break;
    case FX_W_SLIDER:   fxtk_draw_slider(w); break;
    case FX_W_PROGRESS: fxtk_draw_progress(w); break;
    case FX_W_CHECKBOX: fxtk_draw_checkbox(w); break;
    case FX_W_CANVAS:
        if ((w->flags & FX_F_BUF) && w->cb) {
            fxtk_off_begin(w);                /* 离屏: 画完整块刷出, 无闪烁 */
            fxtk_draw_canvas(w);
            fx_canvas_begin(w);
            w->cb(w, w->ud);
            fx_canvas_end();
            fxtk_off_end(w);
        } else {
            fxtk_draw_canvas(w);
            if (w->cb) {                      /* canvas: 内容回调 */
                fx_canvas_begin(w);           /* 原点=画布左上, clip=画布 */
                w->cb(w, w->ud);
                fx_canvas_end();
            }
        }
        break;
    default: break;
    }
    }
    for (fx_widget_t *c = w->child; c; c = c->sibling)
        draw_widget(c, cx1, cy1, cx2, cy2);
}

void fxtk_draw_all(void)
{
    if (!s_drv)
        return;
    /* 整帧直写: 清屏 (驱动单色填充, 1 事务) + 控件顺序绘制 */
    fx_set_color(s_bg);
    fx_fill_rect(0, 0, s_drv->width - 1, s_drv->height - 1);
    draw_widget(&s_root, 0, 0, s_drv->width - 1, s_drv->height - 1);
    if (s_tdbg_on)                          /* 触摸调试 overlay */
        fx_draw_text_c(2, 2, s_tdbg_str, FX_YELLOW, FX_BLACK);
}

/* 局部重绘一个区域: 清背景 + 重绘与其相交的控件 */
static void redraw_region(int x1, int y1, int x2, int y2)
{
    fx_set_color(s_bg);
    fx_fill_rect(x1, y1, x2, y2);
    draw_widget(&s_root, x1, y1, x2, y2);
}

/* 只重绘带每帧回调的 canvas (动画区域): 静态控件不动, 无全屏闪烁 */
static void draw_canvas_only(fx_widget_t *w)
{
    if (!(w->flags & FX_F_VISIBLE))
        return;
    if (w->type == FX_W_TAB) {
        /* 只重绘当前页里的动画画布 (隐藏页不画) */
        for (fx_widget_t *c = w->child; c; c = c->sibling)
            if (c->page == w->value)
                draw_canvas_only(c);
        return;
    }
    if (w->type == FX_W_CANVAS && w->cb && (w->flags & FX_F_ANIM)) {
        /* 动画画布每帧重绘 (静态画布只在事件/切页时画) */
        if (w->flags & FX_F_BUF) {   /* 缓冲已由 enable_buf 分配 */
            fxtk_off_begin(w);
            fxtk_draw_canvas(w);
            fx_canvas_begin(w);
            w->cb(w, w->ud);
            fx_canvas_end();
            fxtk_off_end(w);
        } else {
            fxtk_draw_canvas(w);
            fx_canvas_begin(w);
            w->cb(w, w->ud);
            fx_canvas_end();
        }
    }
    for (fx_widget_t *c = w->child; c; c = c->sibling)
        draw_canvas_only(c);
}

void fxtk_draw_canvases(void)
{
    if (!s_drv)
        return;
    draw_canvas_only(&s_root);
    fxtk_draw_flush_all();
}

/* ================================================================
 * 系统 API
 * ================================================================ */
void fx_init(const fx_driver_t *drv)
{
    s_drv = drv;
    fxtk_draw_set_driver(drv);
    memset(&s_root, 0, sizeof(s_root));
    s_root.type = FX_W_PANEL;
    s_root.flags = FX_F_VISIBLE;
    memset(s_pool, 0, sizeof(s_pool));
    s_pressed = NULL;
    s_touch_prev = 0;
    s_repaint = 1;
    s_autorepaint = 0;          /* 默认事件驱动: 有变化才全屏重绘 */
    fx_layout();
    ESP_LOGI(TAG, "fxtk ready: %dx%d", drv->width, drv->height);
}

void fx_poll(void)
{
    if (!s_drv)
        return;
    /* 触摸: 驱动每次返回当前状态, 库内做边沿检测 */
    if (s_drv->touch_read) {
        int x = 0, y = 0, p = 0;
        if (s_drv->touch_read(&x, &y, &p)) {
            if (p) { s_last_tx = x; s_last_ty = y; }
            if (p && !s_touch_prev) fx_touch_press(x, y);
            else if (p && s_touch_prev) fx_touch_move(x, y);
            else if (!p && s_touch_prev)
                fx_touch_release(s_last_tx, s_last_ty);  /* 抬起: 用最后坐标 */
            s_touch_prev = p;
            if (s_tdbg_on) {                /* 记录调试信息 */
                s_tdbg_x = x; s_tdbg_y = y; s_tdbg_p = p;
                snprintf(s_tdbg_str, sizeof(s_tdbg_str),
                         "T:%3d,%3d %s", x, y, p ? "DOWN" : "up  ");
            }
        }
    }
    /* 渲染: 脏区局部重绘 (快) / 全屏重绘 / canvas 动画区域 */
    if (s_repaint && s_dirty_n > 0) {
        s_repaint = 0;
        fx_frame_begin();
        for (int i = 0; i < s_dirty_n; i++)
            redraw_region(s_dirty[i][0], s_dirty[i][1],
                          s_dirty[i][2], s_dirty[i][3]);
        s_dirty_n = 0;
        fx_frame_end();
    } else if (s_repaint) {
        s_repaint = 0;
        fx_frame_begin();
        fxtk_draw_all();
        fx_frame_end();
    } else if (s_autorepaint) {
        fx_frame_begin();
        fxtk_draw_all();
        fx_frame_end();
    } else {
        fxtk_draw_canvases();               /* 只有动画画布在动 */
    }
}

uint16_t fx_width(void)  { return s_drv ? s_drv->width : 0; }
uint16_t fx_height(void) { return s_drv ? s_drv->height : 0; }

void fx_set_autorepaint(int on) { s_autorepaint = on; }
void fx_set_touch_debug(int on) { s_tdbg_on = on; }
void fx_repaint(void) { s_repaint = 1; s_dirty_n = 0; }

/* 请求重绘一个矩形区域 (自动与已有脏区合并) */
void fx_repaint_rect(int x1, int y1, int x2, int y2)
{
    if (!s_drv)
        return;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= s_drv->width) x2 = s_drv->width - 1;
    if (y2 >= s_drv->height) y2 = s_drv->height - 1;
    if (x1 > x2 || y1 > y2)
        return;
    for (int i = 0; i < s_dirty_n; i++) {       /* 相交则合并 */
        if (x1 <= s_dirty[i][2] && s_dirty[i][0] <= x2 &&
            y1 <= s_dirty[i][3] && s_dirty[i][1] <= y2) {
            if (x1 < s_dirty[i][0]) s_dirty[i][0] = x1;
            if (y1 < s_dirty[i][1]) s_dirty[i][1] = y1;
            if (x2 > s_dirty[i][2]) s_dirty[i][2] = x2;
            if (y2 > s_dirty[i][3]) s_dirty[i][3] = y2;
            s_repaint = 1;
            return;
        }
    }
    if (s_dirty_n < FX_DIRTY_MAX) {
        s_dirty[s_dirty_n][0] = x1; s_dirty[s_dirty_n][1] = y1;
        s_dirty[s_dirty_n][2] = x2; s_dirty[s_dirty_n][3] = y2;
        s_dirty_n++;
        s_repaint = 1;
    } else {
        fx_repaint();                           /* 脏区满: 退化全屏 */
    }
}

void fx_set_bg(fx_color_t c) { s_bg = c; fx_repaint(); }

/* ================================================================
 * 控件属性读写
 * ================================================================ */
void fx_set_title(fx_widget_t *w, const char *s)
{
    if (!w) return;
    strncpy(w->title, s ? s : "", sizeof(w->title) - 1);
    redraw_widget_now(w);
}

void fx_set_color_w(fx_widget_t *w, fx_color_t c)
{
    if (!w) return;
    w->bg = c;
    redraw_widget_now(w);
}

void fx_set_value(fx_widget_t *w, int v)
{
    if (!w) return;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    w->value = (int16_t)v;
    redraw_widget_now(w);
}

int fx_get_value(const fx_widget_t *w) { return w ? w->value : 0; }

void fx_set_cb(fx_widget_t *w, fx_cb_t cb, void *ud)
{
    if (!w) return;
    w->cb = cb;
    w->ud = ud;
}

void fx_set_visible(fx_widget_t *w, int vis)
{
    if (!w) return;
    if (vis) w->flags |= FX_F_VISIBLE;
    else w->flags &= (uint8_t)~FX_F_VISIBLE;
    fx_layout();
    fx_repaint();
}

int fx_widget_type(const fx_widget_t *w) { return w ? w->type : FX_W_NONE; }
const char *fx_widget_title(const fx_widget_t *w) { return w ? w->title : NULL; }

/* 回调里读取控件几何 (fx_widget_t 是公共头的不完整类型) */
void fx_widget_rect(const fx_widget_t *w, int *x1, int *y1, int *x2, int *y2)
{
    if (!w) return;
    if (x1) *x1 = w->x1;
    if (y1) *y1 = w->y1;
    if (x2) *x2 = w->x2;
    if (y2) *y2 = w->y2;
}
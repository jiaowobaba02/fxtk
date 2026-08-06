/**
 * app.c — fxtk 综合演示 (demo)
 *
 * 覆盖 fxtk 全部特性:
 *   - 标签页 (tab): 波形页 / 图形页 / 控件页, 点击标签切换
 *   - percent() 百分比布局 (自适应分辨率)
 *   - gird() 网格键盘 + fx_delete 动态重建
 *   - canvas 画布 + 每帧矢量动画 (正弦波, 速度可调)
 *   - slider / progress / checkbox 联动
 *   - 脏区局部重绘 (触摸反馈无闪烁)
 */
#include "fxtk.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "demo";

/* ---------- 状态 ---------- */
static int s_clicks[9] = { 0 };      /* 键盘每个键的累计点击 */
static int s_phase = 0;              /* 波形相位 (画布动画) */
static int s_wave_on = 1;            /* 波形开关 */
static int s_speed = 30;             /* 波形速度 0-100 */
static const char *s_keys[9] = { "1","2","3","4","5","6","7","8","9" };

/* ---------- 回调 ---------- */

/* 网格键盘按键: ud = 键序号 (0-8) */
static void on_key(fx_widget_t *w, void *ud)
{
    int id = (int)(intptr_t)ud;
    s_clicks[id]++;
    char buf[48];
    snprintf(buf, sizeof(buf), "第 %d 键 · 累计 %d 次", id + 1, s_clicks[id]);
    fx_set_title(fx_find("info"), buf);
}

/* 滑条: 更新速度 + 同步进度条 */
static void on_speed(fx_widget_t *w, void *ud)
{
    s_speed = fx_get_value(w);
    fx_set_value(fx_find("speed_bar"), s_speed);
}

/* 复选框: 波形开关 */
static void on_wave(fx_widget_t *w, void *ud)
{
    s_wave_on = fx_get_value(w) != 0;
}

/* 重置: 计数清零 + 相位归零 */
static void on_reset(fx_widget_t *w, void *ud)
{
    for (int i = 0; i < 9; i++)
        s_clicks[i] = 0;
    s_phase = 0;
    fx_set_title(fx_find("info"), "已重置 · 点击数字键试试");
}

/* 重建: 删除键盘网格, 换成 2x2 四色按钮 */
static void on_rebuild(fx_widget_t *w, void *ud)
{
    fx_delete(gird("keys"));
    fx_gird_map(pixel("6,30","280,220"), line(2), row(2), name("keys"));
    fx_button_new(gird("keys",1,1,1,1), title("A"), color(0xF800), call(on_key));
    fx_button_new(gird("keys",1,2,1,2), title("B"), color(0x07E0), call(on_key));
    fx_button_new(gird("keys",2,1,2,1), title("C"), color(0x001F), call(on_key));
    fx_button_new(gird("keys",2,2,2,2), title("D"), color(0xFFE0), call(on_key));
    fx_set_title(fx_find("info"), "键盘已重建为 2x2 (A/B/C/D)");
}

/* 页1 波形画布: 每帧重绘 (仅在波形页激活时) */
static void on_canvas(fx_widget_t *w, void *ud)
{
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;

    /* 背景网格 */
    fx_set_color(0x0841);
    fx_fill_rect(0, 0, cw - 1, ch - 1);
    fx_set_color(0x10A2);
    for (int x = 0; x < cw; x += 16)
        fx_draw_vline(x, 0, ch - 1);
    for (int y = 0; y < ch; y += 16)
        fx_draw_hline(0, cw - 1, y);
    fx_set_color(0x30C4);
    fx_draw_hline(0, cw - 1, ch / 2);

    if (s_wave_on) {
        /* 正弦波 (双相位叠加) */
        int amp = ch / 2 - 8;
        int prev_y = ch / 2;
        fx_set_color(FX_YELLOW);
        for (int x = 0; x < cw; x++) {
            double t = (double)(x + s_phase) * 3.14159265 / 32.0;
            int y = ch / 2 + (int)(amp * sin(t) * 0.7 +
                                   amp * 0.3 * sin(t / 3.0));
            fx_draw_line(x - 1, prev_y, x, y);
            prev_y = y;
        }
        /* 波形上跳动的圆 */
        int px = cw / 2;
        int py = ch / 2 + (int)(amp * sin((double)(px + s_phase) *
                                          3.14159265 / 32.0) * 0.7);
        fx_set_color(FX_RED);
        fx_fill_circle(px, py, 6);
        fx_set_color(FX_WHITE);
        fx_draw_circle(px, py, 8);
    }
    /* 相位推进 (速度由滑条控制) */
    s_phase += s_speed;
    if (s_phase > 4096) s_phase -= 4096;
}

/* 页2 图形画布: 静态矢量图 */
static void on_gfx(fx_widget_t *w, void *ud)
{
    int x1, y1, x2, y2;
    fx_widget_rect(w, &x1, &y1, &x2, &y2);
    int cw = x2 - x1 + 1, ch = y2 - y1 + 1;

    fx_set_color(0x0010);
    fx_fill_rect(0, 0, cw - 1, ch - 1);

    /* 圆 / 椭圆 / 三角形 / 多边形 / 圆角矩形 / 圆弧 */
    fx_set_color(FX_RED);
    fx_fill_circle(50, ch / 2, 36);
    fx_set_color(FX_GREEN);
    fx_fill_ellipse(cw / 2 - 40, ch / 2, 60, 22);
    fx_set_color(FX_YELLOW);
    fx_fill_triangle(cw / 2 + 60, ch / 2 - 30, cw / 2 + 20, ch / 2 + 32,
                     cw / 2 + 100, ch / 2 + 32);
    fx_set_color(FX_CYAN);
    fx_fill_rect_round(cw - 130, 10, cw - 10, 70, 8);
    fx_set_color(FX_MAGENTA);
    {
        int16_t hex[12] = { 10, 10, 60, 24, 60, 74, 10, 88, -40, 74, -40, 24 };
        fx_draw_polygon(hex, 6);
    }
    fx_set_color(FX_WHITE);
    fx_draw_arc(cw - 60, ch - 50, 36, 0, 270);
    fx_draw_text_c(10, ch - 24, "静态矢量图形页", FX_GREEN, 0x0010);
}

/* ---------- 界面构建 ---------- */
static void build_ui(void)
{
    /* 顶栏 */
    fx_label_new(percent("0.03,0.02","0.97,0.06"),
                 title("fxtk 演示 · 标签页"), fgcolor(FX_GREEN));

    /* 标签页容器: 3 页 (title 逗号分隔页名) */
    fx_tab_new(pixel("10,26","470,262"), title("波形,图形,控件"), name("tab"));
    fx_parent(fx_find("tab"));

    /* 页1: 波形 */
    fx_canvas_new(pixel("6,32","444,196"), name("wave_cv"), page(0), anim(1),
                  color(FX_BLACK), call(on_canvas));
    fx_label_new(pixel("6,202","56,218"), page(0), title("速度"), fgcolor(FX_LGRAY));
    fx_slider_new(pixel("60,202","292,218"), name("speed"), page(0),
                  value(s_speed), color(0x07E0), call(on_speed));
    fx_progress_new(pixel("300,204","444,216"), name("speed_bar"), page(0), value(s_speed));
    fx_checkbox_new(pixel("6,224","130,244"), title("波形开关"), name("wave"), page(0),
                    value(1), call(on_wave));

    /* 页2: 图形 */
    fx_canvas_new(pixel("6,32","444,230"), name("gfx_cv"), page(1),
                  color(0x0010), call(on_gfx));

    /* 页3: 控件 (键盘 + 操作) */
    fx_gird_map(pixel("6,32","280,220"), line(3), row(3), name("keys"), page(2));
    for (int i = 0; i < 9; i++) {
        fx_widget_t *b = fx_button_new(
            gird("keys", i / 3 + 1, i % 3 + 1, i / 3 + 1, i % 3 + 1),
            title(s_keys[i]), color(0x1D5C), page(2), call(on_key));
        fx_set_cb(b, on_key, (void *)(intptr_t)i);
    }
    fx_button_new(pixel("292,32","444,82"), page(2), title("重建键盘"), call(on_rebuild));
    fx_button_new(pixel("292,92","444,142"), page(2), title("重置"), call(on_reset));
    fx_label_new(pixel("292,152","444,220"), name("info"), page(2),
                 title("点击数字键试试"), fgcolor(FX_GREEN));

    fx_parent(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "fxtk demo start");

    /* 1. 外设 */
    fx_gt911_init();
    fx_st6201_driver.init();
    fx_st6201_driver.touch_read = fx_gt911_read;

    /* 2. 库 */
    fx_init(&fx_st6201_driver);
    fx_set_bg(0x3186);                  /* 深灰蓝背景 */
    fx_set_touch_debug(1);              /* 左上角显示触摸坐标 (调试) */

    /* 3. 界面 */
    build_ui();
    fx_canvas_enable_buf(fx_find("wave_cv"));   /* 波形画布离屏缓冲: 无闪烁 */

    /* 4. 主循环 */
    while (1) {
        fx_poll();
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

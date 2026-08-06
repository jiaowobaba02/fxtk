# fxtk API 参考

所有 API 都在 `fxtk.h`, 用户程序只需 `#include "fxtk.h"`。
颜色统一为 RGB565 (`uint16_t`), 用 `FX_RGB(r,g,b)` 或预设宏构造。

---

## 1. 系统

```c
void fx_init(const fx_driver_t *drv);   // 库初始化 (驱动已 init 后调用)
void fx_poll(void);                      // 主循环: 触摸事件 + 整帧渲染
uint16_t fx_width(void);                 // 屏幕宽 (来自驱动)
uint16_t fx_height(void);                // 屏幕高
void fx_set_autorepaint(int on);         // 1=每轮 poll 整帧重绘 (默认 1)
void fx_set_bg(fx_color_t c);            // 清屏背景色 (默认 FX_BLACK)
void fx_repaint(void);                   // 请求下一轮重绘 (autorepaint=0 时用)
```

典型主循环:

```c
while (1) {
    fx_poll();
    vTaskDelay(pdMS_TO_TICKS(16));
}
```

---

## 2. 属性构造器 (控件创建参数)

| 构造器 | 参数 | 说明 |
|---|---|---|
| `pixel(a, b)` | `"x1,y1"`, `"x2,y2"` | 像素矩形, 相对父容器 |
| `percent(a, b)` | `"0.1,0.1"`, `"0.9,0.9"` | 百分比矩形 (0.0~1.0), 相对父容器 |
| `gird(name, r1,c1,r2,c2)` | 网格名 + 行列范围 | 网格内定位 (1 起) |
| `gird(name)` | 仅网格名 | 仅用于 `fx_delete` 按名查找 |
| `title(s)` / `text(s)` | 字符串 | 标题文本 (必须是静态字符串) |
| `name(s)` | 字符串 | 控件唯一 ID |
| `call(fn)` | 回调指针 | 点击 / 滑条变更 / 画布重绘 |
| `line(n)` | 整数 | 网格行数 |
| `row(n)` | 整数 | 网格列数 |
| `color(c)` | RGB565 | 背景色 |
| `fgcolor(c)` | RGB565 | 前景/文字色 |
| `border(n)` | 整数 | 边框宽度 |
| `radius(n)` | 整数 | 圆角半径 |
| `value(n)` | 整数 | 滑条/进度/复选框初值 |
| `fx_wptr(w)` | 控件指针 | 传指针 (配合 `fx_delete`) |

`gird` 与 `grid` 拼写等价。属性顺序任意, 位置属性三选一
(pixel / percent / gird 取最后一个生效)。

---

## 3. 控件

### 创建

```c
fx_widget_t *fx_button_new(...);      // 按钮
fx_widget_t *fx_label_new(...);       // 文本
fx_widget_t *fx_gird_map(...);        // 网格容器 (grid_map 别名)
fx_widget_t *fx_canvas_new(...);      // 画布
fx_widget_t *fx_slider_new(...);      // 滑条
fx_widget_t *fx_progress_new(...);    // 进度条
fx_widget_t *fx_checkbox_new(...);    // 复选框
fx_widget_t *fx_panel_new(...);       // 面板
```

失败 (控件池满) 返回 NULL。

### 查找 / 删除

```c
fx_widget_t *fx_find(const char *name);       // 按名字查找
void fx_delete(gird("gird"));                 // 按名字删除 (含子控件)
void fx_delete(fx_wptr(w));                   // 按指针删除
```

### 属性读写

```c
void fx_set_title(fx_widget_t *w, const char *s);
void fx_set_value(fx_widget_t *w, int v);     // 0-100
int  fx_get_value(const fx_widget_t *w);
void fx_set_cb(fx_widget_t *w, fx_cb_t cb, void *ud);
void fx_set_visible(fx_widget_t *w, int vis);
int  fx_widget_type(const fx_widget_t *w);    // FX_W_* 枚举
const char *fx_widget_title(const fx_widget_t *w);
void fx_widget_rect(const fx_widget_t *w, int *x1, int *y1, int *x2, int *y2);
```

> `fx_widget_t` 在公共头里是不完整类型, 回调里读取几何用 `fx_widget_rect`。

### 回调

```c
typedef void (*fx_cb_t)(fx_widget_t *w, void *ud);
```

| 控件 | 回调时机 | ud |
|---|---|---|
| button / checkbox | 按下并在控件内抬起 | 创建时未提供, 用 `fx_set_cb` 设置 |
| slider | 拖动中数值变化 | 同上 |
| canvas | 每帧重绘 (裁剪已设到画布区) | 同上 |

---

## 4. 布局

```c
void fx_layout(void);    // 全量重算控件矩形 (创建/删除/分辨率变化后自动调用)
```

- `pixel`: 坐标 = 父容器左上 + 像素偏移 (根容器 = 全屏 (0,0))
- `percent`: 坐标 = 父容器内按千分比换算, 分辨率变化自动适配
- `gird`: 按网格行列均分, 子控件矩形 = 覆盖的行列范围
  - `line(n)` = 行数, `row(n)` = 列数, 行列从 1 起
  - 覆盖范围 `(r1,c1)..(r2,c2)`, 允许跨多行多列

---

## 5. 矢量绘制 (立即模式 / 帧模式)

```c
void fx_set_color(fx_color_t c);          // 当前绘制色
void fx_set_clip(int x1, int y1, int x2, int y2);  // 像素裁剪
void fx_reset_clip(void);

void fx_draw_pixel(int x, int y);
void fx_draw_hline(int x1, int x2, int y);
void fx_draw_vline(int x, int y1, int y2);
void fx_draw_line(int x1, int y1, int x2, int y2);       // Bresenham
void fx_draw_rect(int x1, int y1, int x2, int y2);       // 边框
void fx_fill_rect(int x1, int y1, int x2, int y2);
void fx_draw_rect_round(int x1, int y1, int x2, int y2, int r);
void fx_fill_rect_round(int x1, int y1, int x2, int y2, int r);
void fx_draw_circle(int cx, int cy, int r);
void fx_fill_circle(int cx, int cy, int r);
void fx_draw_ellipse(int cx, int cy, int rx, int ry);
void fx_fill_ellipse(int cx, int cy, int rx, int ry);
void fx_draw_arc(int cx, int cy, int r, int a1, int a2); // 0-360°, 逆时针
void fx_fill_arc(int cx, int cy, int r, int a1, int a2); // 放射线近似
void fx_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3);
void fx_fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3);
void fx_draw_polygon(const int16_t *pts, int n);         // n 顶点 x0,y0,...
void fx_fill_polygon(const int16_t *pts, int n);         // 扫描线填充
```

### 文本

```c
void fx_draw_text(int x, int y, const char *s);          // 当前色
void fx_draw_text_c(int x, int y, const char *s, fx_color_t fg, fx_color_t bg);
int  fx_text_width(const char *s);                       // 像素宽
```

- 中文 16px 微软雅黑灰度 (抗锯齿) + ASCII Arial 16px 混排
- `\n` 换行, 行高 18px, 中文前进 17px
- 缺字画方框 (字库未收录的字符)

### 画布控件内绘制

```c
void fx_canvas_begin(fx_widget_t *cv);   // clip 设到画布区域
// ... fx_draw_* 坐标相对画布左上角 ...
void fx_canvas_end(void);                // 恢复 clip
```

更推荐的做法: 画布创建时用 `call(draw_fn)`, 每帧自动以画布为裁剪区调用,
无需手动 begin/end。

---

## 6. 帧渲染 (垂直同步式整帧)

```c
void fx_frame_begin(void);
void fx_frame_end(void);
```

- 帧模式下所有绘制进 band 缓冲 (16 行/带), 整帧逐带刷出, 无撕裂
- `fx_poll()` 内部自动使用帧模式 (autorepaint=1 时)
- 不调用 begin/end 的绘制是立即模式 (行缓冲直写), 适合局部小改动

---

## 7. 驱动抽象

```c
typedef struct {
    uint16_t width, height;                      // 屏幕分辨率
    int  (*init)(void);                          // 初始化, 0=成功
    void (*set_window)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void (*push_pixels)(const uint16_t *px, uint32_t n);  // RGB565 大端
    void (*hold_begin)(void);                    // 连续事务 (可 NULL)
    void (*hold_end)(void);
    int  (*touch_read)(int *x, int *y, int *pressed);    // 每轮状态, 0=无
} fx_driver_t;
```

内置驱动:

```c
extern fx_driver_t fx_st6201_driver;   // 480x272 ST6201 SPI (ETSP32 板)
int fx_gt911_init(void);               // GT911 触摸初始化, 0=成功
int fx_gt911_read(int *x, int *y, int *pressed);
```

换屏步骤:
1. 实现 `fx_driver_t` 四个函数 (init/set_window/push_pixels/hold)
2. 填 `width/height`
3. `fx_init(&你的驱动)` — UI 用 percent 布局自动适配, 不用改代码

---

## 8. 事件流

```
驱动 touch_read (每轮) → fx_poll 边沿检测
  ├─ 按下 → hit_test (子控件优先) → FX_F_PRESSED 置位
  ├─ 拖动 → 若按下控件是 slider → value 更新 + 回调
  └─ 抬起 → 若仍在同一控件 → 回调 cb(w, ud)
```

容器 (gird/panel/根) 不响应点击; 点击命中子控件优先。

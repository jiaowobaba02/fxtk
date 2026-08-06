# fxtk — FX Tool Kit (ESP32 图形界面库)

一个面向 ESP32 的极简图形界面库。语法比 GTK 简单：控件用"属性构造器"声明式
创建，一次调用完成"位置 + 外观 + 行为"的定义。

```c
/* 在像素 (20,20)..(100,40) 建一个按钮, 名为 button, 按下调用 on_button */
fx_button_new(pixel("20,20","100,40"), title("button"), call(on_button));

/* 在像素 (20,20)..(460,252) 建一个 5 行 3 列的控件表 */
fx_gird_map(pixel("20,20","460,252"), line(5), row(3), name("gird"));

/* 在 gird 表里建按钮: 覆盖 第1行第2列 .. 第1行第4列 */
fx_button_new(gird("gird", 1, 2, 1, 4), title("button"), call(on_button));

/* 百分比定位 (自适应分辨率): 10%..90% 的屏幕区域 */
fx_gird_map(percent("0.1,0.1","0.9,0.9"), line(5), row(3), name("gird"));

/* 删除名为 gird 的控件表 */
fx_delete(gird("gird"));
```

## 特性

- **控件**: 按钮 / 文本 / 网格容器 / 画布 / 滑条 / 进度条 / 复选框 / 面板
- **布局**: `pixel()` 绝对像素 · `percent()` 百分比 (自适应分辨率) · `gird()` 网格
- **矢量绘制**: 线 / 矩形 / 圆角矩形 / 圆 / 椭圆 / 圆弧 / 扇形 / 三角形 / 多边形 / 文本
- **帧渲染**: banded 帧缓冲, 整帧逐带刷新 (垂直同步式, 无撕裂)
- **驱动抽象**: 换屏幕只换驱动结构体, UI 用 percent 布局自动适配
- **零 malloc**: 控件静态池分配, 无内存碎片, 长期运行稳定

## 快速开始

### 1. 构建

```bash
cd fxtk
. ~/esp/esp-idf/export.sh          # 按你的 IDF 路径
idf.py set-target esp32           # 首次
idf.py build
```

### 2. 烧录 (会覆盖设备上现有固件!)

```bash
idf.py -p /dev/ttyUSB0 flash
```

### 3. 最小程序

```c
#include "fxtk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void on_btn(fx_widget_t *w, void *ud) { ... }

void app_main(void)
{
    fx_gt911_init();                      /* 触摸 */
    fx_st6201_driver.init();              /* 屏幕 */
    fx_st6201_driver.touch_read = fx_gt911_read;
    fx_init(&fx_st6201_driver);           /* 库初始化 */

    fx_button_new(pixel("20,20","140,60"), title("点我"), call(on_btn));

    while (1) {
        fx_poll();                        /* 触摸事件 + 整帧渲染 */
        vTaskDelay(pdMS_TO_TICKS(16));    /* ~60fps 节奏 */
    }
}
```

## 语法速查

### 控件创建 (属性声明式)

| 函数 | 说明 |
|---|---|
| `fx_button_new(attrs...)` | 按钮, 点击回调 |
| `fx_label_new(attrs...)` | 文本标签 |
| `fx_gird_map(attrs...)` | 网格容器 (5 行 3 列用 `line(5), row(3)`) |
| `fx_canvas_new(attrs...)` | 画布, `call()` 为每帧重绘回调 |
| `fx_slider_new(attrs...)` | 滑条 (0-100, 可拖动) |
| `fx_progress_new(attrs...)` | 进度条 (value 0-100) |
| `fx_checkbox_new(attrs...)` | 复选框 |
| `fx_panel_new(attrs...)` | 面板容器 (子控件坐标相对它) |
| `fx_delete(gird("name"))` | 按名字删除控件 (含子控件) |
| `fx_find("name")` | 按名字查找控件 |

### 属性

| 属性 | 说明 |
|---|---|
| `pixel("x1,y1","x2,y2")` | 像素矩形 (相对父容器) |
| `percent("0.1,0.1","0.9,0.9")` | 百分比矩形 (0.0-1.0) |
| `gird("name", r1,c1,r2,c2)` | 网格内定位 (1 起, 行/列范围) |
| `title("文本")` / `text("文本")` | 标题 |
| `name("id")` | 控件名字 (唯一, 用于查找/删除) |
| `call(fn)` | 回调函数指针 (点击 / 滑条变更 / 画布重绘) |
| `color(c)` / `fgcolor(c)` | 背景色 / 前景色 (RGB565) |
| `border(n)` / `radius(n)` | 边框宽 / 圆角半径 |
| `value(n)` | 滑条/进度条/复选框初值 |
| `line(n)` / `row(n)` | 网格行数 / 列数 |

> 注意: C 语言回调传**函数指针**, 写 `call(on_button)` (不带括号)。
> 回调签名固定: `void fn(fx_widget_t *w, void *ud)`。
> `gird` 是拼写兼容的原始名, `grid` 也可用。

### 颜色

`FX_RGB(r,g,b)` 或预设 `FX_RED / FX_GREEN / FX_BLUE / FX_WHITE / FX_BLACK /
FX_YELLOW / FX_CYAN / FX_MAGENTA / FX_GRAY / FX_LGRAY` (RGB565)。

## 文档

- [docs/guide.md](docs/guide.md) — 上手教程 (从接线到完整界面)
- [docs/api.md](docs/api.md) — 完整 API 参考
- [docs/internals.md](docs/internals.md) — 架构原理 + 移植经验 (屏幕/触摸踩坑)

## 文件结构

```
fxtk/
├── CMakeLists.txt          工程构建
├── partitions.csv          分区表 (1MB app)
├── sdkconfig.defaults
├── main/
│   ├── app.c               示例应用 (所有语法的演示)
│   ├── fxtk.h              公共 API 头 (用户只须包含它)
│   ├── fxtk.c              核心: 属性解析/控件树/布局/事件/帧渲染
│   ├── fxtk_draw.c         矢量绘制 + banded 帧缓冲
│   ├── fxtk_font.c         中英混排文本 (灰度抗锯齿)
│   ├── fxtk_widgets.c      控件绘制 (按钮/滑条/...)
│   ├── fxtk_st6201.c       ST6201 SPI 屏幕驱动 (480x272)
│   ├── fxtk_touch.c        GT911 触摸驱动 (I2C)
│   ├── fxtk_internal.h     内部结构 (用户不需要)
│   ├── cn_gray.c/h         16px 中文灰度字库 (7445 汉字)
│   └── ascii_gray16.c/h    16px Arial 灰度字库
└── docs/                   文档
```

## 已知限制

- 控件池固定 64 个 (编译期 `FX_MAX_WIDGETS`, 可改)
- 文本固定 16px (中英混排), 暂无多字号
- 帧率受 SPI 带宽限制 (~12fps 全屏刷新, 局部更新用立即模式更快)
- 不要在回调里删除触发回调的控件自身

## 版权

字库与驱动移植自 esp32-tester 项目 (EYA ETSP32 板)。

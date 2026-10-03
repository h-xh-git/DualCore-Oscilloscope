# STM32F407_ProjectTemplate — 实时核固件

这是 **DualCore-Oscilloscope 双核示波器**的**实时核**：
STM32F407VET6 负责 ADC 采集 → 边沿触发 → 参数测量 → TFT 显示 → 按键/编码器交互，
再把波形通过 USART2（460800 8N1）交给 ESP32-S3 的联网核。

> 项目主页面：[../README.md](../README.md) · 第三方组件与许可：[../NOTICE.md](../NOTICE.md)

---

## 编译与烧录

1. Keil MDK-ARM **V5** 打开 `project/MDK(V5)/Project.uvprojx`
2. 确认器件为 `STM32F407VETx`（需安装 `STM32F4xx_DFP` 器件包）
3. `F7` 编译，`F8` 用 ST-Link / J-Link 下载

`libraries/` 下的 ST 标准外设库与 CMSIS **已随仓库提供**，无需另外下载。

---

## 目录结构

```
STM32F407_ProjectTemplate/
├── app/main.c             主循环与状态机
├── board/                 板级初始化、delay_us/delay_ms
├── module/                stm32f4xx_conf.h、stm32f4xx_it.c（中断向量）
├── bsp/
│   ├── adc/               双 ADC 规则同步 + DMA2 循环双缓冲采集引擎
│   ├── scope/             scope_trigger_find() / scope_measure() 纯整数测量
│   ├── proto/             与 ESP32 的帧协议（STM32 侧，CRC-8）
│   ├── uart/  uart2/      USART1（调试）/ USART2（接 ESP32）
│   ├── lcd/               ST7789 2.4" TFT，硬件 SPI1
│   ├── oled/              SSD1306 0.96" 128x64，硬件 I2C1
│   ├── key/               4 个按键
│   ├── encoder/           EC11 旋转编码器（TIM8 编码器模式）
│   ├── buzzer/            蜂鸣器
│   └── ui/                UI 绘制 + ui_font_cn14.h（生成的 1bpp 中文字模）
├── project/MDK(V5)/       Keil 工程
├── libraries/             ST 标准外设库 + CMSIS（只读，请勿修改）
└── clean.bat              清掉 Objects/Listings 等构建产物
```

---

## 关键引脚

| 功能 | 引脚 |
|---|---|
| CH1 输入 | **PA1**（ADC1_IN1，主 ADC，值在 `ADC->CDR[15:0]`） |
| CH2 输入 | **PA0**（ADC2_IN0，从 ADC，值在 `ADC->CDR[31:16]`） |
| TFT ST7789 SPI1 | SCK=PA5, MOSI=PA7, CS=PA4, DC=PB0, RST=PB1, BLK=PB10 |
| OLED SSD1306 I2C1 | SCL=PB6, SDA=PB7 |
| 按键 KEY1–4 | PC0 / PC1 / PC2 / PC3（内部上拉，按下为低） |
| 编码器 | A=PC6 (TIM8_CH1), B=PC7 (TIM8_CH2), SW=PC8 |
| 蜂鸣器 | PB8 |
| → ESP32 | USART2：TX=PA2, RX=PA3（AF7） |

> **模拟输入不得超过 3.3 V**，否则永久损坏 MCU。

---

## 关键常量

| 常量 | 值 | 位置 |
|---|---|---|
| `ADC_HALF_SIZE` / `ADC_BUF_SIZE` | 256 / 512 | `bsp/adc/bsp_adc.h` |
| `RING_SIZE` / `RING_MASK` | 2048 / 2047 | `bsp/scope/bsp_scope.h` |
| `WAVE_N` | 300 | `bsp/scope/bsp_scope.h` |
| `TRIG_PRE` / `TRIG_POST` | 64 / 236 | `bsp/scope/bsp_scope.h` |
| `TRIG_SEARCH_MS` / `TRIG_SEARCH_MAX` | 10 / 1748 | `bsp/scope/bsp_scope.h` |
| 采样率档位 | 13 档，100 Hz – 1 MHz | `app/main.c` 的 `k_rates[13]` |
| 默认采样率档 | `RATE_DEF_IDX = 6`（10 kHz） | `app/main.c` |
| `PROTO_MAX_PAYLOAD` | 2048 | `bsp/proto/bsp_proto.h` |
| `LCD_W` / `LCD_H` | 320 / 240（横屏） | `bsp/lcd/bsp_lcd.h` |

采样率是算出来的，不是查表查出来的：

```
rate = 84 MHz / ((psc + 1) * (arr + 1))        // TIM2 时钟 84 MHz
```

触发搜索窗按**时间**换算而不是固定点数，低频档位才不会抓不到边沿：

```c
sn = (adc_get_rate() / 1000) * TRIG_SEARCH_MS;   // 10 ms 的窗
if (sn < 200)   sn = 200;                        // 窗口必须随时基自适应，
if (sn > 1748)  sn = 1748;                       // 固定点数在低采样率下会抓不到边沿
```

---

## 开发约定

1. **`libraries/` 只读。** 不要修改 ST 官方文件。
2. **新建 STM32 源文件必须是纯 ASCII、无 BOM。** Keil/ARMCC 对非 ASCII 源文件容易出
   意外报错。需要中文就用 `tools/gen_ui_text.py` 那种八进制转义的 UTF-8 字面量。
3. **部分已有文件是 GB2312 编码**（例如 `app/main.c` 的中文注释）。**不要整体改写**，
   用定点替换（`edit`）而不是整文件重写，否则会把编码搞坏。
4. **新增 `.c` 文件要手动加进 Keil 工程分组**；新增 `bsp/xxx/` 目录要加进 IncludePaths。
   这两步 Keil 不会自动做。
5. **改 UI 先跑离线预览**，比烧板子快得多：

   ```bash
   python ../tools/preview_ui.py ui_preview.png   # 画出图并断言版面几何
   ```

6. `main.c` 里的 `ms` **不是真毫秒**，只是主循环里 `ms++` 的伪时钟，别拿它当时间用。
7. 状态栏脏标记和画布脏标记是**分开的**（`s_ui_dirty` / `s_canvas_dirty`），
   改刷新逻辑时别把它们合并，否则波形刷新会变慢。

---

## 许可

本目录下的原创代码以 **GPL-3.0** 发布；`libraries/` 与模板骨架的来源与许可见
[../NOTICE.md](../NOTICE.md)。

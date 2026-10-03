# NOTICE — 第三方组件、来源与字体说明

本仓库（**DualCore-Oscilloscope**）整体以 **GPL-3.0** 发布（见 [LICENSE](LICENSE)）。
但其中包含或派生自下列第三方成果。再分发本仓库时请一并保留本文件。

---

## 1. STMicroelectronics STM32F4xx 固件库

目录：`STM32F407_ProjectTemplate/libraries/`

| 子目录 | 内容 | 许可 |
|---|---|---|
| `CMSIS/Device/ST/STM32F4xx/` | STM32F4xx 设备头文件、`system_stm32f4xx.c`、启动文件 | **BSD-3-Clause**（见该目录 `LICENSE.txt`） |
| `CMSIS/Include/` | ARM CMSIS-Core 头文件 | Apache-2.0（ARM Limited）※ 该目录未随附 LICENSE 文件 |
| `STM32F4xx_StdPeriph_Driver/` | ST 标准外设库（SPL）驱动 | **SLA0044 Rev5 / February 2018**（"Ultimate Liberty Software License Agreement"，见该目录 `LICENSE.txt`，BSD-3-Clause 类条款） |

版权归 STMicroelectronics 所有。

> 本仓库**未修改** `libraries/` 下的任何文件，各文件头部的版权与许可声明均原样保留。

---

## 2. 立创开发板（LCKFB）STM32F407 工程模板

`STM32F407_ProjectTemplate/board/board.c`、`board/board.h` 等工程骨架文件
头部保留有原始署名：`LCKFB-LP`，`2024-03-07`，来源 `www.lckfb.com`（立创开发板）。

本项目的工程骨架（目录组织、`stm32f4xx_conf.h`、延时函数、中断向量等）建立在该
开源模板之上；示波器相关的采集、触发、测量、UI、协议代码为本项目原创。

---

## 3. Arduino-ESP32 核心 / Espressif ESP-IDF

`esp32/` 使用 **PlatformIO** + `platform = espressif32@6.9.0`，`framework = arduino`。

- 版权：Espressif Systems (Shanghai) CO LTD
- 许可：Arduino-ESP32 核心为 **LGPL-2.1**；ESP-IDF 为 Apache-2.0
- **上述组件不随本仓库分发**，由 PlatformIO 在首次构建时自动下载到 `~/.platformio/`

ESP32-S3 芯片技术资料版权归 Espressif Systems 所有。
（随项目模板分发的 `ESP32S3数据手册.pdf` **未**包含在本仓库中。）

---

## 4. Web 前端

`esp32/lib/osc_web/osc_web.cpp` 中的网页（HTML / CSS / JS）为本项目原创，
**不引用任何 CDN 或第三方前端库**，全部内联在固件里，因此完全离线可用。

---

## 5. 中文字模 —— 请注意字体版权 ⚠️

本仓库包含两个中文点阵字模文件：

| 文件 | 格式 | 生成方式 |
|---|---|---|
| `font_chinese_14.h` | LVGL v8 `fmt_txt`，14 px，4bpp | 由 `simsun.ttf` 转换而来（文件头注释：`font:simsun.ttf, size:14, bpp:4`） |
| `STM32F407_ProjectTemplate/bsp/ui/ui_font_cn14.h` | 1bpp 行优先点阵，stride `(w+7)/8`，MSB 在左，230 个字形 | 由 `tools/gen_ui_font.py` 从上面那个文件解码生成 |

**问题所在**：这两个文件里的字形来自微软 / 中易（ZhongYi）的 **宋体 SimSun**。
SimSun 是随 Microsoft Windows 分发的**专有字体**，其许可**不允许**再分发字体文件本身。

**本仓库的做法：**

- 仓库中**不含** `simsun.ttf`，也不含任何 `.ttf` / `.otf` 字体文件；
- 只包含为嵌入式屏显服务的、由该字体**渲染后**的低位深点阵数据（且已裁剪为 UI 实际用到的字符子集）。

**但这仍属于法律灰区。** 若你所在司法辖区对该等派生数据的再分发有更严格的要求，
或你打算把本项目用于商业分发，请改用**开源中文字体**重新生成：

```bash
# 1. 用开源字体（如 思源黑体 / Noto Sans CJK / 文泉驿）转出 LVGL 格式字模，
#    替换仓库根目录的 font_chinese_14.h（保持同样的变量名与 4bpp fmt_txt 格式）
# 2. 重新生成 STM32 用的 1bpp 字模
python tools/gen_ui_font.py
# 3. 如果改动了用到的汉字集合，同步更新并重新生成标签表
python tools/gen_ui_text.py
# 4. 验证版面没有被新字模撑坏
python tools/preview_ui.py ui_preview.png
```

对应的开源字体许可通常是 **SIL Open Font License 1.1（OFL）**，允许自由再分发与嵌入。

---

## 6. 本项目原创部分

以下内容为本项目原创，采用 GPL-3.0：

- `STM32F407_ProjectTemplate/{app,bsp,module}/` 下除模板骨架外的全部示波器逻辑
- `esp32/src/`、`esp32/lib/{osc_link,osc_proto,osc_web,osc_wifi}/`
- `tools/` 下四个 Python 脚本
- 仓库根目录的 `ui_preview.png`（由 `tools/preview_ui.py` 生成）
- `docs/` 下的中文文档（`使用指南.md`）与根目录的 `README.md` / `CHANGELOG.md`

---

## 7. 汇总

| 组件 | 版权方 | 许可 | 是否随仓库分发 |
|---|---|---|---|
| STM32F4xx Device (CMSIS) | STMicroelectronics | BSD-3-Clause | ✅ 是 |
| ARM CMSIS-Core | ARM Limited | Apache-2.0 | ✅ 是 |
| STM32F4xx StdPeriph Driver | STMicroelectronics | SLA0044 Rev5 | ✅ 是 |
| LCKFB 工程模板骨架 | 立创开发板 | 见文件头声明 | ✅ 是 |
| Arduino-ESP32 核心 | Espressif Systems | LGPL-2.1 | ❌ 否（PlatformIO 构建时下载） |
| ESP32-S3 数据手册 PDF | Espressif Systems | 专有文档 | ❌ 否（已排除） |
| 宋体 SimSun 派生字模 | 微软 / 中易 | 专有（法律灰区） | ⚠️ 是 |

---

*本文件是工程实践层面的说明，不构成法律意见。正式商用分发前请咨询专业人士。*

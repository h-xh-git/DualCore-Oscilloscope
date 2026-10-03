# Changelog

本项目所有值得记录的变更都会写在这里。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

---

## [Unreleased]

后续开发方向见 [README 的改进方案](README.md#改进方案)。
计划中的重点：模拟前端（AFE）、可变记录深度、FFT 频谱、SD 卡存储、开源自适应字体。

---

## [0.1.0] — 2026-10-03

首个公开版本。功能完整的双通道数字存储示波器。

### Added — 实时核（STM32F407VET6）

- ADC1 + ADC2 **规则同步双重模式**采集，通过 DMA2 Stream0 从 `ADC->CDR` 一次取回双通道
  （低 16 位 CH1 / 高 16 位 CH2），两路严格对齐
- TIM2 TRGO 硬件定时触发，采样率由 `adc_tim_div()` 按
  `rate = 84 MHz / ((psc+1) * (arr+1))` 反解，**13 档准确采样率：100 Hz ~ 1 MSa/s**
- 512 字 DMA 双缓冲 → 2048 点环形缓冲的两级解耦，采集不因显示而丢点
- 边沿触发，支持**上升沿 / 下降沿**，触发通道可选 CH1 / CH2
- 触发模式：**自动 / 常态 / 单次**
- **自适应触发搜索窗**：按时间（默认 10 ms）换算采样点数并夹在 `[200, 1748]`，
  各时基档都能稳定捕获边沿；触发点固定在距画面左边缘 64 点处（前 64 / 后 236）
- **纯整数测量**：Vpp、Vmax、Vmin、Vavg、Vrms（自实现整数开方 `scope_isqrt()`）、
  频率、周期、占空比、脉宽；过零检测带迟滞，最多统计 16 个过零点
- 4 档垂直缩放（V/div），通道可独立显示 / 隐藏
- **无 RTOS 裸机主循环**，零阻塞等待，不依赖 FPU

### Added — 显示与人机交互

- **2.4″ ST7789 TFT 320×240 横屏 UI**（`MADCTL` 旋转），RGB565，硬件 SPI1
- 三区版面：状态栏（时基 / 运行状态 / 采样率 + 运行指示灯）、
  300×150 波形画布（10×6 网格 + 触发电平虚线 + 右侧 20 px 触发条）、两张通道测量卡片
- **不做整屏帧缓冲**，直接向面板写像素；状态栏、画布、测量卡片、菜单行分层脏标记，
  只重绘真正变化的区域
- **0.96″ SSD1306 OLED 128×64** 状态副屏（运行状态 / 时基 / 触发状态）
- 4 个按键 + **EC11 旋转编码器**（TIM8 编码器模式）与 10 项菜单
- 有源蜂鸣器操作提示音

### Added — 连接核（ESP32-S3）

- **内嵌网页示波器**（单页 HTML 存在 Flash 中，不依赖任何 CDN，断网可用）
- 网页实时波形（10 Hz）、测量卡片（2 Hz）、完整控制条
  （运行 / 停止、13 档时基、触发模式 / 边沿 / 通道、电平滑块）
- 在波形区**鼠标或手指拖动即可设置触发电平**（拖动节流 120 ms，松手立即下发）
- **CSV 导出**：300 行原始采样数据，可直接用 Excel / Python 分析
- **网页配网**：SSID / 密码写入 NVS，掉电不丢；密码永不回传，仅返回 `has_pass` 布尔量
- 未配网时自动开启 `Oscilloscope-Setup` 热点（`192.168.4.1`）兜底，无需重新烧录即可改网
- **mDNS**：浏览器直接访问 `http://oscilloscope.local`
- **ArduinoOTA** 无线固件升级
- 页面切到后台自动停止轮询，回到前台自动恢复

### Added — 双核桥接

- 自定义帧协议：`[0xAA][0x55][type][len_lo][len_hi][payload…][crc8]`，
  跑在 **460800 8N1** 上，最大负载 2048 B
- **CRC-8（多项式 `0x07`）** 覆盖 `type + len + payload`
- STM32 侧发送环满时**丢帧而不阻塞**（`proto_drops()` 计数），实时性优先于完整性
- ESP32 侧协议实现与 STM32 侧镜像，便于对照维护

### Added — HTTP 接口

| 路由 | 方法 | 作用 |
|---|---|---|
| `/` | GET | 网页示波器主页面 |
| `/stat` | GET | 运行状态 |
| `/meas` | GET | 当前测量值（JSON） |
| `/wave` | GET | 波形数据（约 1200 B） |
| `/csv` | GET | 波形 CSV 下载 |
| `/cmd` | GET/POST | 下发控制命令 |
| `/wifi` | GET/POST | 配网页面 / 提交凭据 |

### Added — 工具与工程

- `tools/preview_ui.py`：用**真实**字模位图、版面常量与标签字符串离线渲染 TFT 界面，
  并附带版面几何断言 —— 不接硬件即可验证 UI 改动
- `tools/gen_ui_font.py` / `tools/gen_ui_text.py`：生成 STM32 用的 1bpp 中文字模与
  纯 ASCII 标签表（非 ASCII 字符转为八进制转义 UTF-8 字面量，
  使 C 源文件保持纯 ASCII，ARMCC 不依赖 locale 即可编译）
- `tools/show_font.py`：在终端用 ASCII art 检查字模点阵
- `STM32F407_ProjectTemplate/clean.bat`：清理 Keil 构建产物
- 全部 `tools/` 脚本**只依赖 Python 标准库**

### Security

- **固件不包含任何 Wi-Fi 凭据**：`WIFI_SSID_DEFAULT` / `WIFI_PASS_DEFAULT` 默认留空，
  首次上电直接进入自带热点配网流程，不存在硬编码密码泄漏到公开仓库的风险

### Known Limitations

- 模拟输入端为 **0 ~ 3.3 V 直采，没有模拟前端（AFE）**，无衰减、无 AC/DC 耦合、
  无过压保护、无探头补偿；超压会永久损坏 MCU
- 记录深度固定 2048 点，高采样率下屏幕覆盖的时间窗口很短
- 只有边沿触发，没有脉宽 / 斜率 / 视频触发
- 测量为整数运算，低采样率下频率分辨率有限，且没有 FFT / 频谱功能
- 波形不落盘，只能通过网页导出 CSV，无 SD 卡存储与回放
- 中文字模字形源自宋体（SimSun），存在分发授权灰区，详见 [NOTICE.md](NOTICE.md) 第 5 节

---

[Unreleased]: https://github.com/h-xh-git/DualCore-Oscilloscope/compare/v0.1...HEAD
[0.1.0]: https://github.com/h-xh-git/DualCore-Oscilloscope/releases/tag/v0.1

# EBplus

基于 [openEB](https://github.com/prophesee-ai/openeb) 事件相机的专业 Qt 6 桌面应用 —— 实时可视化、相机控制、录制回放、标定与数据导出，构建于开源 OpenEB SDK v5.2.0 之上。

> **什么是事件相机？** 与传统帧相机不同，事件相机（如 Prophesee/CenturyArks 的产品）输出异步的逐像素亮度变化——"事件"——具有微秒级时间分辨率、高动态范围和低功耗。本应用提供完整的事件流可视化、录制与处理桌面工作流。

![主界面](pic/1.0.0.png)

---

## 目录

- [功能特性](#功能特性)
- [快速开始](#快速开始)
- [截图](#截图)
- [目录结构](#目录结构)
- [环境要求](#环境要求)
- [编译](#编译)
- [运行](#运行)
- [相机厂商配置](#相机厂商配置)
- [常见问题](#常见问题)
- [已知问题与反馈](#已知问题与反馈)
- [快捷键](#快捷键)
- [开发路线图](#开发路线图)
- [许可证](#许可证)

---

## 功能特性

### 实时事件显示
- **OpenGL 加速渲染**（GLSL 3.30 core profile，letterbox 视口）
- 7 种帧模式：Diff、Integration、Histogram、Time Decay、Contrast Map、Periodic、On-Demand
- 4 种色彩主题：Dark、Light、CoolWarm、Gray
- 可调累积时间（1–1000 ms）
- 实时统计：事件率、峰值率、ON/OFF 比、FPS、时间戳

### 相机控制面板
- **Biases 面板** — 动态枚举所有 HAL bias，滑块 + 精确输入 + Reset，保存/加载 `.bias` 文件
- **ROI 面板** — 矩形 ROI/RONI（`I_ROI`），显示区拖拽选区，应用/清除
- **ESP 面板** — Anti-Flicker（模式/频带/预设/占空比/阈值）、Trail Filter（类型/阈值）、ERC（目标事件率）
- **Trigger 面板** — Trigger In（逐通道启用）+ Trigger Out（启用/周期/占空比）

所有面板在设备不支持对应 HAL facility 时优雅降级（如文件回放时四个面板自动禁用）。

### 录制与回放
- **RAW 录制** — 实时相机流录制，带实时缓冲刷新
- **文件回放** — 速度控制、跳转、暂停/恢复、位置追踪
- **文件裁剪** — 从事件文件中提取时间段

### 数据导出与转换
- RAW、HDF5、CSV 格式互转
- 事件数据导出为 AVI 视频（via CDFrameGenerator + CvVideoRecorder）
- 可配置 FPS、累积时间、画质、色彩模式

### 事件预处理
- 8 级滤波链：ROI Filter、Polarity Filter、Polarity Invert、Flip X、Flip Y、Rotate、Transpose、Rescale
- 从菜单栏或预处理面板切换各级开关
- 线程安全滤波链（mutex 保护）

### 标定
- **内参标定向导** — 棋盘格采集、角点检测、参数优化
- **外参标定向导** — 多相机外参估计
- 分步引导式工作流，带实时预览

### 多窗口与布局
- **Temporal Plot** — X-T / Y-T 事件坐标-时间散点图
- **多显示窗口** — 在 MDI 区域中生成额外的 OpenGL 视口
- **布局保存/恢复** — 将 dock 几何位置和窗口位置持久化到 JSON

### 算法桥接
- 完整 46 算法元数据注册表（噪声过滤、光流、团块检测、目标跟踪、角点检测、立体匹配等）
- 运行时参数持久化（保存/加载到 JSON）
- 线程安全的算法实例管理
- **统一参数控制**：所有算法参数仅在侧栏（AlgorithmsPanel）调节，算法显示窗口（AlgoWindow）只展示算法标题与输出，避免两处独立参数面板不同步导致用户无法确认算法实际使用的参数值

### 事件→灰度重建（Event-to-Video）
提供三种重建模式，**默认 E2VID**（深度学习）：
- **E2VID**（DL，默认）：从 [rpg_e2vid](https://github.com/uzh-rpg/rpg_e2vid) 完整移植，ONNX Runtime 多线程 CPU 推理。默认 128×128 ROI + 24fps + 1/4 下采样（64×64 推理 → 上采样回 128×128）。无模型时自动回退到启发式重建
- **BardowVariational**（非 DL）：滑动窗口变分优化，联合估计光流 u 和对数亮度 L，使用 `lambda1`-`lambda6` 六个正则化项（光流 TV、时间平滑、亮度 TV、光流约束、无事件死区、先验图保持），Chambolle-Pock 原始-对偶交替优化
- **InteractingMaps**（非 DL）：六张互连图（强度 I、梯度 G、时变 V、光流 F、标定 C、旋转 R）交替松弛，三个关系驱动更新（G=∇I、−V=F·G、F=m32(R×C)），旋转 R 由线性最小二乘估计

#### E2VID 部署方法（一次性，约 5 分钟）

```bash
# 1. 下载 ONNX Runtime 1.19.2（Linux x64 CPU）到 third_party/
cd /path/to/GUI-for-openEB
mkdir -p third_party/onnxruntime && cd third_party/onnxruntime
wget https://github.com/microsoft/onnxruntime/releases/download/v1.19.2/onnxruntime-linux-x64-1.19.2.tgz
tar xzf onnxruntime-linux-x64-1.19.2.tgz --strip-components=1
cd ../..

# 2. 创建 Python 转换环境
python3 -m venv .venv && . .venv/bin/activate
pip install torch --index-url https://download.pytorch.org/whl/cpu onnx onnxscript onnxruntime numpy
deactivate

# 3. 下载 PyTorch 预训练权重（约 41 MB）
wget -P models/ http://rpg.ifi.uzh.ch/data/E2VID/models/E2VID_lightweight.pth.tar

# 4. 转换为 ONNX（生成 models/e2vid_lightweight.onnx）
. .venv/bin/activate && python models/convert_to_onnx.py && deactivate

# 5. 重新编译（CMake 自动检测 ONNX Runtime）
cmake --build build -- -j$(nproc)
```

完成后启动 EBplus，启用 **Algorithm → Event → Video** 即默认 E2VID 模式。GUI 暴露可调参数：模型路径、auto-HDR、下采样开关、锐化强度、双边滤波。

> **无 ONNX Runtime 时**：E2VID 自动回退到启发式模式（体素网格求和 + Sigmoid）。BardowVariational 和 InteractingMaps 模式无需任何额外依赖，均为论文方法的完整复现。

详见 [doc/design.md §4.4.2](doc/design.md)。

---

## 快速开始

```bash
# 1. 克隆仓库（openEB SDK 已作为子树包含在内）
git clone <repo-url> GUI-for-openEB
cd GUI-for-openEB

# 2. 编译
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)

# 3. 运行（启动脚本会自动设置所有必需的环境变量）
./run.sh
```

就这么简单。启动脚本会自动检测 Wayland 会话，设置正确的 Qt 平台插件，并配置 HAL 插件路径。

---

## 截图

### 主界面

![主界面](pic/1.0.0.png)

### Camera 菜单

![Camera 菜单](pic/menu_camera.png)

### Preprocess 菜单

![Preprocess 菜单](pic/menu_preprocess.png)

### Tools 菜单

![Tools 菜单](pic/menu_tools.png)

---

## 目录结构

```
GUI-for-openEB/
├── openeb/                       # openEB SDK 子树（Apache 2.0，v5.2.0）
├── gui/                          # GUI 应用（C++17 / Qt 6）
│   ├── main.cpp                  # 应用入口，环境变量默认值
│   ├── main_window.{h,cpp}       # 主窗口：菜单、dock、信号连接
│   ├── display/                  # OpenGL 事件显示控件
│   ├── panels/                   # 设置 dock 面板
│   │   ├── devices_panel.*       # 相机发现与连接
│   │   ├── information_panel.*   # 传感器元数据
│   │   ├── statistics_panel.*    # 事件率 / ON-OFF / FPS
│   │   ├── display_panel.*       # 帧模式、调色板、累积时间
│   │   ├── biases_panel.*        # LL-bias 控制
│   │   ├── roi_panel.*           # 感兴趣区域
│   │   ├── esp_panel.*           # Anti-Flicker / Trail / ERC
│   │   ├── trigger_panel.*       # Trigger In / Out
│   │   ├── preprocessing_panel.* # 8 级滤波链
│   │   ├── algorithms_panel.*    # 算法注册表
│   │   ├── file_tools_panel.*    # 转换 / 裁剪 / 信息
│   │   └── settings_panel.*      # 标签页容器
│   ├── app/                      # 控制器
│   │   ├── camera_controller.*   # 相机生命周期 & HAL facility 访问
│   │   ├── frame_pipeline.*      # CD → QImage 渲染管线
│   │   ├── statistics_controller.*  # 事件率计算
│   │   └── file_converter.*      # 后台文件转换
│   ├── algo_bridge/              # 算法注册表 & 滤波链
│   ├── recorder/                 # RAW 录制 & 文件回放
│   ├── exporter/                 # HDF5/CSV/AVI 导出
│   ├── config/                   # JSON 配置 & 布局持久化
│   ├── calibration/              # 内参/外参向导
│   ├── temporal/                 # X-T / Y-T 时间图
│   ├── widgets/                  # 多窗口 MDI 管理器
│   └── CMakeLists.txt
├── algo/                         # 自研算法库
│   ├── common/                   # 事件缓冲、帧生成器、数据加载器
│   ├── calibration/              # 内参 & 外参算法
│   └── CMakeLists.txt
├── run.sh                        # 启动脚本（环境变量设置）
├── pic/                          # README 截图
├── doc/
│   ├── design.md                 # 完整设计规格（10 阶段路线图）
│   └── compile.md                # 编译指南（Ubuntu 26.04 / GCC 15）
├── LICENSE                       # MIT（项目原创代码）
├── README.md                     # 英文文档
└── README_CN.md                  # 中文文档
```

---

## 环境要求

| 组件 | 版本 |
|------|------|
| 操作系统 | Ubuntu 26.04（或兼容 Linux） |
| 编译器 | GCC 15+ |
| CMake | 4.x |
| Qt | 6.x（Widgets、OpenGL、OpenGLWidgets） |
| OpenEB SDK | 5.2.0（已作为子树包含） |
| OpenCV | 4.x |
| Python | 3.12（仅在从源码编译 openEB 时需要） |

详见 [doc/compile.md](doc/compile.md)，包含 GCC 15 `<cstdint>` 修复和通过 deadsnakes PPA 安装 Python 3.12 的说明。

---

## 编译

```bash
# 1. 确保 openEB SDK 已安装（见 doc/compile.md）
# 2. 配置
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. 编译
cmake --build build -- -j$(nproc)
```

编译产物输出到 `build/gui/gui_for_openeb`。

---

## 运行

### 方式一：启动脚本（推荐）

```bash
./run.sh
```

脚本自动完成以下设置：
- 设置 `LD_LIBRARY_PATH` 包含 `/usr/local/lib`
- 设置 `HDF5_PLUGIN_PATH` 以支持 HDF5 文件
- 设置 `MV_HAL_PLUGIN_PATH` 为 Prophesee 默认路径（可按需修改为其他厂商）
- 在 Wayland 会话下强制 `QT_QPA_PLATFORM=xcb`（Wayland 插件对 `QOpenGLWidget` 渲染黑屏）
- 强制 `QSG_RHI_BACKEND=opengl`（Qt 6 可能默认使用 Vulkan，导致黑屏）

如需为你的相机自定义，复制脚本并修改环境变量：

```bash
cp run.sh run.local.sh
# 编辑 run.local.sh — 修改 MV_HAL_PLUGIN_PATH 等
./run.local.sh
```

### 方式二：手动启动

```bash
# 必需的环境变量
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:/usr/local/lib"
export HDF5_PLUGIN_PATH="${HDF5_PLUGIN_PATH:-}:/usr/local/lib/hdf5/plugin"
export MV_HAL_PLUGIN_PATH=/usr/local/lib/metavision/hal/plugins  # Prophesee
# export MV_HAL_PLUGIN_PATH=/usr/lib/CenturyArks/hal/plugins     # CenturyArks

# Wayland 修复：强制 XCB 插件（Wayland 插件对 QOpenGLWidget 渲染黑屏）
export QT_QPA_PLATFORM=xcb
# 强制 OpenGL RHI 后端（Qt 6 可能默认使用 Vulkan，导致黑屏）
export QSG_RHI_BACKEND=opengl

./build/gui/gui_for_openeb
```

### 环境变量说明

| 变量 | 用途 | 默认值 |
|------|------|--------|
| `MV_HAL_PLUGIN_PATH` | 相机 HAL 插件目录 | `/usr/local/lib/metavision/hal/plugins` |
| `HDF5_PLUGIN_PATH` | HDF5 插件目录（用于 `.hdf5` 文件） | `/usr/local/lib/hdf5/plugin` |
| `LD_LIBRARY_PATH` | SDK 共享库搜索路径 | 必须包含 `/usr/local/lib` |
| `QT_QPA_PLATFORM` | Qt 平台插件 | Wayland 下为 `xcb`；其他情况不设置 |
| `QSG_RHI_BACKEND` | Qt RHI 后端 | `opengl` |

> **Wayland 注意**：在 Wayland 会话下，Qt 6 的 Wayland 插件对 `QOpenGLWidget` 子控件渲染黑屏。应用和启动脚本会自动强制 `QT_QPA_PLATFORM=xcb`（通过 XWayland）和 `QSG_RHI_BACKEND=opengl` 以确保正确渲染。如果直接运行二进制文件而不使用启动脚本，请务必设置这些变量。

---

## 相机厂商配置

`MV_HAL_PLUGIN_PATH` 必须指向你的相机厂商的 HAL 插件目录：

| 厂商 | 插件路径 |
|------|----------|
| Prophesee（默认 openEB） | `/usr/local/lib/metavision/hal/plugins` |
| CenturyArks | `/usr/lib/CenturyArks/hal/plugins` |

如果环境变量已在 shell 中导出，应用会尊重它；否则回退到 Prophesee 默认路径。

验证相机是否被检测到：

```bash
# 通过 openEB SDK 列出在线相机
metavision_hal_ls
```

---

## 常见问题

### 启动后黑屏 / 视口空白

这是 Wayland + Qt 6 渲染问题。启动脚本（`run.sh`）会自动处理，但如果直接运行二进制文件：

```bash
export QT_QPA_PLATFORM=xcb        # 通过 XWayland 强制 XCB
export QSG_RHI_BACKEND=opengl     # 强制 OpenGL（Qt 6 可能默认使用 Vulkan）
```

### 相机未检测到

1. 确认 HAL 插件路径与你的厂商匹配（见[相机厂商配置](#相机厂商配置)）。
2. 检查 `metavision_hal_ls` 输出——如果失败，说明 SDK 找不到插件。
3. 确保 `LD_LIBRARY_PATH` 包含 `/usr/local/lib`。
4. 对于 CenturyArks 相机，插件目录中必须存在 `libsilky_common_plugin.so`。

### `metavision_hal_ls` 正常但 GUI 显示"未连接相机"

GUI 启动时会自动检测插件路径。如果你的厂商路径与 Prophesee 默认路径不同，启动前设置 `MV_HAL_PLUGIN_PATH`：

```bash
export MV_HAL_PLUGIN_PATH=/usr/lib/CenturyArks/hal/plugins
./run.sh
```

### 编译报错缺少 `<cstdint>`（GCC 15）

GCC 15 更改了默认标准。请按照 [doc/compile.md](doc/compile.md) 中的说明修复。

### HDF5 文件打开失败

设置 `HDF5_PLUGIN_PATH` 为 HDF5 插件目录（通常为 `/usr/local/lib/hdf5/plugin`）。

---

## 已知问题与反馈

EBplus 正在持续开发中，可能仍存在 BUG。如果你在使用过程中遇到任何问题——崩溃、渲染异常、控件失灵或非预期行为——欢迎[提交 issue](../../issues)。本项目会持续优化，来自真实用户的反馈是最直接的帮助。

---

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+O` | 打开事件文件 |
| `Ctrl+C` | 连接第一个可用相机 |
| `Ctrl+R` | 切换 ROI 拖拽模式 |
| `R` | 开始录制 |
| `F11` | 全屏 |
| `Ctrl+Shift+T` | 打开 Temporal Plot |
| `Ctrl+Q` | 退出 |

---

## 开发路线图

基于 [doc/design.md](doc/design.md)（10 阶段计划）：

| 阶段 | 描述 | 状态 |
|------|------|------|
| 1 | CMake 骨架、相机发现、OpenGL 显示、基础参数、统计面板、algo_bridge 骨架 | **已完成** |
| 2 | Bias / ROI / ESP / Trigger 控制面板 | **已完成** |
| 3 | 录制、回放、文件裁剪 | **已完成** |
| 4 | 导出 / 转换（RAW ↔ HDF5 ↔ CSV，AVI 导出） | **已完成** |
| 5 | 事件滤波链 + 8 个预处理器 | **已完成** |
| 6 | 自研 CV 算法（噪声过滤、光流、团块、跟踪…） | **已完成** |
| 7 | 分析算法（主动标记、事件转视频） | **已完成** |
| 8 | 标定算法（内参/外参） | **已完成** |
| 9 | 标定向导 UI（内参/外参） | **已完成** |
| 10 | 多窗口布局、Temporal Plot、布局持久化、i18n | **已完成** |

> 全部 10 个阶段均已完成。自研 CV/分析/标定算法均已实现并经过原始事件数据集成测试验证。

---

## 许可证

### 项目原创代码

采用 [MIT License](LICENSE) 授权。

### 引用的 openEB 代码

本项目引用了 [openEB](https://github.com/prophesee-ai/openeb)（版本 5.2.0）。

openEB 采用 [Apache License 2.0](openeb/licensing/LICENSE_OPEN) 授权，版权归 Prophesee 及其贡献者所有。对 openEB 代码的任何使用、修改或分发必须遵守 Apache License 2.0 的条款。

openEB 的第三方开源声明见 [OPEN_SOURCE_3RDPARTY_NOTICES](openeb/licensing/OPEN_SOURCE_3RDPARTY_NOTICES)。

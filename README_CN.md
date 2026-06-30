# GUI for openEB

基于 [openEB](https://github.com/prophesee-ai/openeb) 事件相机的 Qt 6 图形化界面，附带自研算法库（`algo/`）。

**当前阶段：Phase 2 已完成** — 详见[开发路线图](#开发路线图)。

---

## 功能特性

### Phase 1 — MVP（已交付）
- CMake 项目骨架（`openeb/` + `gui/` + `algo/` 三层架构）
- 相机发现与连接（实时相机 + 离线文件回放）
- OpenGL 实时事件显示（GLSL 330 core，letterbox 视口）
- 基础显示参数（累积时间 1–1000 ms，4 种色彩主题）
- 统计面板（事件率、峰值率、ON/OFF 比、FPS、时间戳）
- `algo_bridge` 接口骨架（完整 46 算法注册表）
- `algo/common/` 公共工具：无锁 SPSC 环形缓冲、多窗口帧生成器、离线数据加载器

### Phase 2 — 相机控制面板（已交付）
- **Biases 面板**：动态枚举所有 HAL bias，滑块+精确输入+Reset，保存/加载 `.bias` 文件
- **ROI 面板**：矩形 ROI/RONI（`I_ROI`），显示区拖拽选区，应用/清除
- **ESP 面板**：Anti-Flicker（模式/频带/预设/占空比/阈值）、Trail Filter（类型/阈值）、ERC（目标事件率）
- **Trigger 面板**：Trigger In（逐通道启用）+ Trigger Out（启用/周期/占空比）

所有面板在设备不支持对应 HAL facility 时优雅降级（如文件回放时四个面板自动禁用）。

---

## 目录结构

```
GUI-for-openEB/
├── openeb/                   # openEB SDK 子树（Apache 2.0，v5.2.0）
├── gui/                      # GUI 应用（C++ / Qt 6）
│   ├── main.cpp              # 应用入口，环境变量默认值
│   ├── main_window.{h,cpp}   # 主窗口：菜单、Dock、信号接线
│   ├── display/              # OpenGL 事件显示控件
│   ├── panels/               # 设置 Dock 面板（Devices/Info/Stats/
│   │                         #   Display/Biases/ROI/ESP/Trigger/…）
│   ├── app/                  # 控制器（camera/frame_pipeline/statistics）
│   ├── algo_bridge/          # 算法注册表与 algo/ 桥接
│   └── CMakeLists.txt
├── algo/                     # 自研算法库（C++）
│   ├── common/               # 事件环形缓冲、帧生成器、数据加载器
│   └── CMakeLists.txt
├── scripts/
│   └── run_gui.sh            # 启动脚本（环境变量设置）
├── doc/
│   ├── design.md             # 完整设计规格（10 阶段路线图）
│   └── compile.md            # 编译指南（Ubuntu 26.04 / GCC 15）
├── LICENSE                   # MIT（项目原创代码）
└── README_CN.md
```

---

## 环境要求

| 组件 | 版本 |
|------|------|
| 操作系统 | Ubuntu 26.04（或兼容 Linux） |
| 编译器 | GCC 15+ |
| CMake | 4.x |
| Qt | 6.x（Widgets, OpenGL, OpenGLWidgets） |
| OpenEB SDK | 5.2.0 |
| OpenCV | 4.x |
| Python | 3.12（仅从源码编译 openEB 时需要） |

详细系统配置见 [doc/compile.md](doc/compile.md)，包含 GCC 15 `<cstdint>` 修复和 deadsnakes PPA 安装 Python 3.12 的说明。

---

## 编译

```bash
# 1. 确保 openEB SDK 已安装（见 doc/compile.md）
# 2. 配置
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. 编译
cmake --build build --config Release -- -j$(nproc)
```

产物输出到 `build/gui/gui_for_openeb`。

---

## 运行

### 快速启动（启动脚本）

```bash
./scripts/run_gui.sh
```

脚本自动检测 Wayland 会话并设置 `QT_QPA_PLATFORM=wayland`。可编辑脚本或复制到 `scripts/run_gui.local.sh` 自定义 HAL 插件路径。

### 手动启动

```bash
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:/usr/local/lib"
export HDF5_PLUGIN_PATH="${HDF5_PLUGIN_PATH:-}:/usr/local/lib/hdf5/plugin"
export MV_HAL_PLUGIN_PATH=/usr/local/lib/metavision/hal/plugins  # Prophesee
# export MV_HAL_PLUGIN_PATH=/usr/lib/CenturyArks/hal/plugins     # CenturyArks

# Wayland 会话需强制 Wayland 平台插件：
export QT_QPA_PLATFORM=wayland

./build/gui/gui_for_openeb
```

### 环境变量

| 变量 | 用途 | 默认值 |
|------|------|--------|
| `MV_HAL_PLUGIN_PATH` | 相机 HAL 插件目录 | `/usr/local/lib/metavision/hal/plugins` |
| `HDF5_PLUGIN_PATH` | HDF5 插件目录（读 `.hdf5` 文件） | `/usr/local/lib/hdf5/plugin` |
| `LD_LIBRARY_PATH` | SDK 共享库搜索路径 | （须包含 `/usr/local/lib`） |
| `QT_QPA_PLATFORM` | Qt 平台插件 | 若 `WAYLAND_DISPLAY` 已设置则为 `wayland` |

> **Wayland 注意**：Wayland 会话下 Qt 经 XWayland 可能渲染全黑窗口。强制 `QT_QPA_PLATFORM=wayland` 使用原生 Wayland 插件。若原生 Wayland 不可用，回退 `QT_QPA_PLATFORM=xcb`。

### 相机厂商

`MV_HAL_PLUGIN_PATH` 须指向你的相机厂商的 HAL 插件目录：

| 厂商 | 插件路径 |
|------|----------|
| Prophesee（默认 openEB） | `/usr/local/lib/metavision/hal/plugins` |
| CenturyArks | `/usr/lib/CenturyArks/hal/plugins` |

若环境变量已在 shell 中导出，应用会尊重它；否则回退到 Prophesee 默认值。

---

## 开发路线图

基于 [doc/design.md](doc/design.md)（10 阶段计划）：

| 阶段 | 描述 | 状态 |
|------|------|------|
| 1 | CMake 骨架、相机发现、OpenGL 显示、基础参数、统计面板、algo_bridge 骨架 | **已完成** |
| 2 | Bias / ROI / ESP / Trigger 控制面板 | **已完成** |
| 3 | 录制、回放、文件裁剪 | 待开发 |
| 4 | 导出 / 转换（RAW↔HDF5↔CSV↔DAT） | 待开发 |
| 5 | 事件过滤链 + 7 种预处理器 | 待开发 |
| 6 | 自研 CV 算法（噪声过滤、光流、团块、跟踪…） | 待开发 |
| 7 | 分析算法（主动标记、事件→视频） | 待开发 |
| 8 | 标定（内参 / 外参） | 待开发 |
| 9 | 多窗口布局（Temporal plot、算法结果窗口） | 待开发 |
| 10 | 国际化（i18n）、打磨、打包 | 待开发 |

---

## 许可证

### 本项目原创代码

使用 [MIT License](LICENSE)。

### 引用的 openEB 代码

本项目引用了 [openEB](https://github.com/prophesee-ai/openeb)（版本 5.2.0）。

openEB 使用 [Apache License 2.0](openeb/licensing/LICENSE_OPEN)，版权归属于 Prophesee 及其贡献者。对 openEB 代码的任何使用、修改和分发均须遵守 Apache License 2.0 的条款。

openEB 第三方开源组件声明见 [OPEN_SOURCE_3RDPARTY_NOTICES](openeb/licensing/OPEN_SOURCE_3RDPARTY_NOTICES)。

# Changelog

## v1.0.1 - 2026-11-18

### 修复
- 修复 `ABC` / `XYZ` 按键映射错位：`A`/`C` 与 `Y`/`X` 曾被错误对调，现按 Genesis 6 键手柄次序正确映射（A/B/C→J/K/L，X/Y/Z→U/I/O）。
- 修复中文等非 ASCII 路径下的自动存档：`saveState` / `loadState` 与 `fileExists` 改用 UTF-16 宽字符文件 I/O，快照缩略图点击后可正确恢复游戏状态。
- 主页游玩历史快照居中显示，消除整体偏右。

### 新增
- 主页快照卡片右上角新增删除按钮（红色 `x`），点击后移除对应历史记录、缩略图与关联的自动存档 / 快捷存档。

## v1.0.0 - 2026-11-18

正式版本。MesenMD 是一款仿照 **Mesen2** 界面风格、面向 **Sega Mega Drive / Genesis** 的 Windows 模拟器前端。

### 新增
- 完整主界面：菜单栏（File / Emulation / Config / Help）、状态栏、主页游玩历史快照。
- 快捷键系统：
  - `Esc` 关闭当前对话框；游戏中则关闭游戏（自动保存状态与快照）。
  - `Ctrl+C` 退出程序（自动保存状态与快照）。
  - `F11` 切换桌面全屏。
  - `F1`-`F10` 快速加载状态槽；`Shift+F1`-`F10` 快速保存状态槽。
  - `F12` 重置、`Space` 暂停/继续、`Ctrl+O` 打开 ROM、`Ctrl+W` 关闭游戏。
- 默认键盘配置：方向（方向键 / WASD）、A/B/C（X / J、Z / K、C / L）、X/Y/Z（U / I / O）、Start（Enter / 小键盘 Enter / B）、Mode（左右 Shift / V）。
- 多输入支持：每个动作可绑定多个按键；键盘与所有已连接的 XInput / DirectInput 手柄同时驱动玩家；手柄热插拔自动识别。
- 视频滤镜：LCD Grid / CRT / Scanlines（内嵌 RetroArch GLSL shader）。
- 自动保存 / 自动加载游戏状态，主页默认显示游玩历史快照。
- 界面多语言：English / 中文（`Config -> Video...` 中切换），支持系统 CJK 字体渲染。
- 配置持久化：`mesenmd.cfg`（含全屏、语言、按键、历史记录等）。

### 修复
- 滤镜开启后偶发黑屏的问题。
- 中文、日语等非 ASCII 路径的加载与显示。
- 程序退出时的状态与快照保存。

### 技术栈
- 核心：Genesis Plus GX (libretro)。
- 界面：SDL2 + OpenGL 3.0 + Dear ImGui。
- 语言：C++17，MinGW-w64 编译。
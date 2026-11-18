# MesenMD

一个仿照 **Mesen2** 界面风格的 **Sega Mega Drive / Genesis** 模拟器前端。

**当前版本：v1.0.5**  ·  Windows (MinGW-w64)

- **核心**：[Genesis Plus GX (libretro)](https://github.com/libretro/Genesis-Plus-GX) —— RetroArch 的官方 MD 核心，精度高，支持 MD / Sega CD / Master System / Game Gear。
- **前端界面**：SDL2 + OpenGL 3 + Dear ImGui，深色主题，行为与外观尽量贴近 Mesen2。
- **平台**：Windows（MinGW-w64 编译）。

> 仅供学习与测试，请使用你合法拥有的 ROM / BIOS 文件。

---

## 特性

- [x] 加载 Mega Drive / Genesis ROM（命令行参数或 `File -> Open ROM...`）
- [x] 「最近打开」历史记录
- [x] 视频缩放：适配窗口 / 原始 / 整数倍（x2–x5）
- [x] 双线性过滤开关
- [x] 帧率显示 FPS
- [x] 音频输出（SDL2 队列驱动，接近实时节奏）
- [x] 键盘按键重映射（`Config -> Input...`）
- [x] 手柄支持（SDL_GameController，玩家 1 / 玩家 2）
- [x] 首页快照支持手柄导航（D-pad 选择、A 开始、X 删除）
- [x] 手柄快捷键（默认 Select+按钮，可在 `Config -> Input...` 重映射）
- [x] 暂停 / 复位 / 关闭游戏
- [x] 支持中文等非 ASCII 路径（UTF-8 处理）
- [x] 拖放 ROM 文件到窗口即可加载
- [x] 快捷键：Esc / Ctrl+C / F11 / F1-F10 / Shift+F1-F10
- [x] 界面多语言：English / 中文
- [x] 全屏切换（F11）

- [x] Video filters (LCD Grid / CRT / Scanlines) using official RetroArch GLSL shaders
- [x] Auto-save & auto-load game state on close/open (F1-F10 quick load / Shift+F1-F10 quick save)
- [x] Home page with play-history snapshots/thumbnails
- [x] Delete a recent-game snapshot (removes the history entry, its thumbnail and saved states)
- [x] UI themes: Light / Dark / follow system (auto-adapts to the Windows personalization setting, switchable in `Config -> Video...`)
- [x] Pause now mutes audio fully (pauses the SDL audio device and clears queued samples)
- [x] Gamepad navigation on the home page (D-pad select, A play, X delete)
- [x] Gamepad hotkeys (default Select+button; rebindable in `Config -> Input...`)
---

## 目录结构

```
mesenmd/
├── src/
│   ├── main.cpp        # SDL2 + OpenGL + ImGui 前端
│   ├── emulator.h      # libretro 核心封装头
│   └── emulator.cpp    # 核心动态加载 + 视频/音频/输入/环境回调
├── third_party/
│   ├── SDL2-2.32.10/                # SDL2 MinGW 开发包
│   ├── imgui-master/                # Dear ImGui
│   └── Genesis-Plus-GX-master/      # 核心源码（含已编译 DLL）
├── build.cmd           # 一键编译前端 + 复制运行时依赖
├── MesenMD.exe         # 编译产物
├── SDL2.dll            # 运行时依赖
└── genesis_plus_gx_libretro.dll     # 核心运行时
```

---

## 依赖

- **MinGW-w64 GCC**（示例：`D:\ThirdLib\mingw64\bin`，gcc 13.x）
- **SDL2 2.32.x** MinGW 开发包（`third_party\SDL2-2.32.10`）
- **Dear ImGui**（`third_party\imgui-master`）
- **Genesis Plus GX** 源码（`third_party\Genesis-Plus-GX-master`）

---

## 编译

### 1. 编译核心（Genesis Plus GX）

本仓库已附带编译好的 `genesis_plus_gx_libretro.dll`。若要重新编译：

```bat
cd third_party\Genesis-Plus-GX-master
:: 先创建 uname 垫片（MinGW 在 Windows 下 make 会找不到 uname）
:: 文件 build_tools\uname.cmd 内容：  @echo MINGW64_NT-10.0
set PATH=%CD%\build_tools;%PATH%
mingw32-make -f Makefile.libretro platform=win CC=gcc CXX=g++
```

产物为 `genesis_plus_gx_libretro.dll`。

### 2. 编译前端

方式 A —— 双击运行 `build.cmd`：

```bat
build.cmd
```

方式 B —— 手动 g++（在项目根目录）：

```bat
g++ -std=c++17 -O2 -mwindows -Isrc ^
  -Ithird_party\imgui-master -Ithird_party\imgui-master\backends ^
  -Ithird_party\SDL2-2.32.10\x86_64-w64-mingw32\include\SDL2 ^
  -Ithird_party\Genesis-Plus-GX-master\libretro\libretro-common\include ^
  src\main.cpp src\emulator.cpp ^
  third_party\imgui-master\imgui.cpp third_party\imgui-master\imgui_draw.cpp ^
  third_party\imgui-master\imgui_tables.cpp third_party\imgui-master\imgui_widgets.cpp ^
  third_party\imgui-master\backends\imgui_impl_sdl2.cpp ^
  third_party\imgui-master\backends\imgui_impl_opengl3.cpp ^
  -Lthird_party\SDL2-2.32.10\x86_64-w64-mingw32\lib ^
  -lmingw32 -lSDL2main -lSDL2 -lshell32 -lopengl32 -lgdi32 -lwinmm -lcomdlg32 ^
  -static-libgcc -static-libstdc++ -o MesenMD.exe
```

复制运行时依赖到 exe 同目录（`build.cmd` 已自动完成）：

```bat
copy third_party\SDL2-2.32.10\x86_64-w64-mingw32\bin\SDL2.dll .
copy third_party\Genesis-Plus-GX-master\genesis_plus_gx_libretro.dll .
```

---

## 运行

双击 `MesenMD.exe`，或传入 ROM 路径：

```bat
MesenMD.exe "C:\path\to\game.md"
```

也可在界面中 `File -> Open ROM...`（Ctrl+O）选择 `.md / .bin / .gen / .smd / .sg / .68k`。

---

## 默认按键

| 动作 | 键盘 |
|------|------|
| 方向 | 方向键 / W A S D |
| A / B / C | X / Z / C 或 J / K / L |
| X / Y / Z | U / I / O |
| Start | Enter / 小键盘 Enter / B |
| Mode | Right Shift / Left Shift / V |
| 重置 | F12 |
| 暂停 / 继续 | Space |
| 打开 ROM | Ctrl+O |
| 关闭游戏 | Ctrl+W |

---

## 快捷键

| 快捷键 | 功能 |
|------|------|
| Esc | 关闭当前对话框；游戏中则关闭游戏（自动保存状态与快照） |
| Ctrl+C | 退出程序（自动保存状态与快照） |
| F11 | 切换全屏（桌面全屏） |
| F1 - F10 | 快速加载状态槽 1-10 |
| Shift+F1 - F10 | 快速保存状态槽 1-10 |
| F12 | 重置游戏 |
| Space | 暂停 / 继续 |
| Ctrl+O | 打开 ROM |
| Ctrl+W | 关闭游戏 |

界面语言可在 `Config -> Video...` 中切换（English / 中文）。

按键可在 `Config -> Input...` 中重映射，支持每个动作绑定多个按键；键盘与所有已连接手柄同时驱动玩家。手柄遵循标准 SDL 映射（A/B/X/Y、肩键、Start/Back、左摇杆）。

手柄快捷键（默认，`Select` 即手柄的 Back / Select 键）：

| 动作 | 默认组合 |
|------|------|
| 保存状态 | Select + A |
| 加载状态 | Select + B |
| 重置游戏 | Select + X |
| 退出游戏 | Select + Y |
| 退出程序 | Select + Start |
| 暂停 | Select + LB |
| 全屏 | Select + RB |

可在 `Config -> Input...` 的「手柄快捷键」区重映射：点击「重新绑定」后先按住修饰键再按目标按钮，按 `Esc` 取消。

---

## 兼容性与说明

- 核心通过 `LoadLibrary` 动态加载 `genesis_plus_gx_libretro.dll`，该 DLL 需与 `MesenMD.exe` 同目录。
- ROM 路径按 UTF-8 传递，核心会自行打开文件（`need_fullpath`），因此支持中文路径。
- 若运行 Sega CD / 32X 等需要扩展 BIOS 的游戏，请将对应 BIOS 放到 ROM 所在目录，或稍后可在 `Config` 中增加系统目录设置。
- 本项目为个人学习项目，无官方支援；界面细节仍在逐步完善。

---

## License

- Genesis Plus GX：GPLv3。
- Dear ImGui：MIT。
- SDL2：zlib。
- 本项目前端代码：随核心以 GPLv3 发布。

请仅使用你合法拥有的游戏 ROM / BIOS。

---

## Input

- Keyboard bindings support multiple keys per action (e.g. Arrows and WASD can both move). Open Config -> Input... to add/remove/rebind keys.
- Gamepads are auto-detected and auto-mapped via SDL's built-in SDL_GameController database, so XInput and DirectInput pads work out of the box.
- Controllers are hot-plugged: plugging/unplugging is handled live, and each pad can be assigned to Player 1 or Player 2 in the Input window.
- Keyboard and gamepad input combine, so multiple pads or a pad plus the keyboard can drive the same player at the same time.
- Optional custom mappings can be added by placing a gamecontrollerdb.txt next to MesenMD.exe.
- Default 6-button gamepad mapping (standard SDL/Xbox layout): `X` = MD A, `A` = MD B, `B` = MD C, `LB` = MD X, `Y` = MD Y, `RB` = MD Z, `Start` = Start, `Back/Guide` = Mode.


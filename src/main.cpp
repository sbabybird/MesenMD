#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_syswm.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stb_image.h>
#include <stb_image_write.h>

#include "emulator.h"
#include "video_filters.h"

#define MESENMD_VERSION "1.0.17"
#define MESENMD_AUTHOR    "sbabybird"
#define MESENMD_REPO_URL  "https://github.com/sbabybird/MesenMD"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
enum Action
{
    AID_UP, AID_DOWN, AID_LEFT, AID_RIGHT,
    AID_A, AID_B, AID_C, AID_X, AID_Y, AID_Z,
    AID_START, AID_MODE,
    AID_COUNT
};

const char* g_actionNames[AID_COUNT] =
{
    "Up", "Down", "Left", "Right",
    "A", "B", "C", "X", "Y", "Z",
    "Start", "Mode"
};

const char* g_scancodeNames[SDL_NUM_SCANCODES] = { nullptr };


enum HotkeyID
{
    HK_SAVE, HK_LOAD, HK_RESET, HK_EXIT_GAME, HK_QUIT, HK_PAUSE, HK_FULLSCREEN,
    HK_COUNT
};

const char* g_hotkeyNamesEnglish[HK_COUNT] =
{
    "Save State", "Load State", "Reset", "Exit Game", "Quit Program", "Pause", "Fullscreen"
};
const char* g_hotkeyNamesChinese[HK_COUNT] =
{
    "保存状态",
    "加载状态",
    "重置游戏",
    "退出游戏",
    "退出程序",
    "暂停",
    "全屏"
};

static const SDL_GameControllerButton g_defaultHotkeyMod[HK_COUNT] =
{
    SDL_CONTROLLER_BUTTON_BACK,        // Save State  (Select + Right Shoulder)
    SDL_CONTROLLER_BUTTON_BACK,        // Load State  (Select + Left Shoulder)
    SDL_CONTROLLER_BUTTON_LEFTSTICK,   // Reset       (L3 + R3)
    SDL_CONTROLLER_BUTTON_BACK,        // Exit Game   (Select + Start)
    SDL_CONTROLLER_BUTTON_BACK,        // Quit Program (Select + Y)
    SDL_CONTROLLER_BUTTON_BACK,        // Pause       (Select + A)
    SDL_CONTROLLER_BUTTON_BACK         // Fullscreen  (Select + B)
};
static const SDL_GameControllerButton g_defaultHotkeyButton[HK_COUNT] =
{
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, // Save State  (Select + Right Shoulder)
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  // Load State  (Select + Left Shoulder)
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,    // Reset       (L3 + R3)
    SDL_CONTROLLER_BUTTON_START,         // Exit Game   (Select + Start)
    SDL_CONTROLLER_BUTTON_Y,             // Quit Program (Select + Y)
    SDL_CONTROLLER_BUTTON_A,             // Pause       (Select + A)
    SDL_CONTROLLER_BUTTON_B              // Fullscreen  (Select + B)
};

#define MAX_HOTKEY_KEYS_PER_ACTION 16

// A single keyboard hotkey binding: key + required modifiers + optional state slot.
struct HotkeyKey
{
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    Uint16 mod = 0;   // required modifiers (KMOD_CTRL/KMOD_SHIFT/KMOD_ALT/KMOD_GUI)
    int  slot = 0;    // state slot index (1..10); 0 = default slot
};

static const HotkeyKey g_defaultHotkeyKeys[HK_COUNT][MAX_HOTKEY_KEYS_PER_ACTION] =
{
    // Save State  (Shift+F1..F10 -> slot 1..10)
    { {SDL_SCANCODE_F1, KMOD_SHIFT, 1}, {SDL_SCANCODE_F2, KMOD_SHIFT, 2},
      {SDL_SCANCODE_F3, KMOD_SHIFT, 3}, {SDL_SCANCODE_F4, KMOD_SHIFT, 4},
      {SDL_SCANCODE_F5, KMOD_SHIFT, 5}, {SDL_SCANCODE_F6, KMOD_SHIFT, 6},
      {SDL_SCANCODE_F7, KMOD_SHIFT, 7}, {SDL_SCANCODE_F8, KMOD_SHIFT, 8},
      {SDL_SCANCODE_F9, KMOD_SHIFT, 9}, {SDL_SCANCODE_F10, KMOD_SHIFT, 10} },
    // Load State  (F1..F10 -> slot 1..10)
    { {SDL_SCANCODE_F1, 0, 1}, {SDL_SCANCODE_F2, 0, 2},
      {SDL_SCANCODE_F3, 0, 3}, {SDL_SCANCODE_F4, 0, 4},
      {SDL_SCANCODE_F5, 0, 5}, {SDL_SCANCODE_F6, 0, 6},
      {SDL_SCANCODE_F7, 0, 7}, {SDL_SCANCODE_F8, 0, 8},
      {SDL_SCANCODE_F9, 0, 9}, {SDL_SCANCODE_F10, 0, 10} },
    // Reset
    { {SDL_SCANCODE_F12, 0, 0} },
    // Exit Game
    { {SDL_SCANCODE_ESCAPE, 0, 0} },
    // Quit Program
    { {SDL_SCANCODE_C, KMOD_CTRL, 0} },
    // Pause
    { {SDL_SCANCODE_SPACE, 0, 0} },
    // Fullscreen
    { {SDL_SCANCODE_F11, 0, 0} }
};
static const int g_defaultHotkeyKeyCount[HK_COUNT] = {10, 10, 1, 1, 1, 1, 1};

#define MAX_KEYS_PER_ACTION 8
#define MAX_PAD_BINDINGS_PER_ACTION 8

// A single controller input, encoded as (kind << 16) | index.
enum PadInputKind
{
    PAD_BUTTON   = 0,
    PAD_AXIS_NEG = 1,
    PAD_AXIS_POS = 2,
    PAD_HAT_UP   = 3,
    PAD_HAT_DOWN = 4,
    PAD_HAT_LEFT = 5,
    PAD_HAT_RIGHT= 6
};

static int  padInput(int kind, int index) { return (kind << 16) | (index & 0xffff); }
static int  padInputKind(int code)        { return (code >> 16) & 0xf; }
static int  padInputIndex(int code)       { return code & 0xffff; }

// Per-device controller mapping: each MD action can be bound to several
// controller inputs (buttons / stick axes / D-pad hats).
struct PadMap
{
    int map[AID_COUNT][MAX_PAD_BINDINGS_PER_ACTION] = {};
    int mapCount[AID_COUNT] = {};
};

// A connected pad, opened either as a standard SDL_GameController or as a
// raw SDL_Joystick (so DirectInput / unrecognised pads are also usable).
struct Pad
{
    SDL_GameController* gc     = nullptr;
    SDL_Joystick*       joy    = nullptr;
    SDL_JoystickID      instanceId = 0;
    std::string         name;
    std::string         guid;
    int                 player = 0;
    int                 buttonCount = 0;
    int                 axisCount   = 0;
    int                 hatCount    = 0;
    PadMap              map;
};

static const SDL_Scancode g_defaultKeys[AID_COUNT][MAX_KEYS_PER_ACTION] =
{
    { SDL_SCANCODE_UP,     SDL_SCANCODE_W },
    { SDL_SCANCODE_DOWN,   SDL_SCANCODE_S },
    { SDL_SCANCODE_LEFT,   SDL_SCANCODE_A },
    { SDL_SCANCODE_RIGHT,  SDL_SCANCODE_D },
    { SDL_SCANCODE_X,      SDL_SCANCODE_J },
    { SDL_SCANCODE_Z,      SDL_SCANCODE_K },
    { SDL_SCANCODE_C,      SDL_SCANCODE_L },
    { SDL_SCANCODE_U },
    { SDL_SCANCODE_I },
    { SDL_SCANCODE_O },
    { SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_B },
    { SDL_SCANCODE_RSHIFT, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_V }
};
static const int g_defaultKeyCount[AID_COUNT] = {2,2,2,2,2,2,2,1,1,1,3,3};

// Detect the OS UI language to pick the default UI language (0=English, 1=Chinese).
// Falls back to English for any non-Chinese system.
static int detectSystemLanguage()
{
    LANGID lang = GetUserDefaultUILanguage();
    return (PRIMARYLANGID(lang) == LANG_CHINESE) ? 1 : 0;
}

struct Config
{
    int videoScale = 0;              // 0 = fit, 1+ = integer scale
    bool filterBilinear = false;
    bool showFps = false;
    bool audioEnabled = true;
    int  videoFilter = 0;          // 0=None 1=Bilinear 2=LCD Grid 3=CRT 4=Scanlines
    bool maskEnabled = false;      // draw a console illustration in the side bars
    int  maskType = 0;             // 0=MD Model 1, 1=MD Model 2, 2=Genesis
    bool autoSaveState = true;    // save game state on close
    bool autoLoadState = false;   // load state on open (off by default)
    int  keyboardPlayer = 0;      // joypad slot the keyboard drives (0=P1, 1=P2)
    bool fullscreen = false;      // start the window fullscreen
    int  aspectRatio = 0;         // video aspect: 0=Auto(core) 1=4:3 2=Original
    int  language = detectSystemLanguage(); // UI language: 0=English, 1=Chinese (defaults to system)
    int  theme = 0;               // UI theme: 0=System(auto), 1=Dark, 2=Light
    int  windowX = SDL_WINDOWPOS_CENTERED;
    int  windowY = SDL_WINDOWPOS_CENTERED;
    int  windowW = 1024;
    int  windowH = 640;
    bool windowMaximized = false;
    bool windowSaved = false;
    SDL_Scancode keys[AID_COUNT][MAX_KEYS_PER_ACTION];
    int  keyCount[AID_COUNT];
    std::vector<std::string> recentGames;
    bool hotkeyEnabled[HK_COUNT];
    SDL_GameControllerButton hotkeyMod[HK_COUNT];
    SDL_GameControllerButton hotkeyButton[HK_COUNT];
    HotkeyKey hotkeyKeys[HK_COUNT][MAX_HOTKEY_KEYS_PER_ACTION];
    int  hotkeyKeyCount[HK_COUNT];
    std::map<std::string, PadMap> padMaps;

    Config()
    {
        for (int a = 0; a < AID_COUNT; ++a)
        {
            keyCount[a] = 0;
            for (int k = 0; k < MAX_KEYS_PER_ACTION; ++k)
                keys[a][k] = SDL_SCANCODE_UNKNOWN;
        }
        for (int a = 0; a < AID_COUNT; ++a)
            for (int k = 0; k < g_defaultKeyCount[a]; ++k)
                addKey(a, g_defaultKeys[a][k]);

        for (int h = 0; h < HK_COUNT; ++h)
        {
            hotkeyEnabled[h] = true;
            hotkeyMod[h] = g_defaultHotkeyMod[h];
            hotkeyButton[h] = g_defaultHotkeyButton[h];
        }
        for (int h = 0; h < HK_COUNT; ++h)
        {
            hotkeyKeyCount[h] = 0;
            for (int k = 0; k < g_defaultHotkeyKeyCount[h]; ++k)
                addHotkeyKey(h, g_defaultHotkeyKeys[h][k].scancode,
                             g_defaultHotkeyKeys[h][k].mod,
                             g_defaultHotkeyKeys[h][k].slot);
        }
    }

    void addKey(int action, SDL_Scancode sc)
    {
        if (action < 0 || action >= AID_COUNT) return;
        if (keyCount[action] >= MAX_KEYS_PER_ACTION) return;
        for (int i = 0; i < keyCount[action]; ++i)
            if (keys[action][i] == sc) return;
        keys[action][keyCount[action]++] = sc;
    }

    void removeKey(int action, int slot)
    {
        if (action < 0 || action >= AID_COUNT) return;
        if (slot < 0 || slot >= keyCount[action]) return;
        for (int i = slot; i + 1 < keyCount[action]; ++i)
            keys[action][i] = keys[action][i + 1];
        keyCount[action]--;
        if (keyCount[action] == 0)
            keys[action][keyCount[action]++] = SDL_SCANCODE_UNKNOWN;
    }

    void addHotkeyKey(int h, SDL_Scancode sc, Uint16 mod, int slot)
    {
        if (h < 0 || h >= HK_COUNT) return;
        if (hotkeyKeyCount[h] >= MAX_HOTKEY_KEYS_PER_ACTION) return;
        for (int i = 0; i < hotkeyKeyCount[h]; ++i)
            if (hotkeyKeys[h][i].scancode == sc && hotkeyKeys[h][i].mod == mod)
                return;
        hotkeyKeys[h][hotkeyKeyCount[h]].scancode = sc;
        hotkeyKeys[h][hotkeyKeyCount[h]].mod = mod;
        hotkeyKeys[h][hotkeyKeyCount[h]].slot = slot;
        hotkeyKeyCount[h]++;
    }

    void removeHotkeyKey(int h, int slot)
    {
        if (h < 0 || h >= HK_COUNT) return;
        if (slot < 0 || slot >= hotkeyKeyCount[h]) return;
        for (int i = slot; i + 1 < hotkeyKeyCount[h]; ++i)
            hotkeyKeys[h][i] = hotkeyKeys[h][i + 1];
        hotkeyKeyCount[h]--;
    }
};

static int actionToRetroId(int action)
{
    switch (action)
    {
        case AID_UP:    return RETRO_DEVICE_ID_JOYPAD_UP;
        case AID_DOWN:  return RETRO_DEVICE_ID_JOYPAD_DOWN;
        case AID_LEFT:  return RETRO_DEVICE_ID_JOYPAD_LEFT;
        case AID_RIGHT: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
        case AID_A:     return RETRO_DEVICE_ID_JOYPAD_Y;
        case AID_B:     return RETRO_DEVICE_ID_JOYPAD_B;
        case AID_C:     return RETRO_DEVICE_ID_JOYPAD_A;
        case AID_X:     return RETRO_DEVICE_ID_JOYPAD_L;
        case AID_Y:     return RETRO_DEVICE_ID_JOYPAD_X;
        case AID_Z:     return RETRO_DEVICE_ID_JOYPAD_R;
        case AID_START: return RETRO_DEVICE_ID_JOYPAD_START;
        case AID_MODE:  return RETRO_DEVICE_ID_JOYPAD_SELECT;
        default:        return -1;
    }
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static Emulator g_emu;
static Config   g_cfg;

static SDL_Window*   g_window   = nullptr;
static SDL_GLContext g_glContext= nullptr;
static SDL_AudioDeviceID g_audioDev = 0;

static GLuint g_tex = 0;
static int g_texW = 0, g_texH = 0;
static std::vector<uint8_t> g_rgba;

static GLuint g_maskTex = 0;      // ready-made black MD side-mask overlay texture

static bool g_showVideo   = false;
static bool g_showInput   = false;
static bool g_showAbout   = false;
static int  g_rebindAction = -1;
static int  g_rebindSlot   = -1;
static int  g_rebindHotkey  = -1;
static int  g_rebindHotkeyKey = -1;
static int  g_rebindHotkeyKeySlot = -1;
static SDL_GameControllerButton g_rebindPendingMod = SDL_CONTROLLER_BUTTON_INVALID;
static int  g_homeIndex = 0;
static Uint64 g_homeNavRepeatMs = 0;
static bool   g_homeNavHolding = false;
static int    g_homeNavHoldDir = 0;
static bool   g_homePrevConfirm = false;
static bool   g_homePrevDelete  = false;
static bool   g_running = true;
static bool   g_quitSaved = false;
static Uint64 g_lastThumbMs = 0;
static Uint64 g_lastThemeCheckMs = 0;
static int   g_lastAppliedDark = -1;
static ImVec4 g_clearColor(0.02f, 0.02f, 0.03f, 1.0f);
static volatile bool g_quitRequested = false;

static BOOL WINAPI consoleCtrlHandler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT)
    {
        g_quitRequested = true;   // handled in the main loop so state can be saved
        return TRUE;
    }
    return FALSE;
}

static std::vector<Pad> g_pads;
static int  g_rebindPad        = -1;
static int  g_rebindPadAction  = -1;
static int  g_rebindPadSlot    = -1;
static bool g_padRebindReady   = false;

// ---------------------------------------------------------------------------
// Localization
// ---------------------------------------------------------------------------
static const char* tr(const char* en, const char* zh)
{
    return (g_cfg.language == 1) ? zh : en;
}

static const char* actionName(int a)
{
    if (a < 0 || a >= AID_COUNT) return "?";
    if (g_cfg.language == 1)
    {
        static const char* zh[AID_COUNT] = {
            "上", "下", "左", "右",
            "A", "B", "C", "X", "Y", "Z",
            "开始", "模式"
        };
        return zh[a];
    }
    return g_actionNames[a];
}

static const char* comboWindowScale()
{
    return (g_cfg.language == 1)
        ? "适应窗口\0原始\0x2\0x3\0x4\0x5\0"
        : "Fit to window\0Original\0x2\0x3\0x4\0x5\0";
}

static const char* comboVideoFilter()
{
    return (g_cfg.language == 1)
        ? "无\0双线性\0LCD 网格\0CRT\0扫描线\0"
        : "None\0Bilinear\0LCD Grid\0CRT\0Scanlines\0";
}

static const char* comboAspectRatio()
{
    return (g_cfg.language == 1)
        ? "自动\0" "4:3\0" "原始像素\0"
        : "Auto\0" "4:3\0" "Original pixels\0";
}


static const char* comboPlayer()
{
    return (g_cfg.language == 1)
        ? "玩家 1\0玩家 2\0"
        : "Player 1\0Player 2\0";
}

// ---------------------------------------------------------------------------
// Config persistence (simple key=value text file)
// ---------------------------------------------------------------------------
static std::string configPath()
{
    char exe[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = exe;
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos)
        dir = dir.substr(0, pos);
    return dir + "\\mesenmd.cfg";
}

// Resolve a path to an asset that ships next to the executable.
static std::string assetPath(const char* name)
{
    char exe[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = exe;
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos)
        dir = dir.substr(0, pos);
    return dir + "\\" + name;
}

static void populateScancodeNames()
{
    for (int i = 0; i < SDL_NUM_SCANCODES; ++i)
    {
        const char* name = SDL_GetScancodeName((SDL_Scancode)i);
        g_scancodeNames[i] = name ? name : "";
    }
}

static std::wstring utf8ToWide(const std::string& s);

static bool writeFileAtomic(const std::string& path, const void* data, size_t size)
{
    std::wstring wpath = utf8ToWide(path);
    if (wpath.empty())
        return false;

    std::wstring tmp = wpath + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    bool ok = WriteFile(h, data, (DWORD)size, &written, nullptr) != 0 && written == size;
    if (ok)
        ok = FlushFileBuffers(h) != 0;
    CloseHandle(h);

    if (!ok)
    {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

static void saveConfig()
{
    std::ostringstream ss;
    ss << "videoScale=" << g_cfg.videoScale << "\n";
    ss << "filterBilinear=" << (g_cfg.filterBilinear ? 1 : 0) << "\n";
    ss << "showFps=" << (g_cfg.showFps ? 1 : 0) << "\n";
    ss << "audioEnabled=" << (g_cfg.audioEnabled ? 1 : 0) << "\n";
    ss << "videoFilter=" << g_cfg.videoFilter << "\n";
    ss << "maskEnabled=" << (g_cfg.maskEnabled ? 1 : 0) << "\n";
    ss << "maskType=" << g_cfg.maskType << "\n";
    ss << "autoSaveState=" << (g_cfg.autoSaveState ? 1 : 0) << "\n";
    ss << "autoLoadState=" << (g_cfg.autoLoadState ? 1 : 0) << "\n";
    for (int a = 0; a < AID_COUNT; ++a)
        for (int k = 0; k < g_cfg.keyCount[a]; ++k)
            ss << "key" << a << "_" << k << "=" << (int)g_cfg.keys[a][k] << "\n";
    ss << "keyboardPlayer=" << g_cfg.keyboardPlayer << "\n";
    ss << "fullscreen=" << (g_cfg.fullscreen ? 1 : 0) << "\n";
    ss << "aspectRatio=" << g_cfg.aspectRatio << "\n";
    ss << "language=" << g_cfg.language << "\n";
    ss << "theme=" << g_cfg.theme << "\n";
    ss << "windowX=" << g_cfg.windowX << "\n";
    ss << "windowY=" << g_cfg.windowY << "\n";
    ss << "windowW=" << g_cfg.windowW << "\n";
    ss << "windowH=" << g_cfg.windowH << "\n";
    ss << "windowMaximized=" << (g_cfg.windowMaximized ? 1 : 0) << "\n";
    ss << "windowSaved=" << (g_cfg.windowSaved ? 1 : 0) << "\n";
    for (size_t i = 0; i < g_cfg.recentGames.size(); ++i)
        ss << "recent=" << g_cfg.recentGames[i] << "\n";
    for (int h = 0; h < HK_COUNT; ++h)
    {
        ss << "hotkeyEnabled" << h << "=" << (g_cfg.hotkeyEnabled[h] ? 1 : 0) << "\n";
        ss << "hotkeyMod" << h << "=" << (int)g_cfg.hotkeyMod[h] << "\n";
        ss << "hotkeyButton" << h << "=" << (int)g_cfg.hotkeyButton[h] << "\n";
    }
    for (int h = 0; h < HK_COUNT; ++h)
        for (int k = 0; k < g_cfg.hotkeyKeyCount[h]; ++k)
        {
            ss << "hotkeyKey" << h << "_" << k << "=" << (int)g_cfg.hotkeyKeys[h][k].scancode << "\n";
            ss << "hotkeyKeyMod" << h << "_" << k << "=" << (int)g_cfg.hotkeyKeys[h][k].mod << "\n";
            ss << "hotkeyKeySlot" << h << "_" << k << "=" << (int)g_cfg.hotkeyKeys[h][k].slot << "\n";
        }
    for (auto& kv : g_cfg.padMaps)
    {
        const std::string& guid = kv.first;
        const PadMap& pm = kv.second;
        for (int a = 0; a < AID_COUNT; ++a)
            for (int k = 0; k < pm.mapCount[a]; ++k)
                ss << "padmap." << guid << "." << a << "." << k << "=" << pm.map[a][k] << "\n";
    }
    std::string text = ss.str();
    writeFileAtomic(configPath(), text.data(), text.size());
}

static void loadConfig()
{
    std::ifstream f(configPath());
    if (!f) return;
    std::string line;
    bool keyParsed[AID_COUNT] = { false };
    bool hotkeyKeyParsed[HK_COUNT] = { false };
    while (std::getline(f, line))
    {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "videoScale")       g_cfg.videoScale = std::atoi(val.c_str());
        else if (key == "filterBilinear") g_cfg.filterBilinear = std::atoi(val.c_str()) != 0;
        else if (key == "showFps")     g_cfg.showFps = std::atoi(val.c_str()) != 0;
        else if (key == "audioEnabled") g_cfg.audioEnabled = std::atoi(val.c_str()) != 0;
        else if (key == "videoFilter") g_cfg.videoFilter = std::atoi(val.c_str());
        else if (key == "maskEnabled") g_cfg.maskEnabled = std::atoi(val.c_str()) != 0;
        else if (key == "maskType") g_cfg.maskType = std::atoi(val.c_str());
        else if (key == "autoSaveState") g_cfg.autoSaveState = std::atoi(val.c_str()) != 0;
        else if (key == "autoLoadState") g_cfg.autoLoadState = std::atoi(val.c_str()) != 0;
        else if (key == "keyboardPlayer") g_cfg.keyboardPlayer = std::atoi(val.c_str());
        else if (key == "fullscreen") g_cfg.fullscreen = std::atoi(val.c_str()) != 0;
        else if (key == "aspectRatio") g_cfg.aspectRatio = std::atoi(val.c_str());
        else if (key == "language") g_cfg.language = std::atoi(val.c_str());
        else if (key == "theme") g_cfg.theme = std::atoi(val.c_str());
        else if (key == "windowX") { g_cfg.windowX = std::atoi(val.c_str()); g_cfg.windowSaved = true; }
        else if (key == "windowY") { g_cfg.windowY = std::atoi(val.c_str()); g_cfg.windowSaved = true; }
        else if (key == "windowW") { g_cfg.windowW = std::atoi(val.c_str()); g_cfg.windowSaved = true; }
        else if (key == "windowH") { g_cfg.windowH = std::atoi(val.c_str()); g_cfg.windowSaved = true; }
        else if (key == "windowMaximized") g_cfg.windowMaximized = std::atoi(val.c_str()) != 0;
        else if (key == "windowSaved") g_cfg.windowSaved = std::atoi(val.c_str()) != 0;
        else if (key.rfind("hotkeyEnabled", 0) == 0)
        {
            int h = -1;
            if (sscanf(key.c_str() + 13, "%d", &h) == 1 && h >= 0 && h < HK_COUNT)
                g_cfg.hotkeyEnabled[h] = std::atoi(val.c_str()) != 0;
        }
        else if (key.rfind("hotkeyMod", 0) == 0)
        {
            int h = -1;
            if (sscanf(key.c_str() + 9, "%d", &h) == 1 && h >= 0 && h < HK_COUNT)
                g_cfg.hotkeyMod[h] = (SDL_GameControllerButton)std::atoi(val.c_str());
        }
        else if (key.rfind("hotkeyButton", 0) == 0)
        {
            int h = -1;
            if (sscanf(key.c_str() + 12, "%d", &h) == 1 && h >= 0 && h < HK_COUNT)
                g_cfg.hotkeyButton[h] = (SDL_GameControllerButton)std::atoi(val.c_str());
        }
        else if (key.rfind("hotkeyKeyMod", 0) == 0)
        {
            int idx = -1, slot = -1;
            if (sscanf(key.c_str() + 12, "%d_%d", &idx, &slot) == 2 &&
                idx >= 0 && idx < HK_COUNT && slot >= 0 && slot < MAX_HOTKEY_KEYS_PER_ACTION)
                g_cfg.hotkeyKeys[idx][slot].mod = (Uint16)std::atoi(val.c_str());
        }
        else if (key.rfind("hotkeyKeySlot", 0) == 0)
        {
            int idx = -1, slot = -1;
            if (sscanf(key.c_str() + 13, "%d_%d", &idx, &slot) == 2 &&
                idx >= 0 && idx < HK_COUNT && slot >= 0 && slot < MAX_HOTKEY_KEYS_PER_ACTION)
                g_cfg.hotkeyKeys[idx][slot].slot = std::atoi(val.c_str());
        }
        else if (key.rfind("hotkeyKey", 0) == 0)
        {
            int idx = -1, slot = -1;
            if (sscanf(key.c_str() + 9, "%d_%d", &idx, &slot) == 2 &&
                idx >= 0 && idx < HK_COUNT && slot >= 0 && slot < MAX_HOTKEY_KEYS_PER_ACTION)
            {
                if (!hotkeyKeyParsed[idx])
                {
                    hotkeyKeyParsed[idx] = true;
                    g_cfg.hotkeyKeyCount[idx] = 0;
                }
                g_cfg.hotkeyKeys[idx][slot].scancode = (SDL_Scancode)std::atoi(val.c_str());
                if (slot + 1 > g_cfg.hotkeyKeyCount[idx])
                    g_cfg.hotkeyKeyCount[idx] = slot + 1;
            }
        }
        else if (key.rfind("key", 0) == 0)
        {
            int idx = -1, slot = -1;
            if (sscanf(key.c_str() + 3, "%d_%d", &idx, &slot) == 2) { }
            else if (sscanf(key.c_str() + 3, "%d", &idx) == 1) slot = 0;
            if (idx >= 0 && idx < AID_COUNT && slot >= 0 && slot < MAX_KEYS_PER_ACTION)
            {
                if (!keyParsed[idx])
                {
                    keyParsed[idx] = true;
                    g_cfg.keyCount[idx] = 0;
                }
                g_cfg.keys[idx][slot] = (SDL_Scancode)std::atoi(val.c_str());
                if (slot + 1 > g_cfg.keyCount[idx])
                    g_cfg.keyCount[idx] = slot + 1;
            }
        }
        else if (key.rfind("padmap.", 0) == 0)
        {
            std::string rest = key.substr(7);
            size_t d1 = rest.find('.');
            if (d1 != std::string::npos)
            {
                std::string guid = rest.substr(0, d1);
                std::string rest2 = rest.substr(d1 + 1);
                size_t d2 = rest2.find('.');
                if (d2 != std::string::npos)
                {
                    int action = std::atoi(rest2.substr(0, d2).c_str());
                    int slot   = std::atoi(rest2.substr(d2 + 1).c_str());
                    if (action >= 0 && action < AID_COUNT && slot >= 0 && slot < MAX_PAD_BINDINGS_PER_ACTION)
                    {
                        PadMap& pm = g_cfg.padMaps[guid];
                        pm.map[action][slot] = std::atoi(val.c_str());
                        if (slot + 1 > pm.mapCount[action])
                            pm.mapCount[action] = slot + 1;
                    }
                }
            }
        }
        else if (key == "recent")
            g_cfg.recentGames.push_back(val);
    }
}

static bool systemUsesDarkTheme()
{
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS)
    {
        DWORD value = 1, size = sizeof(value);
        if (RegQueryValueExW(hkey, L"AppsUseLightTheme", nullptr, nullptr,
                             (LPBYTE)&value, &size) == ERROR_SUCCESS)
        {
            RegCloseKey(hkey);
            return value == 0;
        }
        RegCloseKey(hkey);
    }
    return true;
}

static void applyTheme(bool force)
{
    bool dark = (g_cfg.theme == 1) ? true
              : (g_cfg.theme == 2) ? false
              : systemUsesDarkTheme();
    int d = dark ? 1 : 0;
    if (!force && d == g_lastAppliedDark)
        return;
    g_lastAppliedDark = d;

    if (dark)
        ImGui::StyleColorsDark();
    else
        ImGui::StyleColorsLight();

    if (dark)
        g_clearColor = ImVec4(0.02f, 0.02f, 0.03f, 1.0f);
    else
        g_clearColor = ImVec4(0.86f, 0.87f, 0.89f, 1.0f);

    ImGuiStyle& style = ImGui::GetStyle();
    if (dark)
    {
        style.Colors[ImGuiCol_WindowBg]   = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg]  = ImVec4(0.09f, 0.09f, 0.11f, 1.0f);
        style.Colors[ImGuiCol_FrameBg]    = ImVec4(0.13f, 0.13f, 0.16f, 1.0f);
        style.Colors[ImGuiCol_TitleBg]    = ImVec4(0.09f, 0.09f, 0.11f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.16f, 1.0f);
    }
    else
    {
        style.Colors[ImGuiCol_WindowBg]   = ImVec4(0.96f, 0.96f, 0.97f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg]  = ImVec4(0.90f, 0.91f, 0.93f, 1.0f);
        style.Colors[ImGuiCol_FrameBg]    = ImVec4(0.89f, 0.90f, 0.92f, 1.0f);
        style.Colors[ImGuiCol_TitleBg]    = ImVec4(0.90f, 0.91f, 0.93f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.85f, 0.86f, 0.90f, 1.0f);
    }
}

static void addRecent(const std::string& path)
{
    auto it = std::find(g_cfg.recentGames.begin(), g_cfg.recentGames.end(), path);
    if (it != g_cfg.recentGames.end())
        g_cfg.recentGames.erase(it);
    g_cfg.recentGames.insert(g_cfg.recentGames.begin(), path);
    if (g_cfg.recentGames.size() > 12)
        g_cfg.recentGames.resize(12);
    saveConfig();
}

// ---------------------------------------------------------------------------
// Save-state helpers
// ---------------------------------------------------------------------------
static std::wstring utf8ToWide(const std::string& s)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (wlen <= 1) return std::wstring();
    std::wstring w(static_cast<size_t>(wlen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], wlen - 1);
    return w;
}

static bool fileExists(const std::string& path)
{
    std::wstring w = utf8ToWide(path);
    if (w.empty()) return false;
    DWORD attrs = GetFileAttributesW(w.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
}

static std::string statePathForRom()
{
    return g_emu.romPath() + ".state";
}

static std::string slotStatePathForRom(int slot)
{
    return g_emu.romPath() + "." + std::to_string(slot) + ".state";
}

static void autoSaveState()
{
    if (!g_cfg.autoSaveState || !g_emu.gameLoaded())
        return;
    if (g_emu.saveState(statePathForRom()))
        g_emu.setLastMessage("State saved.");
}

static void loadStateForRom(bool force)
{
    if (!g_emu.gameLoaded())
        return;
    if (!force && !g_cfg.autoLoadState)
        return;
    std::string p = statePathForRom();
    if (fileExists(p) && g_emu.loadState(p))
        g_emu.setLastMessage("State loaded.");
}

static void stopAudio();  // defined in audio section below
static void saveThumbnail();  // defined in home-page section below

static void saveSession()
{
    if (!g_emu.gameLoaded())
        return;
    autoSaveState();
    saveThumbnail();
}

static void closeGame()
{
    saveSession();
    g_emu.unloadGame();
    stopAudio();
    if (g_cfg.fullscreen)
    {
        SDL_SetWindowFullscreen(g_window, 0);
        g_cfg.fullscreen = false;
        saveConfig();
    }
    SDL_SetWindowTitle(g_window, "MesenMD");
}

// ---------------------------------------------------------------------------
// Audio queue helpers
// ---------------------------------------------------------------------------
static bool startAudio(double sampleRate)
{
    if (!g_cfg.audioEnabled) return false;
    SDL_AudioSpec want{}, have{};
    want.freq = (int)(sampleRate > 1000 ? sampleRate : 48000);
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = nullptr;
    g_audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audioDev)
        SDL_PauseAudioDevice(g_audioDev, 0);
    return g_audioDev != 0;
}

static void stopAudio()
{
    if (g_audioDev)
    {
        SDL_CloseAudioDevice(g_audioDev);
        g_audioDev = 0;
    }
}

static void togglePause()
{
    if (!g_emu.gameLoaded())
        return;
    bool nowPaused = !g_emu.paused();
    g_emu.setPaused(nowPaused);
    if (g_audioDev)
    {
        if (nowPaused)
        {
            SDL_PauseAudioDevice(g_audioDev, 1);
            SDL_ClearQueuedAudio(g_audioDev);
        }
        else
        {
            SDL_PauseAudioDevice(g_audioDev, 0);
        }
    }
}


static void doQuitSave()
{
    if (g_quitSaved) return;
    g_quitSaved = true;
    saveSession();
}

static void requestQuit()
{
    if (g_quitRequested) return;
    g_quitRequested = true;
    doQuitSave();
    g_running = false;
}

static void toggleFullscreen()
{
    Uint32 flags = SDL_GetWindowFlags(g_window);
    if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
    {
        SDL_SetWindowFullscreen(g_window, 0);
        g_cfg.fullscreen = false;
    }
    else
    {
        SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        g_cfg.fullscreen = true;
    }
}

static bool isFullscreenActive()
{
    return (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

// ---------------------------------------------------------------------------
// ROM loading
// ---------------------------------------------------------------------------
static std::string utf16ToUtf8(const wchar_t* s)
{
    if (!s) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, &out[0], len - 1, nullptr, nullptr);
    return out;
}
static std::string openRomDialog(SDL_Window* window)
{
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(window, &info);
    HWND hwnd = (info.subsystem == SDL_SYSWM_WINDOWS) ? info.info.win.window : nullptr;

    wchar_t file[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Sega Mega Drive ROMs\0*.md;*.bin;*.gen;*.smd;*.sg;*.68k\0All Files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn))
        return utf16ToUtf8(file);
    return "";
}

static void loadRom(const std::string& path, bool resumeState = false)
{
    if (path.empty())
        return;

    if (g_emu.gameLoaded())
        saveSession();
    stopAudio();

    if (!g_emu.loadGame(path))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MesenMD",
                                 ("Failed to load ROM:\n" + path).c_str(), g_window);
        return;
    }

    startAudio(g_emu.sampleRate());
    addRecent(path);
    loadStateForRom(resumeState);
    SDL_SetWindowTitle(g_window, ("MesenMD - " + path).c_str());
}

// ---------------------------------------------------------------------------
// Video texture update
// ---------------------------------------------------------------------------
static void updateVideoTexture()
{
    if (!g_emu.gameLoaded() || g_emu.videoWidth() == 0 || g_emu.videoHeight() == 0)
        return;

    int w = (int)g_emu.videoWidth();
    int h = (int)g_emu.videoHeight();
    const std::vector<uint8_t>& frame = g_emu.videoFrame();
    if (frame.empty())
        return;

    bool rgb565 = (g_emu.pixelFormat() != RETRO_PIXEL_FORMAT_XRGB8888);
    size_t bpp = rgb565 ? 2 : 4;
    size_t pitch = g_emu.videoPitch() ? g_emu.videoPitch() : (size_t)w * bpp;

    g_rgba.resize((size_t)w * h * 4);

    if (rgb565)
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t* row = frame.data() + (size_t)y * pitch;
            for (int x = 0; x < w; ++x)
            {
                uint16_t p = (uint16_t)(row[x*2]) | ((uint16_t)(row[x*2+1]) << 8);
                uint8_t r = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
                uint8_t g = (uint8_t)(((p >> 5)  & 0x3F) * 255 / 63);
                uint8_t b = (uint8_t)((p & 0x1F) * 255 / 31);
                uint8_t* dst = &g_rgba[((size_t)y * w + x) * 4];
                dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 255;
            }
        }
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t* row = frame.data() + (size_t)y * pitch;
            for (int x = 0; x < w; ++x)
            {
                const uint8_t* p = row + x * 4;
                uint8_t* dst = &g_rgba[((size_t)y * w + x) * 4];
                dst[0] = p[2]; dst[1] = p[1]; dst[2] = p[0]; dst[3] = 255;
            }
        }
    }

    if (!g_tex)
        glGenTextures(1, &g_tex);
    if (w != g_texW || h != g_texH)
    {
        g_texW = w; g_texH = h;
        glDeleteTextures(1, &g_tex);
        glGenTextures(1, &g_tex);
    }
    glBindTexture(GL_TEXTURE_2D, g_tex);
    bool useLinear = (g_cfg.videoFilter == 1) || (g_cfg.videoFilter == 0 && g_cfg.filterBilinear);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, useLinear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, useLinear ? GL_LINEAR : GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_rgba.data());
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static std::string padGUID(SDL_Joystick* joy)
{
    SDL_JoystickGUID g = SDL_JoystickGetGUID(joy);
    char buf[64] = { 0 };
    SDL_JoystickGetGUIDString(g, buf, sizeof(buf));
    return buf;
}

static void resetPadMappingToDefault(Pad& pad)
{
    for (int a = 0; a < AID_COUNT; ++a)
    {
        pad.map.mapCount[a] = 0;
        for (int k = 0; k < MAX_PAD_BINDINGS_PER_ACTION; ++k)
            pad.map.map[a][k] = 0;
    }
    auto add = [&](int a, int code)
    {
        if (a >= 0 && a < AID_COUNT && pad.map.mapCount[a] < MAX_PAD_BINDINGS_PER_ACTION)
            pad.map.map[a][pad.map.mapCount[a]++] = code;
    };
    if (pad.gc)
    {
        add(AID_A,     padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_X));
        add(AID_B,     padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_A));
        add(AID_C,     padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_B));
        add(AID_X,     padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        add(AID_Y,     padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_Y));
        add(AID_Z,     padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        add(AID_START, padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_START));
        add(AID_MODE,  padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_BACK));
        add(AID_UP,    padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_UP));
        add(AID_DOWN,  padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_DOWN));
        add(AID_LEFT,  padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_LEFT));
        add(AID_RIGHT, padInput(PAD_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
    }
    else
    {
        if (pad.hatCount > 0)
        {
            add(AID_UP,    padInput(PAD_HAT_UP, 0));
            add(AID_DOWN,  padInput(PAD_HAT_DOWN, 0));
            add(AID_LEFT,  padInput(PAD_HAT_LEFT, 0));
            add(AID_RIGHT, padInput(PAD_HAT_RIGHT, 0));
        }
        else if (pad.axisCount >= 2)
        {
            add(AID_LEFT,  padInput(PAD_AXIS_NEG, 0));
            add(AID_RIGHT, padInput(PAD_AXIS_POS, 0));
            add(AID_UP,    padInput(PAD_AXIS_NEG, 1));
            add(AID_DOWN,  padInput(PAD_AXIS_POS, 1));
        }
        else if (pad.buttonCount >= 10)
        {
            // Cheap DirectInput pads often expose a button-based D-pad as the
            // highest-numbered buttons. Bind the last four (which sit above the
            // six face buttons 0-5) so the D-pad works even when there is no
            // hat or stick to read, without colliding with the face buttons.
            add(AID_UP,    padInput(PAD_BUTTON, pad.buttonCount - 4));
            add(AID_DOWN,  padInput(PAD_BUTTON, pad.buttonCount - 3));
            add(AID_LEFT,  padInput(PAD_BUTTON, pad.buttonCount - 2));
            add(AID_RIGHT, padInput(PAD_BUTTON, pad.buttonCount - 1));
        }
        if (pad.buttonCount >= 1) add(AID_A, padInput(PAD_BUTTON, 0));
        if (pad.buttonCount >= 2) add(AID_B, padInput(PAD_BUTTON, 1));
        if (pad.buttonCount >= 3) add(AID_C, padInput(PAD_BUTTON, 2));
        if (pad.buttonCount >= 4) add(AID_X, padInput(PAD_BUTTON, 3));
        if (pad.buttonCount >= 5) add(AID_Y, padInput(PAD_BUTTON, 4));
        if (pad.buttonCount >= 6) add(AID_Z, padInput(PAD_BUTTON, 5));
    }
}

static void openPad(int deviceIndex)
{
    if (deviceIndex < 0 || deviceIndex >= SDL_NumJoysticks())
        return;
    SDL_JoystickID instanceId = SDL_JoystickGetDeviceInstanceID(deviceIndex);
    if (instanceId < 0)
        return;
    for (const Pad& p : g_pads)
        if (p.instanceId == instanceId)
            return;

    Pad pad;
    if (SDL_IsGameController(deviceIndex))
    {
        pad.gc = SDL_GameControllerOpen(deviceIndex);
        if (!pad.gc)
            return;
        pad.joy = SDL_GameControllerGetJoystick(pad.gc);
    }
    else
    {
        pad.joy = SDL_JoystickOpen(deviceIndex);
        if (!pad.joy)
            return;
    }

    pad.instanceId   = instanceId;
    pad.name         = (SDL_JoystickName(pad.joy) && *SDL_JoystickName(pad.joy)) ? SDL_JoystickName(pad.joy) : tr("Gamepad", "手柄");
    pad.guid         = padGUID(pad.joy);
    pad.buttonCount  = SDL_JoystickNumButtons(pad.joy);
    pad.axisCount    = SDL_JoystickNumAxes(pad.joy);
    pad.hatCount     = SDL_JoystickNumHats(pad.joy);
    pad.player       = (int)(g_pads.size() % 2);

    auto it = g_cfg.padMaps.find(pad.guid);
    if (it != g_cfg.padMaps.end())
        pad.map = it->second;
    else
        resetPadMappingToDefault(pad);

    g_pads.push_back(pad);
}

static void closePad(SDL_JoystickID instanceId)
{
    for (size_t i = 0; i < g_pads.size(); ++i)
    {
        if (g_pads[i].instanceId != instanceId)
            continue;
        if (g_pads[i].gc)
            SDL_GameControllerClose(g_pads[i].gc);
        else if (g_pads[i].joy)
            SDL_JoystickClose(g_pads[i].joy);
        if (g_rebindPad == (int)i)
        {
            g_rebindPad = -1;
            g_rebindPadAction = -1;
            g_rebindPadSlot = -1;
            g_padRebindReady = false;
        }
        g_pads.erase(g_pads.begin() + i);
        return;
    }
}

static void enumeratePads()
{
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        openPad(i);
}

static void shutdownPads()
{
    for (Pad& p : g_pads)
    {
        if (p.gc)
            SDL_GameControllerClose(p.gc);
        else if (p.joy)
            SDL_JoystickClose(p.joy);
    }
    g_pads.clear();
}

static SDL_GameController* findController(SDL_JoystickID instanceId)
{
    for (Pad& p : g_pads)
        if (p.gc && p.instanceId == instanceId)
            return p.gc;
    return nullptr;
}

static const char* buttonName(SDL_GameControllerButton b);
static bool padInputActive(const Pad& pad, int code)
{
    int kind = padInputKind(code);
    int idx  = padInputIndex(code);
    switch (kind)
    {
        case PAD_BUTTON:
            if (pad.gc)
                return (idx < (int)SDL_CONTROLLER_BUTTON_MAX)
                    ? SDL_GameControllerGetButton(pad.gc, (SDL_GameControllerButton)idx) != 0
                    : false;
            return SDL_JoystickGetButton(pad.joy, idx) != 0;
        case PAD_AXIS_NEG:
        case PAD_AXIS_POS:
        {
            int v;
            if (pad.gc)
            {
                if (idx >= (int)SDL_CONTROLLER_AXIS_MAX) return false;
                v = SDL_GameControllerGetAxis(pad.gc, (SDL_GameControllerAxis)idx);
            }
            else
            {
                v = SDL_JoystickGetAxis(pad.joy, idx);
            }
            return (kind == PAD_AXIS_NEG) ? (v < -20000) : (v > 20000);
        }
        case PAD_HAT_UP:
        case PAD_HAT_DOWN:
        case PAD_HAT_LEFT:
        case PAD_HAT_RIGHT:
        {
            Uint8 hat = SDL_JoystickGetHat(pad.joy, idx);
            switch (kind)
            {
                case PAD_HAT_UP:    return (hat & SDL_HAT_UP) != 0;
                case PAD_HAT_DOWN:  return (hat & SDL_HAT_DOWN) != 0;
                case PAD_HAT_LEFT:  return (hat & SDL_HAT_LEFT) != 0;
                case PAD_HAT_RIGHT: return (hat & SDL_HAT_RIGHT) != 0;
            }
            return false;
        }
    }
    return false;
}

static int firstActivePadInput(const Pad& pad)
{
    int bCount = pad.gc ? ((pad.buttonCount < (int)SDL_CONTROLLER_BUTTON_MAX) ? pad.buttonCount : (int)SDL_CONTROLLER_BUTTON_MAX)
                        : pad.buttonCount;
    for (int b = 0; b < bCount; ++b)
        if (padInputActive(pad, padInput(PAD_BUTTON, b)))
            return padInput(PAD_BUTTON, b);
    for (int h = 0; h < pad.hatCount; ++h)
    {
        Uint8 hat = SDL_JoystickGetHat(pad.joy, h);
        if (hat & SDL_HAT_UP)    return padInput(PAD_HAT_UP, h);
        if (hat & SDL_HAT_DOWN)  return padInput(PAD_HAT_DOWN, h);
        if (hat & SDL_HAT_LEFT)  return padInput(PAD_HAT_LEFT, h);
        if (hat & SDL_HAT_RIGHT) return padInput(PAD_HAT_RIGHT, h);
    }
    int axCount = pad.gc ? ((pad.axisCount < (int)SDL_CONTROLLER_AXIS_MAX) ? pad.axisCount : (int)SDL_CONTROLLER_AXIS_MAX)
                         : pad.axisCount;
    for (int ax = 0; ax < axCount; ++ax)
    {
        int v = pad.gc ? SDL_GameControllerGetAxis(pad.gc, (SDL_GameControllerAxis)ax)
                       : SDL_JoystickGetAxis(pad.joy, ax);
        if (v < -20000) return padInput(PAD_AXIS_NEG, ax);
        if (v >  20000) return padInput(PAD_AXIS_POS, ax);
    }
    return -1;
}

static std::string padInputName(const Pad& pad, int code)
{
    int kind = padInputKind(code), idx = padInputIndex(code);
    char buf[32];
    if (kind == PAD_BUTTON)
    {
        if (pad.gc && idx >= 0 && idx < (int)SDL_CONTROLLER_BUTTON_MAX)
            return buttonName((SDL_GameControllerButton)idx);
        snprintf(buf, sizeof(buf), "%s %d", tr("Button", "按钮"), idx);
        return buf;
    }
    if (kind == PAD_AXIS_NEG) { snprintf(buf, sizeof(buf), "%s %d -", tr("Axis", "轴"), idx); return buf; }
    if (kind == PAD_AXIS_POS) { snprintf(buf, sizeof(buf), "%s %d +", tr("Axis", "轴"), idx); return buf; }
    if (kind == PAD_HAT_UP)    return tr("D-Pad Up", "方向键上");
    if (kind == PAD_HAT_DOWN)  return tr("D-Pad Down", "方向键下");
    if (kind == PAD_HAT_LEFT)  return tr("D-Pad Left", "方向键左");
    if (kind == PAD_HAT_RIGHT) return tr("D-Pad Right", "方向键右");
    return "?";
}

static uint32_t padMask(const Pad& pad)
{
    uint32_t m = 0;
    for (int a = 0; a < AID_COUNT; ++a)
        for (int k = 0; k < pad.map.mapCount[a]; ++k)
            if (padInputActive(pad, pad.map.map[a][k]))
                m |= (1u << actionToRetroId(a));
    return m;
}

static void pollPadRebind()
{
    if (g_rebindPadAction < 0)
        return;
    if (g_rebindPad < 0 || g_rebindPad >= (int)g_pads.size())
    {
        g_rebindPad = -1;
        g_rebindPadAction = -1;
        g_rebindPadSlot = -1;
        g_padRebindReady = false;
        return;
    }
    Pad& pad = g_pads[g_rebindPad];
    int active = firstActivePadInput(pad);
    if (active < 0)
    {
        g_padRebindReady = true;
        return;
    }
    if (!g_padRebindReady)
        return;
    int a = g_rebindPadAction;
    int slot = g_rebindPadSlot;
    if (slot < 0 || slot >= pad.map.mapCount[a])
        slot = pad.map.mapCount[a];
    if (slot >= MAX_PAD_BINDINGS_PER_ACTION)
    {
        g_rebindPad = -1;
        g_rebindPadAction = -1;
        g_rebindPadSlot = -1;
        g_padRebindReady = false;
        return;
    }
    pad.map.map[a][slot] = active;
    if (slot + 1 > pad.map.mapCount[a])
        pad.map.mapCount[a] = slot + 1;
    g_cfg.padMaps[pad.guid] = pad.map;
    saveConfig();
    g_rebindPad = -1;
    g_rebindPadAction = -1;
    g_rebindPadSlot = -1;
    g_padRebindReady = false;
}

static void updateInput()
{
    if (!g_emu.gameLoaded())
        return;

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    uint32_t mask[4] = {0, 0, 0, 0};

    {
        int p = (g_cfg.keyboardPlayer >= 0 && g_cfg.keyboardPlayer < 2) ? g_cfg.keyboardPlayer : 0;
        uint32_t m = 0;
        for (int a = 0; a < AID_COUNT; ++a)
            for (int k = 0; k < g_cfg.keyCount[a]; ++k)
                if (keys[g_cfg.keys[a][k]])
                    m |= (1u << actionToRetroId(a));
        mask[p] |= m;
    }

    for (size_t i = 0; i < g_pads.size(); ++i)
    {
        int p = g_pads[i].player;
        if (p < 0 || p >= 2) p = 0;
        mask[p] |= padMask(g_pads[i]);
    }

    g_emu.setJoypadMask(0, mask[0]);
    g_emu.setJoypadMask(1, mask[1]);
}

static const char* buttonName(SDL_GameControllerButton b)
{
    switch (b)
    {
        case SDL_CONTROLLER_BUTTON_A: return "A";
        case SDL_CONTROLLER_BUTTON_B: return "B";
        case SDL_CONTROLLER_BUTTON_X: return "X";
        case SDL_CONTROLLER_BUTTON_Y: return "Y";
        case SDL_CONTROLLER_BUTTON_BACK: return "Select";
        case SDL_CONTROLLER_BUTTON_START: return "Start";
        case SDL_CONTROLLER_BUTTON_GUIDE: return "Guide";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "LB";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "RB";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "L3";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "R3";
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return "D-Up";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "D-Down";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "D-Left";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "D-Right";
        default: return "?";
    }
}

static const char* hotkeyName(int h)
{
    if (h < 0 || h >= HK_COUNT) return "?";
    return (g_cfg.language == 1) ? g_hotkeyNamesChinese[h] : g_hotkeyNamesEnglish[h];
}

static bool modLikeButton(SDL_GameControllerButton b)
{
    return b == SDL_CONTROLLER_BUTTON_BACK || b == SDL_CONTROLLER_BUTTON_START ||
           b == SDL_CONTROLLER_BUTTON_GUIDE || b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
           b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER || b == SDL_CONTROLLER_BUTTON_LEFTSTICK ||
           b == SDL_CONTROLLER_BUTTON_RIGHTSTICK;
}

static void triggerHotkey(int h, int slot = 1)
{
    bool inGame = g_emu.gameLoaded();
    switch (h)
    {
        case HK_SAVE:
            if (inGame)
            {
                std::string p = slotStatePathForRom(slot > 0 ? slot : 1);
                if (g_emu.saveState(p))
                    g_emu.setLastMessage("State saved.");
            }
            break;
        case HK_LOAD:
            if (inGame)
            {
                std::string p = slotStatePathForRom(slot > 0 ? slot : 1);
                if (fileExists(p) && g_emu.loadState(p))
                    g_emu.setLastMessage("State loaded.");
            }
            break;
        case HK_RESET:
            if (inGame) g_emu.reset();
            break;
        case HK_EXIT_GAME:
            if (isFullscreenActive()) toggleFullscreen();
            else if (inGame) closeGame();
            break;
        case HK_QUIT:
            requestQuit();
            break;
        case HK_PAUSE:
            togglePause();
            break;
        case HK_FULLSCREEN:
            toggleFullscreen();
            break;
    }
}

// SDL2 reports individual left/right modifier flags (KMOD_LSHIFT, KMOD_LCTRL...),
// while 'required' uses the combined masks (KMOD_SHIFT, KMOD_CTRL...). Test whether
// each required modifier group has at least one of its bits set in 'pressed'.
static bool hotkeyKeyMatches(Uint16 pressed, Uint16 required)
{
    const Uint16 m = (Uint16)(KMOD_CTRL | KMOD_SHIFT | KMOD_ALT | KMOD_GUI);
    if (required == 0)
        return (pressed & m) == 0;
    if ((required & KMOD_CTRL) && !(pressed & KMOD_CTRL)) return false;
    if ((required & KMOD_SHIFT) && !(pressed & KMOD_SHIFT)) return false;
    if ((required & KMOD_ALT) && !(pressed & KMOD_ALT)) return false;
    if ((required & KMOD_GUI) && !(pressed & KMOD_GUI)) return false;
    return true;
}

// Collapse left/right modifier flags into the combined group masks so a binding
// recorded while pressing Left Ctrl still matches when Right Ctrl is pressed.
static Uint16 normalizeHotkeyMod(Uint16 mod)
{
    Uint16 out = 0;
    if (mod & KMOD_CTRL)  out |= KMOD_CTRL;
    if (mod & KMOD_SHIFT) out |= KMOD_SHIFT;
    if (mod & KMOD_ALT)   out |= KMOD_ALT;
    if (mod & KMOD_GUI)   out |= KMOD_GUI;
    return out;
}

static bool handleKeyboardHotkey(SDL_Scancode sc, Uint16 mod)
{
    for (int h = 0; h < HK_COUNT; ++h)
    {
        if (!g_cfg.hotkeyEnabled[h]) continue;
        for (int k = 0; k < g_cfg.hotkeyKeyCount[h]; ++k)
        {
            const HotkeyKey& hk = g_cfg.hotkeyKeys[h][k];
            if (hk.scancode == sc && hotkeyKeyMatches(mod, hk.mod))
            {
                triggerHotkey(h, hk.slot > 0 ? hk.slot : 1);
                return true;
            }
        }
    }
    return false;
}

static void handleControllerButton(SDL_GameController* gc, SDL_GameControllerButton button)
{
    if (g_rebindHotkey >= 0)
    {
        if (modLikeButton(button) && g_rebindPendingMod == SDL_CONTROLLER_BUTTON_INVALID)
        {
            g_rebindPendingMod = button;
            return;
        }
        SDL_GameControllerButton mod = g_rebindPendingMod;
        if (mod == SDL_CONTROLLER_BUTTON_INVALID)
        {
            for (int m = 0; m < (int)SDL_CONTROLLER_BUTTON_MAX; ++m)
            {
                SDL_GameControllerButton b = (SDL_GameControllerButton)m;
                if (b != button && modLikeButton(b) && SDL_GameControllerGetButton(gc, b))
                {
                    mod = b;
                    break;
                }
            }
        }
        g_cfg.hotkeyMod[g_rebindHotkey] = mod;
        g_cfg.hotkeyButton[g_rebindHotkey] = button;
        g_cfg.hotkeyEnabled[g_rebindHotkey] = true;
        g_rebindHotkey = -1;
        g_rebindPendingMod = SDL_CONTROLLER_BUTTON_INVALID;
        saveConfig();
        return;
    }

    // While rebinding a pad action, do not let hotkeys fire.
    if (g_rebindPadAction >= 0)
        return;

    for (int h = 0; h < HK_COUNT; ++h)
    {
        if (!g_cfg.hotkeyEnabled[h]) continue;
        if (g_cfg.hotkeyButton[h] != button) continue;
        bool modOk;
        if (g_cfg.hotkeyMod[h] == SDL_CONTROLLER_BUTTON_INVALID)
            modOk = true;
        else
            modOk = SDL_GameControllerGetButton(gc, g_cfg.hotkeyMod[h]) != 0;
        if (!modOk) continue;
        triggerHotkey(h);
        return;
    }
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
static void drawMainMenu()
{
    if (isFullscreenActive())
        return;
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu(tr("File", "文件")))
        {
            if (ImGui::MenuItem(tr("Open ROM...", "打开 ROM..."), "Ctrl+O"))
            {
                std::string path = openRomDialog(g_window);
                if (!path.empty()) loadRom(path);
            }
            if (ImGui::BeginMenu(tr("Open Recent", "最近打开")))
            {
                if (g_cfg.recentGames.empty())
                    ImGui::TextDisabled(tr("No recent games", "无最近游戏"));
                for (size_t i = 0; i < g_cfg.recentGames.size(); ++i)
                {
                    std::string name = g_cfg.recentGames[i];
                    size_t slash = name.find_last_of("\\/");
                    if (slash != std::string::npos)
                        name = name.substr(slash + 1);
                    ImGui::PushID((int)i);
                    if (ImGui::MenuItem(name.c_str()))
                        loadRom(g_cfg.recentGames[i]);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", g_cfg.recentGames[i].c_str());
                    ImGui::PopID();
                }
                if (!g_cfg.recentGames.empty())
                {
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("Clear Recent", "清空最近")))
                    {
                        g_cfg.recentGames.clear();
                        saveConfig();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Exit", "退出"), "Alt+F4"))
                {
                    SDL_Event quit{};
                    quit.type = SDL_QUIT;
                    SDL_PushEvent(&quit);
                }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(tr("Emulation", "模拟")))
        {
            if (g_emu.gameLoaded())
            {
                if (ImGui::MenuItem(g_emu.paused() ? tr("Resume", "继续") : tr("Pause", "暂停"), "Space"))
                    togglePause();
                if (ImGui::MenuItem(tr("Reset", "重置"), "F12"))
                    g_emu.reset();
                ImGui::Separator();
                if (ImGui::MenuItem(tr("Save State", "保存状态"), nullptr))
                {
                    if (g_emu.saveState(statePathForRom()))
                        g_emu.setLastMessage("State saved.");
                }
                if (ImGui::MenuItem(tr("Load State", "加载状态"), nullptr))
                {
                    if (fileExists(statePathForRom()) && g_emu.loadState(statePathForRom()))
                        g_emu.setLastMessage("State loaded.");
                }
                ImGui::Separator();
                ImGui::TextDisabled(tr("State slots:", "状态槽："));
                ImGui::SameLine();
                ImGui::TextDisabled(tr("F1-F10 load / Shift+F1-F10 save", "F1-F10 加载 / Shift+F1-F10 保存"));
                ImGui::Separator();
                if (ImGui::MenuItem(tr("Close Game", "关闭游戏"), "Ctrl+W"))
                    closeGame();
            }
            else
            {
                ImGui::TextDisabled(tr("No game loaded", "未加载游戏"));
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Auto-save state on close", "关闭时自动保存状态"), nullptr, &g_cfg.autoSaveState))
                saveConfig();
            if (ImGui::MenuItem(tr("Auto-load state on open", "打开时自动读档"), nullptr, &g_cfg.autoLoadState))
                saveConfig();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(tr("Config", "配置")))
        {
            if (ImGui::MenuItem(tr("Video...", "视频..."))) g_showVideo = true;
            if (ImGui::MenuItem(tr("Input...", "输入..."))) g_showInput = true;
            if (ImGui::BeginMenu(tr("Theme", "主题")))
            {
                int prev = g_cfg.theme;
                if (ImGui::RadioButton(tr("System (auto)", "跟随系统"), g_cfg.theme == 0)) g_cfg.theme = 0;
                if (ImGui::RadioButton(tr("Dark", "暗色"), g_cfg.theme == 1)) g_cfg.theme = 1;
                if (ImGui::RadioButton(tr("Light", "亮色"), g_cfg.theme == 2)) g_cfg.theme = 2;
                if (g_cfg.theme != prev) { applyTheme(true); saveConfig(); }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(tr("Language", "语言")))
            {
                int prev = g_cfg.language;
                if (ImGui::RadioButton("English", g_cfg.language == 0)) g_cfg.language = 0;
                if (ImGui::RadioButton("中文", g_cfg.language == 1)) g_cfg.language = 1;
                if (g_cfg.language != prev) saveConfig();
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(tr("Help", "帮助")))
        {
            if (ImGui::MenuItem(tr("About", "关于"))) g_showAbout = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

static void drawStatusBar()
{
    if (isFullscreenActive())
        return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - 24));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 24));
    ImGui::Begin("##status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    const float barH = 24;
    ImVec2 cur = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(cur.x, vp->WorkPos.y + vp->WorkSize.y - barH + (barH - ImGui::GetTextLineHeight()) * 0.5f));

    if (g_emu.gameLoaded())
    {
        char buf[256];
        if (g_cfg.showFps)
            snprintf(buf, sizeof(buf), "%s  |  %ux%u  |  %.1f fps",
                     g_emu.coreName().c_str(), g_emu.videoWidth(), g_emu.videoHeight(), g_emu.fps());
        else
            snprintf(buf, sizeof(buf), "%s  |  %ux%u",
                     g_emu.coreName().c_str(), g_emu.videoWidth(), g_emu.videoHeight());
        ImGui::TextUnformatted(buf);
    }
    else
        ImGui::TextUnformatted(tr("No game loaded", "未加载游戏"));
    if (g_emu.paused())
        ImGui::SameLine(), ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), tr("[PAUSED]", "[暂停]"));
    ImGui::SameLine();
    ImGui::TextUnformatted(g_emu.lastMessage().c_str());
    ImGui::End();
}

static void drawVideoSettings()
{
    if (!g_showVideo) return;
    ImGui::SetNextWindowSize(ImVec2(420, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(tr("Video", "视频"), &g_showVideo))
    {
        ImGui::Combo(tr("Window scale", "窗口缩放"), &g_cfg.videoScale, comboWindowScale());
        ImGui::Combo(tr("Video filter", "视频滤镜"), &g_cfg.videoFilter, comboVideoFilter());
        ImGui::Combo(tr("Aspect ratio", "宽高比"), &g_cfg.aspectRatio, comboAspectRatio());
        ImGui::Separator();
        if (ImGui::Checkbox(tr("Show side mask", "显示侧边遮罩"), &g_cfg.maskEnabled))
            saveConfig();
        if (g_cfg.maskEnabled)
        {
            ImGui::TextDisabled(tr("Shows a bundled black Mega Drive bezel around the game.",
                                   "用 MD 主机插画填充两侧黑边。"));
        }
        ImGui::Separator();
        ImGui::Checkbox(tr("Bilinear when no filter", "无滤镜时使用双线性"), &g_cfg.filterBilinear);
        ImGui::Checkbox(tr("Show FPS in status bar", "在状态栏显示帧率"), &g_cfg.showFps);
        ImGui::Checkbox(tr("Enable audio", "启用音频"), &g_cfg.audioEnabled);
        int prevTheme = g_cfg.theme;
        ImGui::Combo(tr("Theme", "主题"), &g_cfg.theme, "System\0Dark\0Light\0");
        if (g_cfg.theme != prevTheme)
        {
            applyTheme(true);
            saveConfig();
        }
        ImGui::TextDisabled(tr("Changes apply immediately.", "更改立即生效。"));
        ImGui::End();
    }
}

static void drawKeyboardTab()
{
    ImGui::TextWrapped(tr("Click a binding, then press a key to change it. '+' adds an extra binding, 'x' removes one. The keyboard and every connected gamepad drive the player at the same time.", "点击绑定项后按键盘即可更改。'+' 添加一个按键，'x' 移除按键。键盘和所有已连接的手柄会同时驱动玩家。"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::Combo("##kbplayer", &g_cfg.keyboardPlayer, comboPlayer());
    if (ImGui::Button(tr("Reset All", "全部重置")))
    {
        for (int a = 0; a < AID_COUNT; ++a)
        {
            g_cfg.keyCount[a] = g_defaultKeyCount[a];
            for (int k = 0; k < g_defaultKeyCount[a]; ++k)
                g_cfg.keys[a][k] = g_defaultKeys[a][k];
        }
        saveConfig();
    }
    ImGui::Separator();
    if (ImGui::BeginTable("kbtbl", 3,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerV, ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn(tr("Action", "操作"));
        ImGui::TableSetupColumn(tr("Bindings", "按键"));
        ImGui::TableSetupColumn(tr("Reset", "重置"));
        ImGui::TableHeadersRow();
        for (int a = 0; a < AID_COUNT; ++a)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", actionName(a));
            ImGui::TableSetColumnIndex(1);
            for (int k = 0; k < g_cfg.keyCount[a]; ++k)
            {
                SDL_Scancode sc = g_cfg.keys[a][k];
                const char* name = (sc >= 0 && sc < SDL_NUM_SCANCODES && g_scancodeNames[sc]) ? g_scancodeNames[sc] : "?";
                std::string label = (g_rebindAction == a && g_rebindSlot == k)
                    ? tr("Press key...", "请按键...") : name;
                std::string id = label + "##k" + std::to_string(a) + "_" + std::to_string(k);
                if (ImGui::Button(id.c_str(), ImVec2(120, 0)))
                {
                    g_rebindAction = a;
                    g_rebindSlot = k;
                }
                ImGui::SameLine();
                std::string del = "x##k" + std::to_string(a) + "_" + std::to_string(k);
                if (ImGui::SmallButton(del.c_str()))
                {
                    g_cfg.removeKey(a, k);
                    if (g_rebindAction == a) { g_rebindAction = -1; g_rebindSlot = -1; }
                }
            }
            if (g_cfg.keyCount[a] < MAX_KEYS_PER_ACTION)
            {
                std::string add = "+ " + std::string(tr("Add", "添加")) + "##add" + std::to_string(a);
                if (ImGui::SmallButton(add.c_str()))
                {
                    g_rebindAction = a;
                    g_rebindSlot = -1;
                }
            }
            ImGui::TableSetColumnIndex(2);
            std::string r = std::string(tr("Reset", "重置")) + "##r" + std::to_string(a);
            if (ImGui::Button(r.c_str()))
            {
                g_cfg.keyCount[a] = g_defaultKeyCount[a];
                for (int k = 0; k < g_defaultKeyCount[a]; ++k)
                    g_cfg.keys[a][k] = g_defaultKeys[a][k];
                saveConfig();
            }
        }
        ImGui::EndTable();
    }
}

static void drawGamepadTab()
{
    if (g_pads.empty())
    {
        ImGui::TextDisabled(tr("No gamepad detected. Plug in a USB / Xbox / DirectInput pad - standard pads auto-map. For a custom pad, put a 'gamecontrollerdb.txt' next to MesenMD.exe.", "未检测到手柄。请插入 USB / Xbox / DirectInput 手柄——标准手柄会自动映射。如需自定义手柄，请将 'gamecontrollerdb.txt' 放在 MesenMD.exe 旁边。"));
        return;
    }
    static int sel = 0;
    if (sel < 0) sel = 0;
    if (sel >= (int)g_pads.size()) sel = (int)g_pads.size() - 1;

    if (g_pads.size() > 1)
        if (ImGui::BeginCombo(tr("Device", "设备"), g_pads[sel].name.c_str()))
        {
            for (int i = 0; i < (int)g_pads.size(); ++i)
                if (ImGui::Selectable(g_pads[i].name.c_str(), i == sel))
                    sel = i;
            ImGui::EndCombo();
        }
    Pad& pad = g_pads[sel];

    ImGui::Text("%s", pad.name.c_str());
    ImGui::SameLine();
    int p = pad.player;
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo(tr("Player", "玩家"), &p, comboPlayer()))
    {
        pad.player = p;
        saveConfig();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Reset Pad", "重置手柄")))
    {
        resetPadMappingToDefault(pad);
        g_cfg.padMaps[pad.guid] = pad.map;
        saveConfig();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(tr("Click a binding, then press a pad button / stick / D-pad.", "点击绑定项后按手柄按钮 / 摇杆 / 方向键。"));
    ImGui::Separator();

    int live = firstActivePadInput(pad);
    if (live >= 0)
        ImGui::Text(tr("Active input: %s", "当前输入: %s"), padInputName(pad, live).c_str());
    else
        ImGui::TextDisabled(tr("Active input: (none) - press a button, stick or D-pad", "当前输入: (无) - 按按钮 / 摇杆 / 方向键"));
    ImGui::Separator();

    if (ImGui::CollapsingHeader(tr("Input diagnostics", "输入检测")))
    {
        bool any = false;
        for (int b = 0; b < pad.buttonCount; ++b)
            if (padInputActive(pad, padInput(PAD_BUTTON, b)))
            { any = true; ImGui::Text("%s %d", tr("Button", "按钮"), b); }
        for (int ax = 0; ax < pad.axisCount; ++ax)
        {
            int v = pad.gc
                ? (ax < (int)SDL_CONTROLLER_AXIS_MAX ? SDL_GameControllerGetAxis(pad.gc, (SDL_GameControllerAxis)ax) : 0)
                : SDL_JoystickGetAxis(pad.joy, ax);
            if (v < -20000 || v > 20000)
            { any = true; ImGui::Text("%s %d (%d)", tr("Axis", "轴"), ax, v); }
        }
        for (int h = 0; h < pad.hatCount; ++h)
        {
            Uint8 hat = SDL_JoystickGetHat(pad.joy, h);
            if (hat)
            { any = true; ImGui::Text("%s %d = %02x", tr("Hat", "方向键"), h, (unsigned)hat); }
        }
        if (!any)
            ImGui::TextDisabled(tr("Press any control to identify it.", "按下任意按键以识别。"));
    }
    ImGui::Separator();

    if (ImGui::BeginTable("padtbl", 3,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerV, ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn(tr("Action", "操作"));
        ImGui::TableSetupColumn(tr("Bindings", "按键"));
        ImGui::TableSetupColumn(tr("Clear", "清除"));
        ImGui::TableHeadersRow();
        for (int a = 0; a < AID_COUNT; ++a)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", actionName(a));
            ImGui::TableSetColumnIndex(1);
            bool removed = false;
            for (int k = 0; k < pad.map.mapCount[a]; ++k)
            {
                std::string name;
                if (g_rebindPadAction == a && g_rebindPad == sel && g_rebindPadSlot == k)
                {
                    int act = firstActivePadInput(pad);
                    name = (act >= 0) ? padInputName(pad, act) : tr("Press btn...", "请按按钮...");
                }
                else
                {
                    name = padInputName(pad, pad.map.map[a][k]);
                }
                std::string id = name + "##pd" + std::to_string(a) + "_" + std::to_string(k);
                if (ImGui::Button(id.c_str(), ImVec2(110, 0)))
                {
                    g_rebindPad = sel;
                    g_rebindPadAction = a;
                    g_rebindPadSlot = k;
                    g_padRebindReady = false;
                }
                ImGui::SameLine();
                std::string del = "x##pd" + std::to_string(a) + "_" + std::to_string(k);
                if (ImGui::SmallButton(del.c_str()))
                {
                    for (int m = k; m + 1 < pad.map.mapCount[a]; ++m)
                        pad.map.map[a][m] = pad.map.map[a][m + 1];
                    pad.map.mapCount[a]--;
                    if (g_rebindPadAction == a && g_rebindPadSlot == k)
                    { g_rebindPad = -1; g_rebindPadAction = -1; g_rebindPadSlot = -1; g_padRebindReady = false; }
                    g_cfg.padMaps[pad.guid] = pad.map;
                    saveConfig();
                    removed = true;
                    break;
                }
            }
            if (!removed && pad.map.mapCount[a] < MAX_PAD_BINDINGS_PER_ACTION)
            {
                std::string add = "+ " + std::string(tr("Add", "添加")) + "##padd" + std::to_string(a);
                if (ImGui::SmallButton(add.c_str()))
                {
                    g_rebindPad = sel;
                    g_rebindPadAction = a;
                    g_rebindPadSlot = -1;
                    g_padRebindReady = false;
                }
            }
            ImGui::TableSetColumnIndex(2);
            std::string clr = std::string(tr("Clear", "清除")) + "##pclr" + std::to_string(a);
            if (ImGui::Button(clr.c_str()))
            {
                pad.map.mapCount[a] = 0;
                g_cfg.padMaps[pad.guid] = pad.map;
                saveConfig();
            }
        }
        ImGui::EndTable();
    }
}

static void drawGamepadHotkeysTab()
{
    ImGui::TextWrapped(tr("Hold modifier + press button. Esc cancels rebinding.", "按住修饰键再按按钮。Esc 取消重绑。"));
    if (ImGui::Button(tr("Reset Hotkeys", "重置快捷键")))
    {
        for (int h = 0; h < HK_COUNT; ++h)
        {
            g_cfg.hotkeyEnabled[h] = true;
            g_cfg.hotkeyMod[h] = g_defaultHotkeyMod[h];
            g_cfg.hotkeyButton[h] = g_defaultHotkeyButton[h];
        }
        saveConfig();
    }
    ImGui::Separator();
    if (ImGui::BeginTable("hktbl", 4,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerV, ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn(tr("Action", "操作"));
        ImGui::TableSetupColumn(tr("Modifier", "修饰键"));
        ImGui::TableSetupColumn(tr("Button", "按钮"));
        ImGui::TableSetupColumn(tr("Rebind", "重新绑定"));
        ImGui::TableHeadersRow();
        for (int h = 0; h < HK_COUNT; ++h)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox(("##en" + std::to_string(h)).c_str(), &g_cfg.hotkeyEnabled[h]);
            ImGui::SameLine();
            ImGui::Text("%s", hotkeyName(h));
            ImGui::TableSetColumnIndex(1);
            const char* mname = (g_cfg.hotkeyMod[h] == SDL_CONTROLLER_BUTTON_INVALID)
                ? tr("None", "无") : buttonName(g_cfg.hotkeyMod[h]);
            ImGui::Text("%s", mname);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", buttonName(g_cfg.hotkeyButton[h]));
            ImGui::TableSetColumnIndex(3);
            std::string label = (g_rebindHotkey == h)
                ? tr("Press combo...", "请按组合键...") : tr("Rebind", "重新绑定");
            if (ImGui::Button((label + "##hk" + std::to_string(h)).c_str(), ImVec2(120, 0)))
            {
                g_rebindHotkey = h;
                g_rebindPendingMod = SDL_CONTROLLER_BUTTON_INVALID;
            }
        }
        ImGui::EndTable();
    }
}

static std::string hotkeyKeyName(const HotkeyKey& hk)
{
    std::string s;
    if (hk.mod & KMOD_CTRL)  s += "Ctrl+";
    if (hk.mod & KMOD_ALT)   s += "Alt+";
    if (hk.mod & KMOD_SHIFT) s += "Shift+";
    if (hk.mod & KMOD_GUI)   s += "Win+";
    const char* n = (hk.scancode >= 0 && hk.scancode < SDL_NUM_SCANCODES && g_scancodeNames[hk.scancode])
        ? g_scancodeNames[hk.scancode] : "?";
    return s + n;
}

static void drawKeyboardHotkeysTab()
{
    ImGui::TextWrapped(tr("Click a binding, then press a key combination (e.g. Ctrl+C, Shift+F3). '+' adds another binding, 'x' removes one. Esc cancels.", "点击绑定项后按下按键组合（如 Ctrl+C、Shift+F3）。'+' 添加一个组合，'x' 移除。Esc 取消。"));
    if (ImGui::Button(tr("Reset All", "全部重置")))
    {
        for (int h = 0; h < HK_COUNT; ++h)
        {
            g_cfg.hotkeyEnabled[h] = true;
            g_cfg.hotkeyKeyCount[h] = g_defaultHotkeyKeyCount[h];
            for (int k = 0; k < g_defaultHotkeyKeyCount[h]; ++k)
                g_cfg.hotkeyKeys[h][k] = g_defaultHotkeyKeys[h][k];
        }
        saveConfig();
    }
    ImGui::Separator();
    if (ImGui::BeginTable("hkktbl", 3,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerV, ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn(tr("Action", "操作"));
        ImGui::TableSetupColumn(tr("Bindings", "按键"));
        ImGui::TableSetupColumn(tr("Reset", "重置"));
        ImGui::TableHeadersRow();
        for (int h = 0; h < HK_COUNT; ++h)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox(("##hken" + std::to_string(h)).c_str(), &g_cfg.hotkeyEnabled[h]);
            ImGui::SameLine();
            ImGui::Text("%s", hotkeyName(h));
            ImGui::TableSetColumnIndex(1);
            bool removed = false;
            for (int k = 0; k < g_cfg.hotkeyKeyCount[h]; ++k)
            {
                std::string name = (g_rebindHotkeyKey == h && g_rebindHotkeyKeySlot == k)
                    ? tr("Press key...", "请按键...") : hotkeyKeyName(g_cfg.hotkeyKeys[h][k]);
                std::string id = name + "##hkk" + std::to_string(h) + "_" + std::to_string(k);
                if (ImGui::Button(id.c_str(), ImVec2(150, 0)))
                {
                    g_rebindHotkeyKey = h;
                    g_rebindHotkeyKeySlot = k;
                }
                ImGui::SameLine();
                std::string del = "x##hkk" + std::to_string(h) + "_" + std::to_string(k);
                if (ImGui::SmallButton(del.c_str()))
                {
                    g_cfg.removeHotkeyKey(h, k);
                    if (g_rebindHotkeyKey == h && g_rebindHotkeyKeySlot == k)
                    { g_rebindHotkeyKey = -1; g_rebindHotkeyKeySlot = -1; }
                    saveConfig();
                    removed = true;
                    break;
                }
            }
            if (!removed && g_cfg.hotkeyKeyCount[h] < MAX_HOTKEY_KEYS_PER_ACTION)
            {
                std::string add = "+ " + std::string(tr("Add", "添加")) + "##hkadd" + std::to_string(h);
                if (ImGui::SmallButton(add.c_str()))
                {
                    g_rebindHotkeyKey = h;
                    g_rebindHotkeyKeySlot = -1;
                }
            }
            ImGui::TableSetColumnIndex(2);
            std::string r = std::string(tr("Reset", "重置")) + "##hkres" + std::to_string(h);
            if (ImGui::Button(r.c_str()))
            {
                g_cfg.hotkeyKeyCount[h] = g_defaultHotkeyKeyCount[h];
                for (int k = 0; k < g_defaultHotkeyKeyCount[h]; ++k)
                    g_cfg.hotkeyKeys[h][k] = g_defaultHotkeyKeys[h][k];
                saveConfig();
            }
        }
        ImGui::EndTable();
    }
}

static void drawHotkeysTab()
{
    if (ImGui::BeginTabBar("hotkeytabs"))
    {
        if (ImGui::BeginTabItem(tr("Gamepad", "手柄")))
        {
            drawGamepadHotkeysTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Keyboard", "键盘")))
        {
            drawKeyboardHotkeysTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

static void drawInputSettings()
{
    if (!g_showInput) return;
    ImGui::SetNextWindowSize(ImVec2(880, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(tr("Input", "输入"), &g_showInput))
    {
        if (ImGui::BeginTabBar("inputtabs"))
        {
            if (ImGui::BeginTabItem(tr("Keyboard", "键盘")))
            {
                drawKeyboardTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr("Gamepads", "手柄")))
            {
                drawGamepadTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr("Hotkeys", "快捷键")))
            {
                drawHotkeysTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
}

static void drawAbout()
{
    if (!g_showAbout) return;
    ImGui::SetNextWindowSize(ImVec2(480, 280), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(tr("About", "关于"), &g_showAbout))
    {
        ImGui::TextUnformatted("MesenMD");
        ImGui::SameLine();
        ImGui::TextDisabled(MESENMD_VERSION);
        ImGui::Separator();

        ImGui::TextWrapped(tr("A Mesen2-inspired Sega Mega Drive / Genesis emulator.",
                              "一款灵感来自 Mesen2 的世嘉 Mega Drive / Genesis 模拟器。"));
        ImGui::TextWrapped(tr("Core: Genesis Plus GX (libretro).  UI: SDL2 + OpenGL + Dear ImGui.",
                              "核心：Genesis Plus GX (libretro)。界面：SDL2 + OpenGL + Dear ImGui。"));
        ImGui::Separator();

        ImGui::TextUnformatted(tr("Author", "作者"));
        ImGui::SameLine();
        ImGui::TextDisabled(MESENMD_AUTHOR);
        ImGui::TextUnformatted(tr("Repository", "项目仓库"));
        ImGui::SameLine();
        ImGui::TextDisabled(MESENMD_REPO_URL);

        if (ImGui::Button(tr("Open GitHub", "打开 GitHub")))
            ShellExecuteA(nullptr, "open", MESENMD_REPO_URL, nullptr, nullptr, SW_SHOWNORMAL);

        ImGui::Separator();
        ImGui::TextWrapped(tr("GPLv3 license. Intended for legally owned ROMs only.",
                              "GPLv3 许可。仅用于合法拥有的 ROM。"));
        ImGui::End();
    }
}

// ---------------------------------------------------------------------------
// Home page / play-history thumbnails
// ---------------------------------------------------------------------------
static std::string appDir()
{
    char exe[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = exe;
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos)
        dir = dir.substr(0, pos);
    return dir;
}

static std::string thumbPathForPath(const std::string& rom)
{
    if (rom.empty()) return "";
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : rom)
    {
        h ^= (uint64_t)c;
        h *= 1099511628211ull;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "\\thumbnails\\%016llx.png", (unsigned long long)h);
    return appDir() + buf;
}

static std::string thumbPathForRom()
{
    return thumbPathForPath(g_emu.romPath());
}

static void saveThumbnail()
{
    if (g_rgba.empty() || g_texW <= 0 || g_texH <= 0)
        return;
    std::string p = thumbPathForRom();
    if (p.empty()) return;
    size_t pos = p.find_last_of("\\/");
    std::string dir = p.substr(0, pos);
    if (!dir.empty())
        CreateDirectoryA(dir.c_str(), nullptr);
    stbi_write_png(p.c_str(), g_texW, g_texH, 4, g_rgba.data(), g_texW * 4);
}

static std::map<std::string, GLuint> g_thumbCache;

static GLuint getThumbTexture(const std::string& path)
{
    if (path.empty()) return 0;
    auto it = g_thumbCache.find(path);
    if (it != g_thumbCache.end())
        return it->second;
    GLuint tex = 0;
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (data)
    {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
        g_thumbCache.emplace(path, tex);
    }
    return tex;
}

static void deleteRecentSnapshot(const std::string& path)
{
    auto it = std::find(g_cfg.recentGames.begin(), g_cfg.recentGames.end(), path);
    if (it != g_cfg.recentGames.end())
        g_cfg.recentGames.erase(it);

    std::string tpath = thumbPathForPath(path);
    std::wstring wthumb = utf8ToWide(tpath);
    if (!wthumb.empty())
        DeleteFileW(wthumb.c_str());
    auto tc = g_thumbCache.find(tpath);
    if (tc != g_thumbCache.end())
    {
        if (tc->second)
            glDeleteTextures(1, &tc->second);
        g_thumbCache.erase(tc);
    }

    std::wstring wstate = utf8ToWide(path + ".state");
    if (!wstate.empty())
        DeleteFileW(wstate.c_str());
    for (int slot = 1; slot <= 10; ++slot)
    {
        std::wstring wslot = utf8ToWide(path + "." + std::to_string(slot) + ".state");
        if (!wslot.empty())
            DeleteFileW(wslot.c_str());
    }

    saveConfig();
}

static bool applyHomeStep(int dir, int axis, int cols, int count)
{
    int idx = g_homeIndex;
    if (axis == 2)
    {
        int target = idx + dir * cols;
        if (target >= 0 && target < count)
            idx = target;
    }
    else
    {
        int target = idx + dir;
        if (target < 0) target = count - 1;
        if (target >= count) target = 0;
        idx = target;
    }
    if (idx != g_homeIndex)
    {
        g_homeIndex = idx;
        return true;
    }
    return false;
}

static bool homeNavStep(bool left, bool right, bool up, bool down, int cols, int count)
{
    if (count <= 0 || cols <= 0) return false;
    if (!(left || right || up || down))
    {
        g_homeNavHolding = false;
        g_homeNavHoldDir = 0;
        return false;
    }
    int axis = 0, dir = 0;
    if (left || right) { axis = 1; dir = right ? 1 : -1; }
    else { axis = 2; dir = down ? 1 : -1; }
    int holdDir = (axis == 1) ? (dir * 1000) : (dir * 1000000);
    Uint64 now = SDL_GetTicks64();
    if (g_homeNavHolding && g_homeNavHoldDir == holdDir)
    {
        if (now >= g_homeNavRepeatMs)
        {
            g_homeNavRepeatMs = now + 90;
            return applyHomeStep(dir, axis, cols, count);
        }
        return false;
    }
    g_homeNavHolding = true;
    g_homeNavHoldDir = holdDir;
    g_homeNavRepeatMs = now + 400;
    return applyHomeStep(dir, axis, cols, count);
}

static bool mdActionActive(int action)
{
    if (action < 0 || action >= AID_COUNT) return false;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    for (int k = 0; k < g_cfg.keyCount[action]; ++k)
        if (keys[g_cfg.keys[action][k]])
            return true;
    for (const Pad& pad : g_pads)
        for (int k = 0; k < pad.map.mapCount[action]; ++k)
            if (padInputActive(pad, pad.map.map[action][k]))
                return true;
    return false;
}

static void drawHomePage()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wPos = ImGui::GetWindowPos();
    ImVec2 center = ImVec2(wPos.x + avail.x * 0.5f, wPos.y + avail.y * 0.5f);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fpX = style.FramePadding.x;
    const float padX = style.WindowPadding.x;

    // Faint background watermark + product tagline (drawn behind the cards).
    ImFont* font = ImGui::GetFont();
    const char* wm = tr("Recent Games", "最近游戏");
    float wmSize = 72.0f;
    float wmW = font ? font->CalcTextSizeA(wmSize, FLT_MAX, 0.0f, wm).x : 0.0f;
    ImU32 wmCol = (g_lastAppliedDark == 1) ? IM_COL32(255,255,255,20) : IM_COL32(0,0,0,18);
    if (dl && font)
        dl->AddText(font, wmSize, ImVec2(center.x - wmW * 0.5f, center.y - wmSize), wmCol, wm);

    const float topReserve = 30.0f;
    const char* tagline = tr("A Mesen2-style emulator for Sega Mega Drive / Genesis",
                             "仿 Mesen2 的世嘉 Mega Drive / Genesis 模拟器");
    float tagSize = 16.0f;
    float tagW = font ? font->CalcTextSizeA(tagSize, FLT_MAX, 0.0f, tagline).x : 0.0f;
    ImU32 tagCol = (g_lastAppliedDark == 1) ? IM_COL32(255,255,255,110) : IM_COL32(0,0,0,110);
    if (dl && font)
        dl->AddText(font, tagSize, ImVec2(center.x - tagW * 0.5f, wPos.y + 8.0f), tagCol, tagline);

    if (g_cfg.recentGames.empty())
    {
        ImGui::TextDisabled(tr("No recent games.\nOpen a ROM to begin.",
            "无最近游戏。\n请打开一个 ROM 开始。"));
        return;
    }

    const float gap = 14.0f;
    const float aspect = 0.652f;
    const float headerH = 22.0f;
    const float nameH = 40.0f;
    const float minCardW = 100.0f;
    int count = (int)g_cfg.recentGames.size();

    float gridAvailX = avail.x;
    float gridAvailY = avail.y - topReserve;
    if (gridAvailY < 40.0f) gridAvailY = 40.0f;

    int cols = 1;
    float cardW = 0.0f;
    for (int c = 1; c <= count; ++c)
    {
        float w = (gridAvailX - (c - 1) * gap) / c;
        if (w < minCardW) continue;
        int r = (count + c - 1) / c;
        float cardH = headerH + w * aspect + nameH;
        float gridH = r * cardH + (r - 1) * gap;
        if (gridH <= gridAvailY + 1.0f && w > cardW)
        {
            cols = c;
            cardW = w;
        }
    }
    if (cardW <= 0.0f)
    {
        cols = 1;
        cardW = gridAvailX - 2.0f * gap;
        float cardH = headerH + cardW * aspect + nameH;
        if (cardH > gridAvailY)
            cardW *= (gridAvailY / cardH);
        if (cardW < minCardW) cardW = minCardW;
    }

    float imgH = cardW * aspect;
    float cardH = headerH + imgH + nameH;
    int rows = (count + cols - 1) / cols;
    float contentW = cols * cardW + (cols - 1) * gap;
    float gridH = rows * cardH + (rows - 1) * gap;
    float startX = (gridAvailX - contentW) * 0.5f;
    float startY = topReserve + (gridAvailY - gridH) * 0.5f;
    if (startX < 0.0f) startX = 0.0f;
    if (startY < 0.0f) startY = 0.0f;

    if (g_homeIndex >= count) g_homeIndex = count - 1;
    if (g_homeIndex < 0) g_homeIndex = 0;

    bool overlayOpen = g_showVideo || g_showInput || g_showAbout;
    std::string clicked;
    std::string deleted;
    bool navMoved = false;

    // Edge-detect the MD "A" (confirm) / "B" (delete) presses even while an
    // overlay is open, so a held button cannot trigger a surprise launch.
    bool confirmHeld = mdActionActive(AID_A);
    bool deleteHeld  = mdActionActive(AID_B);
    bool confirmPressed = confirmHeld && !g_homePrevConfirm;
    bool deletePressed  = deleteHeld  && !g_homePrevDelete;
    g_homePrevConfirm = confirmHeld;
    g_homePrevDelete  = deleteHeld;

    if (!overlayOpen)
    {
        bool left  = mdActionActive(AID_LEFT);
        bool right = mdActionActive(AID_RIGHT);
        bool up    = mdActionActive(AID_UP);
        bool down  = mdActionActive(AID_DOWN);
        navMoved = homeNavStep(left, right, up, down, cols, count);

        if (confirmPressed)
            clicked = g_cfg.recentGames[g_homeIndex];
        if (deletePressed)
            deleted = g_cfg.recentGames[g_homeIndex];
    }

    ImGui::BeginChild("##homeScroll", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);

    float imgW = cardW - 2.0f * padX - 2.0f * fpX;
    if (imgW <= 0.0f) imgW = cardW - 2.0f * fpX;
    float imgX = (cardW - imgW) * 0.5f - fpX;
    ImVec2 imgSize(imgW, imgH);

    int col = 0;
    float rowY = startY;
    for (size_t i = 0; i < g_cfg.recentGames.size(); ++i)
    {
        const std::string& path = g_cfg.recentGames[i];
        bool selected = (int)i == g_homeIndex;
        if (col == 0)
            ImGui::SetCursorPos(ImVec2(startX, rowY));
        else
            ImGui::SameLine(0, gap);
        col++;

        std::string name = path;
        size_t pos = name.find_last_of("\\/");
        if (pos != std::string::npos) name = name.substr(pos + 1);

        if (selected && navMoved)
            ImGui::SetScrollHereY(0.5f);
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.62f, 0.15f, 1.0f));
        ImGui::BeginChild(("##card" + std::to_string(i)).c_str(), ImVec2(cardW, cardH), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::SetCursorPos(ImVec2(cardW - 30, 2));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.2f, 0.2f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Text, (g_lastAppliedDark == 1) ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
        if (ImGui::Button(("x##del" + std::to_string(i)).c_str(), ImVec2(26, 18)))
            deleted = path;
        ImGui::PopStyleColor(3);

        GLuint tex = getThumbTexture(thumbPathForPath(path));
        ImGui::SetCursorPos(ImVec2(imgX, headerH));
        if (tex)
        {
            if (ImGui::ImageButton(("##thumb" + std::to_string(i)).c_str(),
                                   (ImTextureID)(intptr_t)tex, imgSize))
                clicked = path;
        }
        else
        {
            ImGui::SetCursorPos(ImVec2(imgX + fpX, headerH));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button(tr("No snapshot", "无快照"), imgSize))
                clicked = path;
            ImGui::PopStyleColor(3);
        }

        ImGui::SetCursorPos(ImVec2(4, headerH + imgH + 10));
        ImGui::TextWrapped("%s", name.c_str());
        ImGui::EndChild();
        if (selected)
            ImGui::PopStyleColor(1);

        if (col >= cols)
        {
            col = 0;
            rowY += cardH + gap;
        }
    }
    ImGui::EndChild();

    if (!overlayOpen)
    {
        ImGui::TextDisabled(tr("D-Pad / Arrows: Select    A: Play    B: Delete",
            "方向键/D-Pad：选择    A：开始    B：删除"));
    }
    if (!deleted.empty())
        deleteRecentSnapshot(deleted);
    if (!clicked.empty())
        loadRom(clicked, true);
}


struct MaskColors
{
    ImU32 body, top, slot, accent, text, glow, panel;
};

static MaskColors maskColors(int type)
{
    bool dark = (g_lastAppliedDark == 1);
    ImU32 body  = dark ? IM_COL32(120, 125, 135, 82) : IM_COL32(55, 60, 70, 78);
    ImU32 top   = dark ? IM_COL32(190, 195, 205, 105) : IM_COL32(100, 105, 115, 92);
    ImU32 slot  = dark ? IM_COL32(12, 12, 16, 135) : IM_COL32(12, 12, 16, 115);
    ImU32 text  = dark ? IM_COL32(255, 255, 255, 100) : IM_COL32(0, 0, 0, 100);
    ImU32 glow  = dark ? IM_COL32(255, 255, 255, 7) : IM_COL32(0, 0, 0, 6);
    ImU32 panel = dark ? IM_COL32(255, 255, 255, 7) : IM_COL32(0, 0, 0, 6);
    ImU32 accent;
    switch (type)
    {
        case 1:  accent = IM_COL32(70, 130, 220, 125); break;
        case 2:  accent = IM_COL32(150, 90, 220, 125); break;
        default: accent = IM_COL32(210, 70, 70, 125); break;
    }
    return { body, top, slot, accent, text, glow, panel };
}

static void drawMdConsole(ImDrawList* dl, ImVec2 center, float w, int type)
{
    MaskColors c = maskColors(type);
    float h = w * 0.42f;
    ImVec2 p0(center.x - w * 0.5f, center.y - h * 0.5f);
    ImVec2 p1(center.x + w * 0.5f, center.y + h * 0.5f);
    float rounding = h * 0.14f;

    dl->AddCircleFilled(center, w * 0.62f, c.glow, 48);
    dl->AddRectFilled(p0, p1, c.body, rounding);

    ImVec2 t0(p0.x + w * 0.04f, p0.y + h * 0.05f);
    ImVec2 t1(p1.x - w * 0.04f, p0.y + h * 0.24f);
    dl->AddRectFilled(t0, t1, c.top, rounding * 0.7f);

    float slotW = w * 0.34f;
    float slotH = h * 0.16f;
    ImVec2 s0(center.x - slotW * 0.5f, p0.y + h * 0.08f);
    ImVec2 s1(center.x + slotW * 0.5f, s0.y + slotH);
    dl->AddRectFilled(s0, s1, c.slot, 3.0f);

    ImVec2 c0(center.x - slotW * 0.42f, p0.y - h * 0.16f);
    ImVec2 c1(center.x + slotW * 0.42f, p0.y + h * 0.05f);
    dl->AddRectFilled(c0, c1, c.accent, 2.0f);

    ImVec2 a0(p0.x + w * 0.06f, p1.y - h * 0.22f);
    ImVec2 a1(p1.x - w * 0.06f, p1.y - h * 0.14f);
    dl->AddRectFilled(a0, a1, c.accent, 2.0f);

    float r = h * 0.09f;
    ImVec2 pc(p1.x - w * 0.10f, p0.y + h * 0.55f);
    dl->AddCircle(pc, r, c.top, 16, 2.0f);

    ImFont* font = ImGui::GetFont();
    if (font)
    {
        const char* label = (type == 2) ? "GENESIS" : (type == 1 ? "MD 2" : "16-BIT");
        float fs = h * 0.16f;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
        dl->AddText(font, fs, ImVec2(center.x - ts.x * 0.5f, p1.y - h * 0.05f - ts.y), c.text, label);
    }
}

static void drawMdController(ImDrawList* dl, ImVec2 center, float w, int type)
{
    MaskColors c = maskColors(type);
    float h = w * 0.62f;
    ImVec2 p0(center.x - w * 0.5f, center.y - h * 0.5f);
    ImVec2 p1(center.x + w * 0.5f, center.y + h * 0.5f);
    float rounding = h * 0.24f;

    dl->AddCircleFilled(center, w * 0.58f, c.glow, 48);
    dl->AddRectFilled(p0, p1, c.body, rounding);

    ImVec2 t0(p0.x + w * 0.06f, p0.y + h * 0.10f);
    ImVec2 t1(p1.x - w * 0.06f, p0.y + h * 0.22f);
    dl->AddRectFilled(t0, t1, c.top, rounding * 0.5f);

    float dp = w * 0.24f;
    ImVec2 dc(center.x - w * 0.24f, center.y + h * 0.06f);
    dl->AddRectFilled(ImVec2(dc.x - dp * 0.5f, dc.y - dp * 0.17f),
                      ImVec2(dc.x + dp * 0.5f, dc.y + dp * 0.17f), c.top, 2.0f);
    dl->AddRectFilled(ImVec2(dc.x - dp * 0.17f, dc.y - dp * 0.5f),
                      ImVec2(dc.x + dp * 0.17f, dc.y + dp * 0.5f), c.top, 2.0f);

    float br = w * 0.065f;
    ImVec2 bc(center.x + w * 0.20f, center.y + h * 0.04f);
    dl->AddCircleFilled(ImVec2(bc.x - br * 1.5f, bc.y + br * 1.4f), br, c.accent, 16);
    dl->AddCircleFilled(ImVec2(bc.x, bc.y), br, c.accent, 16);
    dl->AddCircleFilled(ImVec2(bc.x + br * 1.5f, bc.y - br * 1.4f), br, c.accent, 16);

    dl->AddCircleFilled(ImVec2(center.x, center.y - h * 0.30f), br * 0.7f, c.top, 12);

    dl->AddBezierCubic(ImVec2(center.x - w * 0.10f, p0.y),
                       ImVec2(center.x - w * 0.30f, p0.y - h * 0.55f),
                       ImVec2(center.x - w * 0.05f, p0.y - h * 0.85f),
                       ImVec2(center.x + w * 0.10f, p0.y - h * 1.10f),
                       c.top, 2.0f);
}

static GLuint getMaskTexture()
{
    if (!g_maskTex)
    {
        std::string p = assetPath("mask_md.png");
        int w = 0, h = 0, ch = 0;
        unsigned char* data = stbi_load(p.c_str(), &w, &h, &ch, 4);
        if (data)
        {
            glGenTextures(1, &g_maskTex);
            glBindTexture(GL_TEXTURE_2D, g_maskTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
    }
    return g_maskTex;
}

static void drawSideMask(const ImVec2& winPos, const ImVec2& winSize,
                         const ImVec2& gamePos, const ImVec2& gameSize)
{
    float leftW = gamePos.x - winPos.x;
    float rightW = (winPos.x + winSize.x) - (gamePos.x + gameSize.x);
    if (leftW < 70.0f && rightW < 70.0f)
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!dl)
        return;

    // Ready-made black MD console overlay (RetroArch bezel): draw it as a
    // full-window backdrop and let the game image sit inside its play area.
    if (getMaskTexture())
    {
        ImU32 bg = (g_lastAppliedDark == 1) ? IM_COL32(6, 8, 10, 255) : IM_COL32(14, 14, 16, 255);
        dl->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y), bg, 0.0f);

        dl->AddImage((ImTextureID)(intptr_t)g_maskTex, winPos,
                     ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
        return;
    }

    MaskColors c = maskColors(g_cfg.maskType);
    float top = winPos.y;
    float bottom = winPos.y + winSize.y;
    float centerY = top + (bottom - top) * 0.5f;

    if (leftW >= 70.0f)
    {
        ImVec2 b0(winPos.x + 6.0f, top + 6.0f);
        ImVec2 b1(gamePos.x - 6.0f, bottom - 6.0f);
        if (b1.x > b0.x && b1.y > b0.y)
            dl->AddRectFilled(b0, b1, c.panel, 12.0f);
        float cw = std::min(leftW * 0.78f, 300.0f);
        drawMdConsole(dl, ImVec2(winPos.x + leftW * 0.5f, centerY), cw, g_cfg.maskType);
    }

    if (rightW >= 70.0f)
    {
        ImVec2 b0(gamePos.x + gameSize.x + 6.0f, top + 6.0f);
        ImVec2 b1(winPos.x + winSize.x - 6.0f, bottom - 6.0f);
        if (b1.x > b0.x && b1.y > b0.y)
            dl->AddRectFilled(b0, b1, c.panel, 12.0f);
        float cw = std::min(rightW * 0.70f, 220.0f);
        drawMdController(dl, ImVec2(gamePos.x + gameSize.x + rightW * 0.5f, centerY), cw, g_cfg.maskType);
    }
}

static void drawViewport()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float yTop = vp->WorkPos.y + (isFullscreenActive() ? 0.0f : ImGui::GetFrameHeight());
    float yBottom = vp->WorkPos.y + vp->WorkSize.y - (isFullscreenActive() ? 0.0f : 24.0f);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, yTop));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, yBottom - yTop));
    ImGui::Begin("Game", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav);

    ImVec2 avail = ImGui::GetContentRegionAvail();

    if (!g_emu.gameLoaded())
    {
        drawHomePage();
        ImGui::End();
        return;
    }
    if (g_tex == 0)
    {
        ImGui::TextDisabled(tr("Loading...", "加载中..."));
        ImGui::End();
        return;
    }

    int baseW = g_emu.videoWidth();
    int baseH = g_emu.videoHeight();
    if (baseW <= 0 || baseH <= 0) baseW = 320, baseH = 224;

    float aspect = (float)baseW / (float)baseH;
    if (g_cfg.aspectRatio == 0)
    {
        float ga = g_emu.avInfo().geometry.aspect_ratio;
        if (ga > 0.0f) aspect = ga;
    }
    else if (g_cfg.aspectRatio == 1)
    {
        aspect = 4.0f / 3.0f;
    }
    float destW = avail.x;
    float destH = destW / aspect;
    if (destH > avail.y) { destH = avail.y; destW = destH * aspect; }

    if (g_cfg.videoScale >= 2)
    {
        float scale = (float)(g_cfg.videoScale - 1);
        destW = baseW * scale; destH = baseH * scale;
        if (destW > avail.x) destW = avail.x;
        if (destH > avail.y) destH = avail.y;
    }

    ImVec2 imgSize(destW, destH);
    ImVec2 availStart = ImGui::GetCursorScreenPos();
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec2 center(winPos.x + winSize.x * 0.5f, winPos.y + winSize.y * 0.5f);
    ImVec2 drawPos(center.x - destW * 0.5f, center.y - destH * 0.5f);

    GLuint displayTex = g_tex;
    if (g_cfg.videoFilter >= 2)
        displayTex = videoFilterRender(g_tex, baseW, baseH, (int)destW, (int)destH, g_cfg.videoFilter);
    if (g_cfg.maskEnabled)
        drawSideMask(winPos, winSize, drawPos, imgSize);
    ImGui::SetCursorScreenPos(drawPos);
    ImGui::Image((ImTextureID)(intptr_t)displayTex, imgSize);
    (void)availStart;

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

static void handleEvent(const SDL_Event& e)
{
    ImGui_ImplSDL2_ProcessEvent(&e);
    if (e.type == SDL_QUIT)
    {
        requestQuit();
    }
    else if (e.type == SDL_DROPFILE)
    {
        if (e.drop.file)
        {
            loadRom(e.drop.file);
            SDL_free(e.drop.file);
        }
    }
    else if (e.type == SDL_WINDOWEVENT)
    {
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
            SDL_StopTextInput();
        else if (e.window.event == SDL_WINDOWEVENT_MAXIMIZED)
            g_cfg.windowMaximized = true;
        else if (e.window.event == SDL_WINDOWEVENT_RESTORED)
            g_cfg.windowMaximized = false;
        else if (e.window.event == SDL_WINDOWEVENT_MOVED && !isFullscreenActive() && !g_cfg.windowMaximized)
        {
            SDL_GetWindowPosition(g_window, &g_cfg.windowX, &g_cfg.windowY);
            g_cfg.windowSaved = true;
        }
        else if (e.window.event == SDL_WINDOWEVENT_RESIZED && !isFullscreenActive() && !g_cfg.windowMaximized)
        {
            SDL_GetWindowSize(g_window, &g_cfg.windowW, &g_cfg.windowH);
            g_cfg.windowSaved = true;
        }
    }
    else if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_JOYDEVICEADDED)
    {
        openPad((e.type == SDL_CONTROLLERDEVICEADDED) ? e.cdevice.which : e.jdevice.which);
    }
    else if (e.type == SDL_CONTROLLERDEVICEREMOVED || e.type == SDL_JOYDEVICEREMOVED)
    {
        closePad((e.type == SDL_CONTROLLERDEVICEREMOVED) ? e.cdevice.which : e.jdevice.which);
    }
    else if (e.type == SDL_CONTROLLERBUTTONDOWN)
    {
        SDL_GameController* gc = findController(e.cbutton.which);
        if (gc)
            handleControllerButton(gc, (SDL_GameControllerButton)e.cbutton.button);
    }
    else if (e.type == SDL_KEYDOWN)
    {
        if (g_rebindHotkey >= 0 && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
        {
            g_rebindHotkey = -1;
            g_rebindPendingMod = SDL_CONTROLLER_BUTTON_INVALID;
        }
        else if (g_rebindHotkeyKey >= 0)
        {
            if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
            {
                g_rebindHotkeyKey = -1;
                g_rebindHotkeyKeySlot = -1;
            }
            else
            {
                SDL_Scancode ksc = e.key.keysym.scancode;
                if (ksc == SDL_SCANCODE_LSHIFT || ksc == SDL_SCANCODE_RSHIFT ||
                    ksc == SDL_SCANCODE_LCTRL || ksc == SDL_SCANCODE_RCTRL ||
                    ksc == SDL_SCANCODE_LALT  || ksc == SDL_SCANCODE_RALT ||
                    ksc == SDL_SCANCODE_LGUI  || ksc == SDL_SCANCODE_RGUI)
                {
                    // Keep waiting for the primary key.
                }
                else
                {
                    int h = g_rebindHotkeyKey;
                    int slot = g_rebindHotkeyKeySlot;
                    if (slot < 0 || slot >= g_cfg.hotkeyKeyCount[h])
                        slot = g_cfg.hotkeyKeyCount[h];
                    if (slot < MAX_HOTKEY_KEYS_PER_ACTION)
                    {
                        Uint16 pressedMod = e.key.keysym.mod;
                        int fslot = 0;
                        if (ksc >= SDL_SCANCODE_F1 && ksc <= SDL_SCANCODE_F10)
                            fslot = (int)(ksc - SDL_SCANCODE_F1 + 1);
                        g_cfg.hotkeyKeys[h][slot].scancode = ksc;
                        g_cfg.hotkeyKeys[h][slot].mod = normalizeHotkeyMod(pressedMod);
                        g_cfg.hotkeyKeys[h][slot].slot = fslot;
                        if (slot >= g_cfg.hotkeyKeyCount[h])
                            g_cfg.hotkeyKeyCount[h] = slot + 1;
                        saveConfig();
                    }
                    g_rebindHotkeyKey = -1;
                    g_rebindHotkeyKeySlot = -1;
                }
            }
        }
        else if (g_rebindAction >= 0)
        {
            if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
            {
                g_rebindAction = -1;
                g_rebindSlot = -1;
            }
            else
            {
                int slot = g_rebindSlot;
                if (slot < 0 || slot >= g_cfg.keyCount[g_rebindAction])
                    slot = g_cfg.keyCount[g_rebindAction];
                if (slot < MAX_KEYS_PER_ACTION)
                {
                    g_cfg.keys[g_rebindAction][slot] = e.key.keysym.scancode;
                    if (slot >= g_cfg.keyCount[g_rebindAction])
                        g_cfg.keyCount[g_rebindAction] = slot + 1;
                }
                g_rebindAction = -1;
                g_rebindSlot = -1;
            }
        }
        else
        {
            if (e.key.repeat)
                return;

            SDL_Scancode sc = e.key.keysym.scancode;
            Uint16 mod = e.key.keysym.mod;

            // While rebinding a pad action, ESC cancels that rebinding.
            if (sc == SDL_SCANCODE_ESCAPE && g_rebindPadAction >= 0)
            {
                g_rebindPad = -1;
                g_rebindPadAction = -1;
                g_rebindPadSlot = -1;
                g_padRebindReady = false;
            }
            else if (sc == SDL_SCANCODE_ESCAPE &&
                     (g_showInput || g_showVideo || g_showAbout))
            {
                if (g_showInput) g_showInput = false;
                else if (g_showVideo) g_showVideo = false;
                else if (g_showAbout) g_showAbout = false;
            }
            else if (g_rebindHotkey >= 0 || g_rebindPadAction >= 0)
            {
                // Ignore hotkeys while a rebind is pending.
            }
            else
            {
                // Configurable keyboard hotkeys: ESC exits the game when no
                // dialog is open, Ctrl+C quits, F11 fullscreen, F1-F10 load,
                // Shift+F1-F10 save, F12 reset, Space pause.
                if (handleKeyboardHotkey(sc, mod))
                    return;

                // Fixed menu accelerators.
                if (sc == SDL_SCANCODE_O && (mod & KMOD_CTRL))
                {
                    std::string path = openRomDialog(g_window);
                    if (!path.empty()) loadRom(path);
                }
                if (sc == SDL_SCANCODE_W && (mod & KMOD_CTRL) && g_emu.gameLoaded())
                    closeGame();
            }
        }
    }
}

static bool windowRectVisible(int x, int y, int w, int h)
{
    for (int i = 0; i < SDL_GetNumVideoDisplays(); ++i)
    {
        SDL_Rect bounds{};
        if (SDL_GetDisplayUsableBounds(i, &bounds) != 0)
            continue;
        SDL_Rect window{x, y, w, h};
        SDL_Rect overlap{};
        if (SDL_IntersectRect(&window, &bounds, &overlap) && overlap.w >= 100 && overlap.h >= 100)
            return true;
    }
    return false;
}

static void centerWindowOnPrimary(int w, int h, int& x, int& y)
{
    SDL_Rect bounds{};
    if (SDL_GetDisplayUsableBounds(0, &bounds) == 0)
    {
        x = bounds.x + (bounds.w - w) / 2;
        y = bounds.y + (bounds.h - h) / 2;
    }
    else
    {
        x = SDL_WINDOWPOS_CENTERED;
        y = SDL_WINDOWPOS_CENTERED;
    }
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    populateScancodeNames();
    loadConfig();
    size_t recentCount = g_cfg.recentGames.size();
    g_cfg.recentGames.erase(
        std::remove_if(g_cfg.recentGames.begin(), g_cfg.recentGames.end(),
                       [](const std::string& path) { return !fileExists(path); }),
        g_cfg.recentGames.end());
    if (g_cfg.recentGames.size() != recentCount)
        saveConfig();
    // Always launch windowed; fullscreen is a runtime toggle only (F11).
    g_cfg.fullscreen = false;
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MesenMD",
                                 SDL_GetError(), nullptr);
        return 1;
    }

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    {
        std::string db = appDir() + "\\gamecontrollerdb.txt";
        if (FILE* h = fopen(db.c_str(), "r")) { fclose(h); SDL_GameControllerAddMappingsFromFile(db.c_str()); }
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int winW = g_cfg.windowW > 0 ? g_cfg.windowW : 1024;
    int winH = g_cfg.windowH > 0 ? g_cfg.windowH : 640;
    if (winW < 640) winW = 640;
    if (winH < 400) winH = 400;
    int winX = g_cfg.windowX;
    int winY = g_cfg.windowY;
    if (!g_cfg.windowSaved || !windowRectVisible(winX, winY, winW, winH))
        centerWindowOnPrimary(winW, winH, winX, winY);

    g_window = SDL_CreateWindow("MesenMD",
                                winX, winY,
                                winW, winH,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MesenMD", SDL_GetError(), nullptr);
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(g_window, 640, 400);
    if (g_cfg.windowSaved && g_cfg.windowMaximized)
        SDL_MaximizeWindow(g_window);

    g_glContext = SDL_GL_CreateContext(g_window);
    if (!g_glContext)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MesenMD",
                                 "Failed to create OpenGL context.", nullptr);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(g_window, g_glContext);
    SDL_GL_SetSwapInterval(1);
    // No text fields exist; keep the system IME off so a Chinese/Japanese IME
    // doesn't swallow SDL_KEYDOWN events and the SDL keyboard state.
    SDL_StopTextInput();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    applyTheme(true);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    {
        ImGuiIO& io = ImGui::GetIO();
        const char* fontPaths[] = {
            "C:\\Windows\\Fonts\\msyh.ttc",
            "C:\\Windows\\Fonts\\simhei.ttf",
            "C:\\Windows\\Fonts\\simsun.ttc",
        };
        for (const char* fp : fontPaths)
        {
            if (FILE* h = fopen(fp, "rb"))
            {
                fclose(h);
                io.Fonts->AddFontFromFileTTF(fp, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
                break;
            }
        }
    }

    ImGui_ImplSDL2_InitForOpenGL(g_window, g_glContext);
    ImGui_ImplOpenGL3_Init("#version 130");
    videoFiltersInit();

    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string corePath = exePath;
        size_t pos = corePath.find_last_of("\\/");
        if (pos != std::string::npos)
            corePath = corePath.substr(0, pos);
        corePath += "\\genesis_plus_gx_libretro.dll";
        if (!g_emu.loadCore(corePath))
        {
            std::string errMsg = "Failed to load core DLL:\n" + corePath;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MesenMD", errMsg.c_str(), g_window);
            videoFiltersShutdown();
    ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
            SDL_GL_DeleteContext(g_glContext);
            SDL_DestroyWindow(g_window);
            SDL_Quit();
            return 1;
        }
    }
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargc > 1)
        loadRom(utf16ToUtf8(wargv[1]));
    if (wargv)
        LocalFree(wargv);

    enumeratePads();

    g_running = true;
    Uint64 perfFreq = SDL_GetPerformanceFrequency();

    while (g_running)
    {
        Uint64 frameStart = SDL_GetPerformanceCounter();
        // Keep text input / IME off every frame (no text fields in this app).
        if (SDL_IsTextInputActive())
            SDL_StopTextInput();

        if (g_quitRequested)
        {
            doQuitSave();
            g_running = false;
            break;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e))
            handleEvent(e);
        updateInput();
        pollPadRebind();
        if (g_emu.gameLoaded())
            g_emu.runFrame();

        if (g_emu.gameLoaded() && g_audioDev && !g_emu.paused())
        {
            const std::vector<int16_t>& aq = g_emu.audioQueue();
            if (!aq.empty())
            {
                SDL_QueueAudio(g_audioDev, aq.data(), aq.size() * sizeof(int16_t));
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        updateVideoTexture();
        if (g_emu.gameLoaded())
        {
            Uint64 now = SDL_GetTicks64();
            if (now - g_lastThumbMs > 10000)
            {
                saveThumbnail();
                g_lastThumbMs = now;
            }
        }

        Uint64 now2 = SDL_GetTicks64();
        if (now2 - g_lastThemeCheckMs > 1000)
        {
            g_lastThemeCheckMs = now2;
            if (g_cfg.theme == 0)
                applyTheme(false);
        }
        drawMainMenu();
        drawViewport();
        drawStatusBar();
        drawVideoSettings();
        drawInputSettings();
        drawAbout();

        if (g_rebindAction >= 0 || g_rebindPadAction >= 0)
        {
            ImGui::Begin("##rebind", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SetWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 150,
                                       ImGui::GetIO().DisplaySize.y * 0.5f - 30));
            if (g_rebindAction >= 0)
                ImGui::Text(tr("Press a key for '%s'...", "请为 '%s' 按键..."), actionName(g_rebindAction));
            else
                ImGui::Text(tr("Press a button for '%s'... (Esc to cancel)", "请为 '%s' 按手柄按钮... (Esc 取消)"), actionName(g_rebindPadAction));
            ImGui::End();
        }

        ImGui::Render();
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(g_window, &dw, &dh);
        if (dw > 0 && dh > 0)
            glViewport(0, 0, dw, dh);
        glClearColor(g_clearColor.x, g_clearColor.y, g_clearColor.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(g_window);

        // Pace emulation to ~real-time using audio queue occupancy.
        if (g_emu.gameLoaded() && g_audioDev && !g_emu.paused())
        {
            double rate = g_emu.sampleRate();
            int maxBytes = (int)(rate * 0.20 * 2 * 2); // 200ms of stereo S16
            while (SDL_GetQueuedAudioSize(g_audioDev) > maxBytes)
            {
                SDL_Delay(1);
                SDL_Event e2;
                while (SDL_PollEvent(&e2))
                    handleEvent(e2);
            }
        }

        Uint64 end = SDL_GetPerformanceCounter();
        double elapsedMs = (double)(end - frameStart) * 1000.0 / (double)perfFreq;
        double targetMs = g_emu.gameLoaded() ? (1000.0 / (g_emu.fps() > 0 ? g_emu.fps() : 60.0)) : 16.67;
        if (elapsedMs < targetMs)
            SDL_Delay((Uint32)(targetMs - elapsedMs));
    }

    if (g_tex)
        glDeleteTextures(1, &g_tex);
    if (g_maskTex)
        glDeleteTextures(1, &g_maskTex);

    videoFiltersShutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    stopAudio();
    g_emu.unloadGame();
    g_emu.unloadCore();

    if (!isFullscreenActive())
    {
        if (!(SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED))
        {
            SDL_GetWindowPosition(g_window, &g_cfg.windowX, &g_cfg.windowY);
            SDL_GetWindowSize(g_window, &g_cfg.windowW, &g_cfg.windowH);
        }
        g_cfg.windowSaved = true;
    }
    g_cfg.windowMaximized = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED) != 0;

    SDL_GL_DeleteContext(g_glContext);
    SDL_DestroyWindow(g_window);
    shutdownPads();
    SDL_Quit();

    saveConfig();
    return 0;
}

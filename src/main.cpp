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
#include <algorithm>
#include <stb_image.h>
#include <stb_image_write.h>

#include "emulator.h"
#include "video_filters.h"

#define MESENMD_VERSION "1.0.2"

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

#define MAX_KEYS_PER_ACTION 8

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

struct Config
{
    int videoScale = 0;              // 0 = fit, 1+ = integer scale
    bool filterBilinear = false;
    bool showFps = false;
    bool audioEnabled = true;
    int  videoFilter = 0;          // 0=None 1=Bilinear 2=LCD Grid 3=CRT 4=Scanlines
    bool autoSaveState = true;    // save/restore game state on close/open
    int  keyboardPlayer = 0;      // joypad slot the keyboard drives (0=P1, 1=P2)
    bool fullscreen = false;      // start the window fullscreen
    int  language = 0;            // UI language: 0=English, 1=Chinese
    int  theme = 0;               // UI theme: 0=System(auto), 1=Dark, 2=Light
    SDL_Scancode keys[AID_COUNT][MAX_KEYS_PER_ACTION];
    int  keyCount[AID_COUNT];
    std::vector<std::string> recentGames;

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

static bool g_showVideo   = false;
static bool g_showInput   = false;
static bool g_showAbout   = false;
static int  g_rebindAction = -1;
static int  g_rebindSlot   = -1;
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

static std::vector<SDL_GameController*> g_controllers;
static std::vector<int> g_controllerPlayer;

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

static void populateScancodeNames()
{
    for (int i = 0; i < SDL_NUM_SCANCODES; ++i)
    {
        const char* name = SDL_GetScancodeName((SDL_Scancode)i);
        g_scancodeNames[i] = name ? name : "";
    }
}

static void saveConfig()
{
    std::ofstream f(configPath());
    if (!f) return;
    f << "videoScale=" << g_cfg.videoScale << "\n";
    f << "filterBilinear=" << (g_cfg.filterBilinear ? 1 : 0) << "\n";
    f << "showFps=" << (g_cfg.showFps ? 1 : 0) << "\n";
    f << "audioEnabled=" << (g_cfg.audioEnabled ? 1 : 0) << "\n";
    f << "videoFilter=" << g_cfg.videoFilter << "\n";
    f << "autoSaveState=" << (g_cfg.autoSaveState ? 1 : 0) << "\n";
    for (int a = 0; a < AID_COUNT; ++a)
        for (int k = 0; k < g_cfg.keyCount[a]; ++k)
            f << "key" << a << "_" << k << "=" << (int)g_cfg.keys[a][k] << "\n";
    f << "keyboardPlayer=" << g_cfg.keyboardPlayer << "\n";
    f << "fullscreen=" << (g_cfg.fullscreen ? 1 : 0) << "\n";
    f << "language=" << g_cfg.language << "\n";
    f << "theme=" << g_cfg.theme << "\n";
    for (size_t i = 0; i < g_cfg.recentGames.size(); ++i)
        f << "recent=" << g_cfg.recentGames[i] << "\n";
}

static void loadConfig()
{
    std::ifstream f(configPath());
    if (!f) return;
    std::string line;
    bool keyParsed[AID_COUNT] = { false };
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
        else if (key == "autoSaveState") g_cfg.autoSaveState = std::atoi(val.c_str()) != 0;
        else if (key == "keyboardPlayer") g_cfg.keyboardPlayer = std::atoi(val.c_str());
        else if (key == "fullscreen") g_cfg.fullscreen = std::atoi(val.c_str()) != 0;
        else if (key == "language") g_cfg.language = std::atoi(val.c_str());
        else if (key == "theme") g_cfg.theme = std::atoi(val.c_str());
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

static void autoLoadState()
{
    if (!g_cfg.autoSaveState || !g_emu.gameLoaded())
        return;
    std::string p = statePathForRom();
    if (fileExists(p) && g_emu.loadState(p))
        g_emu.setLastMessage("State loaded.");
}

static void stopAudio();  // defined in audio section below
static void saveThumbnail();  // defined in home-page section below

static void closeGame()
{
    autoSaveState();
    saveThumbnail();
    g_emu.unloadGame();
    stopAudio();
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

static void loadRom(const std::string& path)
{
    if (path.empty())
        return;

    if (g_emu.gameLoaded())
        g_emu.unloadGame();
    stopAudio();

    if (!g_emu.loadGame(path))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MesenMD",
                                 ("Failed to load ROM:\n" + path).c_str(), g_window);
        return;
    }

    startAudio(g_emu.sampleRate());
    addRecent(path);
    autoLoadState();
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
static void openController(int deviceIndex)
{
    if (deviceIndex < 0 || deviceIndex >= SDL_NumJoysticks())
        return;
    if (!SDL_IsGameController(deviceIndex))
        return;
    SDL_GameController* gc = SDL_GameControllerOpen(deviceIndex);
    if (!gc)
        return;
    g_controllers.push_back(gc);
    g_controllerPlayer.push_back((int)((g_controllers.size() - 1) % 2));
}

static void closeController(SDL_JoystickID instanceId)
{
    for (size_t i = 0; i < g_controllers.size(); ++i)
    {
        SDL_Joystick* joy = SDL_GameControllerGetJoystick(g_controllers[i]);
        if (joy && SDL_JoystickInstanceID(joy) == instanceId)
        {
            SDL_GameControllerClose(g_controllers[i]);
            g_controllers.erase(g_controllers.begin() + i);
            g_controllerPlayer.erase(g_controllerPlayer.begin() + i);
            return;
        }
    }
}

static void enumerateControllers()
{
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        openController(i);
}

static void shutdownControllers()
{
    for (SDL_GameController* gc : g_controllers)
        SDL_GameControllerClose(gc);
    g_controllers.clear();
    g_controllerPlayer.clear();
}

// Map a standard SDL_GameController to a libretro joypad bitmask.
static uint32_t controllerMask(SDL_GameController* gc)
{
    uint32_t m = 0;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) m |= (1u << RETRO_DEVICE_ID_JOYPAD_B);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B)) m |= (1u << RETRO_DEVICE_ID_JOYPAD_A);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X)) m |= (1u << RETRO_DEVICE_ID_JOYPAD_Y);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y)) m |= (1u << RETRO_DEVICE_ID_JOYPAD_X);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK))  m |= (1u << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) m |= (1u << RETRO_DEVICE_ID_JOYPAD_START);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_GUIDE)) m |= (1u << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) m |= (1u << RETRO_DEVICE_ID_JOYPAD_L);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) m |= (1u << RETRO_DEVICE_ID_JOYPAD_R);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP))    m |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  m |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  m |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) m |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    int lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    if (lx > 20000)  m |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (lx < -20000) m |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (ly > 20000)  m |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (ly < -20000) m |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
    return m;
}

static void updateInput()
{
    if (!g_emu.gameLoaded())
        return;

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    uint32_t mask[4] = {0, 0, 0, 0};

    // Keyboard -> the configured player slot (default Player 1).
    {
        int p = (g_cfg.keyboardPlayer >= 0 && g_cfg.keyboardPlayer < 2) ? g_cfg.keyboardPlayer : 0;
        uint32_t m = 0;
        for (int a = 0; a < AID_COUNT; ++a)
            for (int k = 0; k < g_cfg.keyCount[a]; ++k)
                if (keys[g_cfg.keys[a][k]])
                    m |= (1u << actionToRetroId(a));
        mask[p] |= m;
    }

    // Gamepads: each connected controller drives its assigned player slot.
    for (size_t i = 0; i < g_controllers.size(); ++i)
    {
        int p = g_controllerPlayer[i];
        if (p < 0 || p >= 2) p = 0;
        mask[p] |= controllerMask(g_controllers[i]);
    }

    g_emu.setJoypadMask(0, mask[0]);
    g_emu.setJoypadMask(1, mask[1]);
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
static void drawMainMenu()
{
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
                    if (ImGui::MenuItem(g_cfg.recentGames[i].c_str()))
                        loadRom(g_cfg.recentGames[i]);
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
            ImGui::MenuItem(tr("Auto-save state on close", "关闭时自动保存状态"), nullptr, &g_cfg.autoSaveState);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(tr("Config", "配置")))
        {
            if (ImGui::MenuItem(tr("Video...", "视频..."))) g_showVideo = true;
            if (ImGui::MenuItem(tr("Input...", "输入..."))) g_showInput = true;
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
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - 24));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 24));
    ImGui::Begin("##status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (g_emu.gameLoaded())
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s  |  %ux%u  |  %.1f fps",
                 g_emu.coreName().c_str(), g_emu.videoWidth(), g_emu.videoHeight(), g_emu.fps());
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
    ImGui::SetNextWindowSize(ImVec2(420, 250), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(tr("Video", "视频"), &g_showVideo))
    {
        ImGui::Combo(tr("Window scale", "窗口缩放"), &g_cfg.videoScale, comboWindowScale());
        ImGui::Combo(tr("Video filter", "视频滤镜"), &g_cfg.videoFilter, comboVideoFilter());
        ImGui::Checkbox(tr("Bilinear when no filter", "无滤镜时使用双线性"), &g_cfg.filterBilinear);
        ImGui::Checkbox(tr("Show FPS in status bar", "在状态栏显示帧率"), &g_cfg.showFps);
        ImGui::Checkbox(tr("Enable audio", "启用音频"), &g_cfg.audioEnabled);
        ImGui::Combo(tr("Language", "语言"), &g_cfg.language, "English\0中文\0");
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

static void drawInputSettings()
{
    if (!g_showInput) return;
    ImGui::SetNextWindowSize(ImVec2(760, 540), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(tr("Input", "输入"), &g_showInput))
    {
        ImGui::TextWrapped(tr("Click a binding, then press a key to change it. '+' adds an extra key, 'x' removes one. The keyboard and every connected gamepad drive the player at the same time.", "点击绑定项后按键盘即可更改。'+' 添加按键，'x' 移除按键。键盘和所有已连接的手柄会同时驱动玩家。"));

        ImGui::Separator();
        ImGui::Text(tr("Keyboard", "键盘"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::Combo("##kbplayer", &g_cfg.keyboardPlayer, comboPlayer());

        ImGui::Separator();
        ImGui::Columns(3, "inputcols", true);
        ImGui::Text(tr("Action", "操作")); ImGui::NextColumn();
        ImGui::Text(tr("Bindings", "按键")); ImGui::NextColumn();
        ImGui::Text(tr("Reset", "重置")); ImGui::NextColumn();
        ImGui::Separator();

        for (int a = 0; a < AID_COUNT; ++a)
        {
            ImGui::Text("%s", actionName(a));
            ImGui::NextColumn();

            ImGui::BeginGroup();
            for (int k = 0; k < g_cfg.keyCount[a]; ++k)
            {
                SDL_Scancode sc = g_cfg.keys[a][k];
                const char* name = (sc >= 0 && sc < SDL_NUM_SCANCODES && g_scancodeNames[sc]) ? g_scancodeNames[sc] : "?";
                std::string label;
                if (g_rebindAction == a && g_rebindSlot == k)
                    label = tr("Press key...", "请按键...");
                else
                    label = name;
                char btn[96];
                snprintf(btn, sizeof(btn), "%s##k%d_%d", label.c_str(), a, k);
                if (ImGui::Button(btn, ImVec2(150, 0)))
                {
                    g_rebindAction = a;
                    g_rebindSlot = k;
                }
                ImGui::SameLine();
                char del[64];
                snprintf(del, sizeof(del), "x##k%d_%d", a, k);
                if (ImGui::SmallButton(del))
                {
                    g_cfg.removeKey(a, k);
                    if (g_rebindAction == a) { g_rebindAction = -1; g_rebindSlot = -1; }
                }
            }
            if (g_cfg.keyCount[a] < MAX_KEYS_PER_ACTION)
            {
                char add[64];
                snprintf(add, sizeof(add), "+ %s##add%d", tr("Add", "添加"), a);
                if (ImGui::SmallButton(add))
                {
                    g_rebindAction = a;
                    g_rebindSlot = -1;
                }
            }
            ImGui::EndGroup();
            ImGui::NextColumn();

            char rLabel[64];
            snprintf(rLabel, sizeof(rLabel), "%s##%d", tr("Reset", "重置"), a);
            if (ImGui::Button(rLabel))
            {
                g_cfg.keyCount[a] = g_defaultKeyCount[a];
                for (int k = 0; k < g_defaultKeyCount[a]; ++k)
                    g_cfg.keys[a][k] = g_defaultKeys[a][k];
            }
            ImGui::NextColumn();
        }
        ImGui::Columns(1);

        ImGui::Separator();
        ImGui::Text(tr("Gamepads", "手柄"));
        if (g_controllers.empty())
        {
            ImGui::TextDisabled(tr("No gamepad detected. Plug in a USB / Xbox / DirectInput pad - standard pads auto-map. For a custom pad, put a 'gamecontrollerdb.txt' next to MesenMD.exe.", "未检测到手柄。请插入 USB / Xbox / DirectInput 手柄——标准手柄会自动映射。如需自定义手柄，请将 'gamecontrollerdb.txt' 放在 MesenMD.exe 旁边。"));
        }
        for (size_t i = 0; i < g_controllers.size(); ++i)
        {
            const char* name = SDL_GameControllerName(g_controllers[i]);
            ImGui::Text("%s", name ? name : tr("Gamepad", "手柄"));
            ImGui::SameLine();
            int p = g_controllerPlayer[i];
            ImGui::PushID((int)i);
            ImGui::SetNextItemWidth(150);
            if (ImGui::Combo(tr("Player", "玩家"), &p, comboPlayer()))
                g_controllerPlayer[i] = p;
            ImGui::PopID();
        }
        ImGui::End();
    }
}

static void drawAbout()
{
    if (!g_showAbout) return;
    ImGui::SetNextWindowSize(ImVec2(460, 240), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(tr("About", "关于"), &g_showAbout))
    {
        ImGui::TextUnformatted("MesenMD");
        ImGui::SameLine();
        ImGui::TextDisabled(MESENMD_VERSION);
        ImGui::Separator();
        ImGui::TextWrapped(tr("A Mesen2-inspired Sega Mega Drive / Genesis emulator.", "一款灵感来自 Mesen2 的世嘉 Mega Drive / Genesis 模拟器。"));
        ImGui::TextWrapped(tr("Core: Genesis Plus GX (libretro).  UI: SDL2 + OpenGL + Dear ImGui.", "核心：Genesis Plus GX (libretro)。界面：SDL2 + OpenGL + Dear ImGui。"));
        ImGui::TextWrapped(tr("GPLv3 license. Intended for legally owned ROMs only.", "GPLv3 许可。仅用于合法拥有的 ROM。"));
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

static void drawHomePage()
{
    ImGui::Text(tr("Recent Games", "最近游戏"));
    ImGui::Separator();
    ImGui::BeginChild("##homeScroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (g_cfg.recentGames.empty())
    {
        ImGui::TextDisabled(tr("No recent games.\nOpen a ROM to begin.", "无最近游戏。\n请打开一个 ROM 开始。"));
        ImGui::EndChild();
        return;
    }

    const float cardW = 184, gap = 14, imgH = 120;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fpX = style.FramePadding.x;
    const float padX = style.WindowPadding.x;

    float avail = ImGui::GetContentRegionAvail().x;
    int cols = (int)(avail / (cardW + gap));
    if (cols < 1) cols = 1;
    float contentW = (float)cols * cardW + (float)(cols - 1) * gap;
    float startX = (avail - contentW) * 0.5f;
    if (startX < 0.0f) startX = 0.0f;

    std::string clicked;
    std::string deleted;
    int col = 0;
    for (size_t i = 0; i < g_cfg.recentGames.size(); ++i)
    {
        const std::string& path = g_cfg.recentGames[i];
        if (col == 0)
            ImGui::SetCursorPosX(startX);
        else
            ImGui::SameLine(0, gap);
        col++;

        std::string name = path;
        size_t pos = name.find_last_of("\\/");
        if (pos != std::string::npos) name = name.substr(pos + 1);

        ImGui::BeginChild(("##card" + std::to_string(i)).c_str(), ImVec2(cardW, imgH + 36), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // ImageButton() adds FramePadding*2 around the image, so shift the cursor so the
        // image content itself is centered within the card instead of being pushed right.
        float imgW = cardW - 2.0f * padX - 2.0f * fpX;
        if (imgW <= 0.0f) imgW = cardW - 2.0f * fpX;
        float imgX = (cardW - imgW) * 0.5f - fpX;
        GLuint tex = getThumbTexture(thumbPathForPath(path));
        ImVec2 imgSize(imgW, imgH);
        if (tex)
        {
            ImGui::SetCursorPosX(imgX);
            if (ImGui::ImageButton(("##thumb" + std::to_string(i)).c_str(),
                                   (ImTextureID)(intptr_t)tex, imgSize))
                clicked = path;
        }
        else
        {
            // Button() uses the given size directly (no FramePadding), so center it on its own.
            ImGui::SetCursorPosX((cardW - imgSize.x) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.28f, 1.0f));
            if (ImGui::Button(tr("No snapshot", "无快照"), imgSize))
                clicked = path;
            ImGui::PopStyleColor(2);
        }

        ImGui::SetCursorPos(ImVec2(cardW - 28, 2));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.2f, 0.2f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button(("x##del" + std::to_string(i)).c_str(), ImVec2(24, 22)))
            deleted = path;
        ImGui::PopStyleColor(3);

        ImGui::SetCursorPos(ImVec2(4, imgH + 12));
        ImGui::TextWrapped("%s", name.c_str());
        ImGui::EndChild();

        if (col >= cols) col = 0;
    }
    ImGui::EndChild();

    if (!deleted.empty())
        deleteRecentSnapshot(deleted);
    if (!clicked.empty())
        loadRom(clicked);
}
static void drawViewport()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float yTop = vp->WorkPos.y + ImGui::GetFrameHeight();
    float yBottom = vp->WorkPos.y + vp->WorkSize.y - 24;
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
    ImGui::SetCursorScreenPos(drawPos);
    ImGui::Image((ImTextureID)(intptr_t)displayTex, imgSize);
    (void)availStart;

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    populateScancodeNames();
    loadConfig();
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MesenMD",
                                 SDL_GetError(), nullptr);
        return 1;
    }

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);
    {
        std::string db = appDir() + "\\gamecontrollerdb.txt";
        if (FILE* h = fopen(db.c_str(), "r")) { fclose(h); SDL_GameControllerAddMappingsFromFile(db.c_str()); }
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow("MesenMD",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                1024, 640,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MesenMD", SDL_GetError(), nullptr);
        SDL_Quit();
        return 1;
    }

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

    if (g_cfg.fullscreen)
        SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    applyTheme(true);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

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

    enumerateControllers();

    bool running = true;
    Uint64 perfFreq = SDL_GetPerformanceFrequency();

    while (running)
    {
        Uint64 frameStart = SDL_GetPerformanceCounter();

        if (g_quitRequested)
        {
            autoSaveState();
            saveThumbnail();
            running = false;
            break;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT)
            {
                autoSaveState();
                saveThumbnail();
                running = false;
            }
            else if (e.type == SDL_DROPFILE)
            {
                if (e.drop.file)
                {
                    loadRom(e.drop.file);
                    SDL_free(e.drop.file);
                }
            }
            else if (e.type == SDL_CONTROLLERDEVICEADDED)
            {
                openController(e.cdevice.which);
            }
            else if (e.type == SDL_CONTROLLERDEVICEREMOVED)
            {
                closeController(e.cdevice.which);
            }
            else if (e.type == SDL_KEYDOWN)
            {
                if (g_rebindAction >= 0)
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
                else
                {
                    SDL_Scancode sc = e.key.keysym.scancode;
                    Uint16 mod = e.key.keysym.mod;

                    // ESC: close an open dialog first, otherwise exit the game (auto-saves).
                    if (sc == SDL_SCANCODE_ESCAPE)
                    {
                        if (g_showInput) g_showInput = false;
                        else if (g_showVideo) g_showVideo = false;
                        else if (g_showAbout) g_showAbout = false;
                        else if (g_emu.gameLoaded()) closeGame();
                    }

                    // Ctrl+C: quit the program gracefully (saves state + thumbnail).
                    if (sc == SDL_SCANCODE_C && (mod & KMOD_CTRL))
                    {
                        SDL_Event quit{};
                        quit.type = SDL_QUIT;
                        SDL_PushEvent(&quit);
                    }

                    // F11: toggle fullscreen.
                    if (sc == SDL_SCANCODE_F11)
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

                    // F1-F10: quick load; Shift+F1-F10: quick save.
                    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10 && g_emu.gameLoaded())
                    {
                        int slot = sc - SDL_SCANCODE_F1 + 1;
                        std::string p = slotStatePathForRom(slot);
                        if (mod & KMOD_SHIFT)
                        {
                            if (g_emu.saveState(p))
                                g_emu.setLastMessage("State saved.");
                        }
                        else
                        {
                            if (fileExists(p) && g_emu.loadState(p))
                                g_emu.setLastMessage("State loaded.");
                        }
                    }

                    if (sc == SDL_SCANCODE_F12 && g_emu.gameLoaded())
                        g_emu.reset();
                    if (sc == SDL_SCANCODE_SPACE && g_emu.gameLoaded())
                        togglePause();
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

        updateInput();
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

        if (g_rebindAction >= 0)
        {
            // Show a hint in the viewport
            ImGui::Begin("##rebind", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SetWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 120,
                                       ImGui::GetIO().DisplaySize.y * 0.5f - 30));
            ImGui::Text(tr("Press a key for '%s'...", "请为 '%s' 按键..."), actionName(g_rebindAction));
            ImGui::End();
        }

        ImGui::Render();
        int dw, dh;
        SDL_GetWindowSize(g_window, &dw, &dh);
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
                SDL_PollEvent(&e); // keep responsive
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

    videoFiltersShutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    stopAudio();
    g_emu.unloadGame();
    g_emu.unloadCore();

    SDL_GL_DeleteContext(g_glContext);
    SDL_DestroyWindow(g_window);
    shutdownControllers();
    SDL_Quit();

    saveConfig();
    return 0;
}
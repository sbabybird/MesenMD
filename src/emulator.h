#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>

#if defined(_WIN32)
#  include <windows.h>
#endif

#include <libretro.h>

class Emulator
{
public:
    Emulator();
    ~Emulator();

    Emulator(const Emulator&) = delete;
    Emulator& operator=(const Emulator&) = delete;

    bool loadCore(const std::string& dllPath);
    void unloadCore();

    bool loadGame(const std::string& romPath);
    void unloadGame();

    bool coreLoaded() const { return coreLoaded_; }
    bool gameLoaded() const { return gameLoaded_; }
    bool isRunning() const { return running_; }
    void setPaused(bool value) { paused_ = value; }
    bool paused() const { return paused_; }

    void runFrame();
    void reset();

    bool saveState(const std::string& path);
    bool loadState(const std::string& path);
    bool saveSram(const std::string& path);
    bool loadSram(const std::string& path);
    size_t saveStateSize();
    const std::string& romPath() const { return romPath_; }

    const std::vector<uint8_t>& videoFrame() const { return videoFrame_; }
    unsigned videoWidth()  const { return videoWidth_; }
    unsigned videoHeight() const { return videoHeight_; }
    size_t   videoPitch()  const { return videoPitch_; }
    retro_pixel_format pixelFormat() const { return pixelFormat_; }

    const std::vector<int16_t>& audioQueue() const { return audioQueue_; }
    void clearAudioQueue() { audioQueue_.clear(); }

    double sampleRate() const { return avInfo_.timing.sample_rate; }
    double fps()         const { return avInfo_.timing.fps; }

    const retro_system_info&    systemInfo() const { return systemInfo_; }
    const retro_system_av_info& avInfo()     const { return avInfo_; }
    std::string coreName() const { return systemInfo_.library_name ? systemInfo_.library_name : ""; }

    void setJoypadMask(int player, uint32_t mask)
    {
        if (player >= 0 && player < 4)
            joypadMask_[player] = mask;
    }

    const std::string& lastMessage() const { return lastMessage_; }
    void setLastMessage(const std::string& m) { lastMessage_ = m; }

private:
    struct CoreApi;
    std::unique_ptr<CoreApi> api_;

    bool coreLoaded_ = false;
    bool gameLoaded_ = false;
    bool running_    = false;
    bool paused_     = false;

    retro_system_info    systemInfo_{};
    retro_system_av_info avInfo_{};

    retro_pixel_format pixelFormat_ = RETRO_PIXEL_FORMAT_RGB565;
    std::vector<uint8_t> videoFrame_;
    unsigned videoWidth_  = 0;
    unsigned videoHeight_ = 0;
    size_t   videoPitch_  = 0;

    std::vector<int16_t> audioQueue_;
    uint32_t joypadMask_[4] = {0, 0, 0, 0};
    std::string romPath_;
    std::string lastMessage_;
    std::vector<uint8_t> sramInitial_;

    static bool environmentCallback(unsigned cmd, void* data);
    static void videoRefreshCallback(const void* data, unsigned width, unsigned height, size_t pitch);
    static void audioSampleCallback(int16_t left, int16_t right);
    static size_t audioSampleBatchCallback(const int16_t* data, size_t frames);
    static void inputPollCallback();
    static int16_t inputStateCallback(unsigned port, unsigned device, unsigned index, unsigned id);
    static void logCallback(enum retro_log_level level, const char* fmt, ...);

    bool handleEnvironment(unsigned cmd, void* data);
    void captureSramInitial();
    void handleVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch);
    void handleAudioSample(int16_t left, int16_t right);
    size_t handleAudioSampleBatch(const int16_t* data, size_t frames);
    void handleInputPoll() {}
    int16_t handleInputState(unsigned port, unsigned device, unsigned index, unsigned id);
};

extern Emulator* g_emulator;

#include "emulator.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>

Emulator* g_emulator = nullptr;

struct Emulator::CoreApi
{
    HMODULE module = nullptr;

    unsigned (*api_version)() = nullptr;
    void     (*init)() = nullptr;
    void     (*deinit)() = nullptr;
    void     (*get_system_info)(retro_system_info*) = nullptr;
    void     (*get_system_av_info)(retro_system_av_info*) = nullptr;
    void     (*set_environment)(retro_environment_t) = nullptr;
    void     (*set_video_refresh)(retro_video_refresh_t) = nullptr;
    void     (*set_audio_sample)(retro_audio_sample_t) = nullptr;
    void     (*set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
    void     (*set_input_poll)(retro_input_poll_t) = nullptr;
    void     (*set_input_state)(retro_input_state_t) = nullptr;
    void     (*set_controller_port_device)(unsigned, unsigned) = nullptr;
    bool     (*load_game)(const retro_game_info*) = nullptr;
    void     (*run)() = nullptr;
    size_t   (*serialize_size)() = nullptr;
    bool     (*serialize)(void*, size_t) = nullptr;
    bool     (*unserialize)(const void*, size_t) = nullptr;
    void     (*reset)() = nullptr;
    void     (*unload_game)() = nullptr;
    unsigned (*get_region)() = nullptr;
};

Emulator::Emulator()
{
    g_emulator = this;
}

Emulator::~Emulator()
{
    unloadGame();
    unloadCore();
    if (g_emulator == this)
        g_emulator = nullptr;
}

bool Emulator::loadCore(const std::string& dllPath)
{
    unloadCore();

    auto api = std::make_unique<CoreApi>();
    api->module = LoadLibraryA(dllPath.c_str());
    if (!api->module)
        return false;

    #define RESOLVE(member, name) \
        api->member = reinterpret_cast<decltype(api->member)>(GetProcAddress(api->module, name)); \
        if (!api->member) { FreeLibrary(api->module); return false; }

    RESOLVE(api_version,               "retro_api_version");
    RESOLVE(init,                      "retro_init");
    RESOLVE(deinit,                    "retro_deinit");
    RESOLVE(get_system_info,           "retro_get_system_info");
    RESOLVE(get_system_av_info,        "retro_get_system_av_info");
    RESOLVE(set_environment,           "retro_set_environment");
    RESOLVE(set_video_refresh,         "retro_set_video_refresh");
    RESOLVE(set_audio_sample,          "retro_set_audio_sample");
    RESOLVE(set_audio_sample_batch,    "retro_set_audio_sample_batch");
    RESOLVE(set_input_poll,            "retro_set_input_poll");
    RESOLVE(set_input_state,           "retro_set_input_state");
    RESOLVE(set_controller_port_device,"retro_set_controller_port_device");
    RESOLVE(load_game,                 "retro_load_game");
    RESOLVE(run,                       "retro_run");
    RESOLVE(serialize_size,           "retro_serialize_size");
    RESOLVE(serialize,                "retro_serialize");
    RESOLVE(unserialize,              "retro_unserialize");
    RESOLVE(reset,                     "retro_reset");
    RESOLVE(unload_game,               "retro_unload_game");
    RESOLVE(get_region,                "retro_get_region");
    #undef RESOLVE

    api->set_environment(&Emulator::environmentCallback);
    api->set_video_refresh(&Emulator::videoRefreshCallback);
    api->set_audio_sample(&Emulator::audioSampleCallback);
    api->set_audio_sample_batch(&Emulator::audioSampleBatchCallback);
    api->set_input_poll(&Emulator::inputPollCallback);
    api->set_input_state(&Emulator::inputStateCallback);

    api->init();
    api->get_system_info(&systemInfo_);
    api_ = std::move(api);
    coreLoaded_ = true;
    return true;
}

void Emulator::unloadCore()
{
    if (api_)
    {
        api_->deinit();
        FreeLibrary(api_->module);
        api_.reset();
    }
    coreLoaded_ = false;
    gameLoaded_ = false;
}

bool Emulator::loadGame(const std::string& romPath)
{
    if (!coreLoaded_)
        return false;

    unloadGame();

    /* Convert the UTF-8 path to UTF-16 for an existence check. */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, romPath.c_str(), -1, nullptr, 0);
    std::wstring wpath;
    if (wlen > 1)
    {
        wpath.resize(static_cast<size_t>(wlen - 1));
        MultiByteToWideChar(CP_UTF8, 0, romPath.c_str(), -1, &wpath[0], wlen - 1);
    }
    if (wpath.empty() || GetFileAttributesW(wpath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;

    retro_game_info info{};
    info.path = romPath.c_str();
    info.data = nullptr;
    info.size = 0;
    info.meta = nullptr;

    bool ok = false;
    try
    {
        ok = api_->load_game(&info);
    }
    catch (...)
    {
        ok = false;
    }

    if (ok)
    {
        api_->get_system_av_info(&avInfo_);
        api_->set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
        api_->set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
        gameLoaded_ = true;
        romPath_ = romPath;
    }
    return ok;
}
void Emulator::unloadGame()
{
    if (coreLoaded_ && api_ && gameLoaded_)
        api_->unload_game();
    gameLoaded_ = false;
    romPath_.clear();
    videoFrame_.clear();
    videoWidth_ = videoHeight_ = 0;
}

void Emulator::runFrame()
{
    if (!coreLoaded_ || !gameLoaded_ || paused_)
        return;
    audioQueue_.clear();
    running_ = true;
    api_->run();
    running_ = false;
}

void Emulator::reset()
{
    if (coreLoaded_ && gameLoaded_)
        api_->reset();
}

bool Emulator::saveState(const std::string& path)
{
    if (!coreLoaded_ || !gameLoaded_ || !api_->serialize)
        return false;

    size_t size = api_->serialize_size();
    if (size == 0)
        return false;

    std::vector<uint8_t> buf(size);
    if (!api_->serialize(buf.data(), buf.size()))
        return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    return f.good();
}

bool Emulator::loadState(const std::string& path)
{
    if (!coreLoaded_ || !gameLoaded_ || !api_->unserialize)
        return false;

    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.seekg(0, std::ios::end);
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size <= 0)
        return false;
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    if (!f)
        return false;
    return api_->unserialize(buf.data(), buf.size());
}

size_t Emulator::saveStateSize()
{
    if (!coreLoaded_ || !gameLoaded_ || !api_->serialize_size)
        return 0;
    return api_->serialize_size();
}

// ----- Callback trampolines -----

bool Emulator::environmentCallback(unsigned cmd, void* data)
{
    return g_emulator ? g_emulator->handleEnvironment(cmd, data) : false;
}

void Emulator::videoRefreshCallback(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (g_emulator)
        g_emulator->handleVideoRefresh(data, width, height, pitch);
}

void Emulator::audioSampleCallback(int16_t left, int16_t right)
{
    if (g_emulator)
        g_emulator->handleAudioSample(left, right);
}

size_t Emulator::audioSampleBatchCallback(const int16_t* data, size_t frames)
{
    return g_emulator ? g_emulator->handleAudioSampleBatch(data, frames) : 0;
}

void Emulator::inputPollCallback()
{
    if (g_emulator)
        g_emulator->handleInputPoll();
}

int16_t Emulator::inputStateCallback(unsigned port, unsigned device, unsigned index, unsigned id)
{
    return g_emulator ? g_emulator->handleInputState(port, device, index, id) : 0;
}

void Emulator::logCallback(enum retro_log_level level, const char* fmt, ...)
{
    (void)level;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fprintf(stderr, "[core] %s\n", buf);
}

// ----- Instance handlers -----

bool Emulator::handleEnvironment(unsigned cmd, void* data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        {
            pixelFormat_ = *(const retro_pixel_format*)data;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        {
            auto* cb = (retro_log_callback*)data;
            if (cb)
                cb->log = &Emulator::logCallback;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        {
            if (data)
                *(unsigned*)data = 1;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_MESSAGE:
        {
            const auto* msg = (const retro_message*)data;
            if (msg && msg->msg)
                lastMessage_ = msg->msg;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        {
            const auto* msg = (const retro_message_ext*)data;
            if (msg && msg->msg)
                lastMessage_ = msg->msg;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        {
            const auto* info = (const retro_system_av_info*)data;
            if (info)
                avInfo_ = *info;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        {
            const auto* info = (const retro_system_av_info*)data;
            if (info)
                avInfo_.geometry = info->geometry;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        case RETRO_ENVIRONMENT_GET_OVERSCAN:
        case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_ROTATION:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        case RETRO_ENVIRONMENT_SET_HW_RENDER:
        default:
            return false;
    }
}

void Emulator::handleVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || width == 0 || height == 0)
        return;

    videoWidth_  = width;
    videoHeight_ = height;
    videoPitch_  = pitch;

    size_t bytesPerPixel = (pixelFormat_ == RETRO_PIXEL_FORMAT_XRGB8888) ? 4 : 2;
    size_t needed = pitch * height;
    if (videoFrame_.size() < needed)
        videoFrame_.resize(needed);
    if (videoFrame_.size() >= needed)
        std::memcpy(videoFrame_.data(), data, needed);
    (void)bytesPerPixel;
}

void Emulator::handleAudioSample(int16_t left, int16_t right)
{
    audioQueue_.push_back(left);
    audioQueue_.push_back(right);
}

size_t Emulator::handleAudioSampleBatch(const int16_t* data, size_t frames)
{
    if (!data || frames == 0)
        return frames;
    audioQueue_.insert(audioQueue_.end(), data, data + frames * 2);
    return frames;
}

int16_t Emulator::handleInputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (device != RETRO_DEVICE_JOYPAD || port >= 4)
        return 0;

    uint32_t mask = joypadMask_[port];

    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        return static_cast<int16_t>(mask & 0xFFFF);

    if (id < 32)
        return (mask & (1u << id)) ? 1 : 0;

    return 0;
}
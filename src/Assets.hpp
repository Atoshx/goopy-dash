#pragma once
#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>
#include <atomic>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "cupread/cupread.hpp"

namespace goopy {

struct FrameTex {
    cocos2d::CCTexture2D* texture = nullptr;
    float centerX = 0, centerY = 0; // source pixels, pivot-relative, y up
    float width = 0, height = 0;
};

// Goopy's art and sounds, read from the player's own Cuphead install.
class Assets {
public:
    static Assets& get();

    // Starts reading Cuphead's files on a worker thread (safe to call repeatedly).
    void startLoading();
    bool ready() const { return m_state == State::Ready; }
    bool failed() const { return m_state == State::Failed; }
    std::string const& error() const { return m_error; }
    std::filesystem::path const& cupheadDir() const { return m_cupheadDir; }

    // Main thread only. Builds textures/sounds on first use.
    std::vector<FrameTex> const& anim(std::string_view id);
    void play(std::string_view soundId);

private:
    enum class State { Idle, Loading, Ready, Failed };
    std::atomic<State> m_state = State::Idle;
    std::string m_error;
    std::filesystem::path m_cupheadDir;
    std::map<std::string, cupread::Frame> m_frames;
    std::map<std::string, std::vector<uint8_t>> m_clips;
    std::map<std::string, std::vector<FrameTex>, std::less<>> m_anims;
    std::map<std::string, FMOD::Sound*, std::less<>> m_sounds;
    std::map<std::string, FMOD::Sound*, std::less<>> m_subSounds;
    unsigned m_rng = 12345;

    FMOD::Sound* sound(std::string const& clip);
};

std::filesystem::path findCupheadDir();

} // namespace goopy

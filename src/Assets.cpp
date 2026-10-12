#include "Assets.hpp"
#include "generated/Sheets.hpp"
#include "TestMode.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <thread>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace goopy {

namespace {

bool isCupheadDir(fs::path const& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(p / "Cuphead_Data" / "StreamingAssets" / "AssetBundles" / "atlas_slimelevel", ec);
}

fs::path envPath(char const* name) {
#ifdef GEODE_IS_WINDOWS
    std::wstring wname(name, name + std::strlen(name));
    if (wchar_t const* v = _wgetenv(wname.c_str()); v && *v) return fs::path(v);
#else
    if (char const* v = std::getenv(name); v && *v) return fs::path(v);
#endif
    return {};
}

std::vector<fs::path> steamLibraries() {
    std::vector<fs::path> libs;
#ifdef GEODE_IS_WINDOWS
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) {
        fs::path steam(buf);
        libs.push_back(steam);
        std::ifstream vdf(steam / "steamapps" / "libraryfolders.vdf");
        std::string line;
        static const std::regex pathRe("\"path\"\\s+\"([^\"]+)\"");
        while (std::getline(vdf, line)) {
            std::smatch m;
            if (std::regex_search(line, m, pathRe)) {
                std::string p = m[1].str();
                std::string unescaped;
                for (size_t i = 0; i < p.size(); ++i) {
                    if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') ++i;
                    unescaped += p[i];
                }
                libs.push_back(fs::path(utils::string::utf8ToWide(unescaped)));
            }
        }
    }
#endif
    return libs;
}

} // namespace

fs::path findCupheadDir() {
    // 1. Melty passes the player's Cuphead folder when it launches the mashup.
    if (auto p = envPath("GOOPYDASH_CUPHEAD_DIR"); isCupheadDir(p)) return p;
    // 2. The folder the player picked in the mod's settings.
    if (auto p = Mod::get()->getSettingValue<fs::path>("cuphead-folder"); isCupheadDir(p)) return p;
    // 3. Next to Geometry Dash in the same Steam library, then every Steam library.
    if (auto p = dirs::getGameDir().parent_path() / "Cuphead"; isCupheadDir(p)) return p;
    for (auto const& lib : steamLibraries())
        if (auto p = lib / "steamapps" / "common" / "Cuphead"; isCupheadDir(p)) return p;
    return {};
}

Assets& Assets::get() {
    static Assets inst;
    return inst;
}

void Assets::startLoading() {
    State expected = State::Idle;
    if (!m_state.compare_exchange_strong(expected, State::Loading)) return;
    m_cupheadDir = findCupheadDir();
    if (m_cupheadDir.empty()) {
        m_error = "Cuphead isn't installed (or its folder wasn't found). Goopy needs your copy of Cuphead.";
        m_state = State::Failed;
        log::error("{}", m_error);
        test::report("cuphead-found", false, "Cuphead folder not found");
        return;
    }
    log::info("Reading Goopy Le Grande from {}", utils::string::pathToString(m_cupheadDir));
    std::thread([this] {
        auto data = m_cupheadDir / "Cuphead_Data";
        auto bundles = data / "StreamingAssets" / "AssetBundles";
        std::vector<std::string> prefixes;
        for (auto const& a : sheets::kAnims) prefixes.push_back(std::string(a.prefix) + "_");
        std::vector<std::string> clips;
        for (auto const& s : sheets::kSounds)
            for (int i = 0; i < s.clipCount; ++i) clips.emplace_back(s.clips[i]);

        auto sr = cupread::loadSprites(bundles / "atlas_slimelevel", prefixes, m_frames);
        auto cr = cupread::loadClips(data, bundles / "music_mus_slime", clips, m_clips);
        if (!sr.ok) {
            m_error = "Couldn't read Goopy's art from Cuphead: " + sr.error;
            m_state = State::Failed;
            log::error("{}", m_error);
            queueInMainThread([err = sr.error] { test::report("cuphead-found", false, err); });
            return;
        }
        if (!cr.ok || !cr.error.empty()) log::warn("Goopy sounds: {}", cr.error);
        log::info("Loaded {} Goopy frames and {} sounds from Cuphead", m_frames.size(), m_clips.size());
        m_state = State::Ready;
        queueInMainThread([n = m_frames.size()] { test::report("cuphead-found", n >= 90, "too few Goopy frames"); });
    }).detach();
}

std::vector<FrameTex> const& Assets::anim(std::string_view id) {
    static std::vector<FrameTex> const empty;
    if (!ready()) return empty;
    if (auto it = m_anims.find(id); it != m_anims.end()) return it->second;
    std::vector<FrameTex> frames;
    for (auto const& a : sheets::kAnims) {
        if (a.id != id) continue;
        for (int i = 1; i <= a.frames; ++i) {
            auto name = fmt::format("{}_{:04}", a.prefix, i);
            auto f = m_frames.find(name);
            if (f == m_frames.end()) continue;
            auto* tex = new CCTexture2D();
            if (!tex->initWithData(f->second.rgba.data(), kCCTexture2DPixelFormat_RGBA8888, f->second.width,
                                   f->second.height, CCSize(f->second.width, f->second.height))) {
                tex->release();
                continue;
            }
            tex->setAntiAliasTexParameters();
            frames.push_back({tex, f->second.centerX, f->second.centerY, float(f->second.width), float(f->second.height)});
            // the pixels now live on the GPU
            std::vector<uint8_t>().swap(f->second.rgba);
        }
    }
    return m_anims[std::string(id)] = std::move(frames);
}

FMOD::Sound* Assets::sound(std::string const& clip) {
    if (auto it = m_subSounds.find(clip); it != m_subSounds.end()) return it->second;
    FMOD::Sound* sub = nullptr;
    auto data = m_clips.find(clip);
    auto* engine = FMODAudioEngine::sharedEngine();
    if (data != m_clips.end() && engine && engine->m_system) {
        FMOD_CREATESOUNDEXINFO info{};
        info.cbsize = sizeof(info);
        info.length = static_cast<unsigned>(data->second.size());
        FMOD::Sound* snd = nullptr;
        auto r = engine->m_system->createSound(reinterpret_cast<char const*>(data->second.data()),
                                               FMOD_OPENMEMORY | FMOD_CREATESAMPLE, &info, &snd);
        if (r == FMOD_OK && snd) {
            int count = 0;
            snd->getNumSubSounds(&count);
            if (count > 0) snd->getSubSound(0, &sub);
            else sub = snd;
            m_sounds[clip] = snd;
        } else {
            log::warn("FMOD couldn't open {} ({})", clip, int(r));
        }
    }
    return m_subSounds[clip] = sub;
}

void Assets::play(std::string_view soundId) {
    if (!ready() || !Mod::get()->getSettingValue<bool>("goopy-sounds")) return;
    for (auto const& s : sheets::kSounds) {
        if (s.id != soundId || s.clipCount == 0) continue;
        m_rng = m_rng * 1103515245u + 12345u;
        auto clip = std::string(s.clips[(m_rng >> 16) % s.clipCount]);
        auto* snd = sound(clip);
        auto* engine = FMODAudioEngine::sharedEngine();
        if (!snd || !engine || !engine->m_system) return;
        FMOD::Channel* ch = nullptr;
        if (engine->m_system->playSound(snd, engine->m_globalChannel, true, &ch) == FMOD_OK && ch) {
            ch->setVolume(s.volume * engine->m_sfxVolume);
            ch->setPaused(false);
            test::report("goopy-sounds", true);
        }
        return;
    }
}

} // namespace goopy

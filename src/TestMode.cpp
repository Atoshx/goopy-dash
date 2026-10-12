#include "TestMode.hpp"

#include <Geode/Geode.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace goopy::test {
namespace {
struct State {
    bool loaded = false;
    bool active = false;
    fs::path checks;
    std::set<std::string> reported;
};
State& state() {
    static State s;
    if (!s.loaded) {
        s.loaded = true;
#ifdef GEODE_IS_WINDOWS
        wchar_t const* lad = _wgetenv(L"LOCALAPPDATA");
        if (!lad) return s;
        fs::path cfg = fs::path(lad) / "Melty" / "test" / "goopy-dash.json";
        std::error_code ec;
        if (!fs::exists(cfg, ec)) return s;
        std::ifstream f(cfg);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto parsed = matjson::parse(text);
        if (parsed.isErr()) return s;
        auto value = parsed.unwrap();
        auto checks = value["checks"].asString();
        if (checks.isErr()) return s;
        auto path = checks.unwrap();
        s.checks = fs::path(utils::string::utf8ToWide(path));
        s.active = true;
        log::info("Melty test run: reporting checks to {}", path);
#endif
    }
    return s;
}
} // namespace

bool active() { return state().active; }

void report(std::string_view check, bool pass, std::string_view why) {
    auto& s = state();
    if (!s.active) return;
    std::string name(check);
    if (s.reported.contains(name)) return;
    s.reported.insert(name);
    std::ofstream out(s.checks, std::ios::app);
    out << (pass ? "pass " : "fail ") << name;
    if (!pass && !why.empty()) out << ": " << why;
    out << "\n";
    log::info("test check {} {}", pass ? "pass" : "fail", name);
}
} // namespace goopy::test

namespace goopy::test {
void capture(std::string_view name) {
    static std::set<std::string> done;
    if (!active() || done.contains(std::string(name))) return;
    done.insert(std::string(name));
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    auto size = CCDirector::get()->getWinSize();
    auto* rt = CCRenderTexture::create(int(size.width), int(size.height));
    if (!rt) return;
    rt->begin();
    scene->visit();
    rt->end();
    auto dir = Mod::get()->getConfigDir() / "shots";
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto path = dir / (std::string(name) + ".png");
    if (auto* img = rt->newCCImage(true)) {
        bool ok = img->saveToFile(utils::string::pathToString(path).c_str(), false);
        img->release();
        log::info("test screenshot {} saved={}", utils::string::pathToString(path), ok);
    }
}
}

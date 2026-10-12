// Goopy Dash: Goopy Le Grande, read from the player's own Cuphead install,
// chases you through Geometry Dash levels.
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Assets.hpp"
#include "Goopy.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    goopy::Assets::get().startLoading();
}

class $modify(GoopyPlayLayer, PlayLayer) {
    struct Fields {
        goopy::Goopy* goopy = nullptr;
        bool warned = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        this->spawnGoopy();
        return true;
    }

    void spawnGoopy() {
        auto& assets = goopy::Assets::get();
        if (!Mod::get()->getSettingValue<bool>("enabled") || m_isPlatformer) return;
        if (assets.failed()) {
            if (!m_fields->warned) {
                m_fields->warned = true;
                Notification::create(assets.error(), NotificationIcon::Warning, 4.f)->show();
            }
            return;
        }
        if (!assets.ready() || m_fields->goopy) return;
        m_fields->goopy = goopy::Goopy::create(this);
        if (m_fields->goopy) m_objectLayer->addChild(m_fields->goopy, 500);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (m_fields->goopy) m_fields->goopy->reset();
    }

    void levelComplete() {
        if (m_fields->goopy) m_fields->goopy->defeat();
        PlayLayer::levelComplete();
    }

    void tickGoopy(float dt) {
        // Cuphead may still be loading when the level opens.
        if (!m_fields->goopy) this->spawnGoopy();
        if (m_fields->goopy) m_fields->goopy->tick(dt);
    }
};

class $modify(GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);
        if (auto* pl = typeinfo_cast<PlayLayer*>(this)) static_cast<GoopyPlayLayer*>(pl)->tickGoopy(dt);
    }
};

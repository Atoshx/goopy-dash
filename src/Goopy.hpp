#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <vector>

#include "Assets.hpp"

namespace goopy {

// A sprite that plays one of Goopy's animations frame by frame.
class AnimSprite : public cocos2d::CCSprite {
public:
    static AnimSprite* create();
    void play(std::string_view anim, bool restart = true);
    bool step(float dt);              // returns true when a non-looping anim has finished
    int frameIndex() const { return m_frame; }
    int frameCount() const { return m_frames ? int(m_frames->size()) : 0; }
    FrameTex const* frame() const;
    std::string_view current() const { return m_anim; }
    float scaleFactor = 1.f;

private:
    std::vector<FrameTex> const* m_frames = nullptr;
    std::string m_anim;
    float m_fps = 24.f, m_time = 0.f;
    bool m_loop = true;
    int m_frame = 0;
    void applyFrame();
};

struct Hazard {
    AnimSprite* sprite = nullptr;
    float x = 0, y = 0;   // world (object layer) position of the hazard's bottom centre
    float vx = 0;         // world units per second
    float width = 0, height = 0;
    float life = 0;
};

class Goopy : public cocos2d::CCNode {
public:
    static Goopy* create(PlayLayer* layer);
    void tick(float dt);
    void reset();
    void defeat();

private:
    enum class State { Waiting, Entering, Idle, SlamWindup, SlamAir, PunchWindup, Punching, Recover, Defeated };

    PlayLayer* m_layer = nullptr;
    AnimSprite* m_body = nullptr;
    State m_state = State::Waiting;
    float m_stateTime = 0;
    float m_relX = 0;        // Goopy's x relative to the player
    float m_targetRelX = 0;
    float m_hop = 0;         // height above the ground
    float m_hopVel = 0;
    float m_groundY = 90;    // where Goopy (and his attacks) stand
    float m_untilAttack = 0;
    float m_footOffset = 0;  // source pixels from pivot down to Goopy's feet
    float m_punchTipOffset = 0;
    unsigned m_rng = 1;
    std::vector<Hazard> m_hazards;

    bool init(PlayLayer* layer);
    void setState(State s);
    float playerX() const;
    float rand01();
    void chooseAttack();
    void spawnDust();
    void updateHazards(float dt);
    bool hitsPlayer(cocos2d::CCRect const& r) const;
    void killPlayer();
    cocos2d::CCRect gloveRect() const;
};

} // namespace goopy

#include "Goopy.hpp"
#include "generated/Sheets.hpp"
#include "TestMode.hpp"

#include <Geode/binding/GJGameLevel.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace goopy {

namespace {
sheets::Anim const* findAnim(std::string_view id) {
    for (auto const& a : sheets::kAnims) if (a.id == id) return &a;
    return nullptr;
}
sheets::Attack const* findAttack(std::string_view id) {
    for (auto const& a : sheets::kAttacks) if (a.id == id) return &a;
    return nullptr;
}
} // namespace

// ---------------------------------------------------------------- AnimSprite

AnimSprite* AnimSprite::create() {
    auto* s = new AnimSprite();
    if (s->init()) { s->autorelease(); return s; }
    delete s;
    return nullptr;
}

void AnimSprite::play(std::string_view anim, bool restart) {
    if (!restart && m_anim == anim) return;
    auto const* a = findAnim(anim);
    m_anim = std::string(anim);
    m_frames = &Assets::get().anim(anim);
    m_fps = a ? a->fps : 24.f;
    m_loop = a ? a->loop : true;
    m_time = 0;
    m_frame = 0;
    applyFrame();
}

bool AnimSprite::step(float dt) {
    if (!m_frames || m_frames->empty()) return true;
    m_time += dt;
    int f = static_cast<int>(m_time * m_fps);
    int n = static_cast<int>(m_frames->size());
    bool done = false;
    if (m_loop) f %= n;
    else if (f >= n) { f = n - 1; done = true; }
    if (f != m_frame) { m_frame = f; applyFrame(); }
    return done;
}

FrameTex const* AnimSprite::frame() const {
    if (!m_frames || m_frame >= static_cast<int>(m_frames->size())) return nullptr;
    return &(*m_frames)[m_frame];
}

void AnimSprite::applyFrame() {
    auto const* f = frame();
    if (!f) { setVisible(false); return; }
    setVisible(true);
    setTexture(f->texture);
    setTextureRect(CCRect(0, 0, f->width, f->height));
    setScale(scaleFactor);
    setPosition({f->centerX * scaleFactor, f->centerY * scaleFactor});
}

// ---------------------------------------------------------------- Goopy

Goopy* Goopy::create(PlayLayer* layer) {
    auto* g = new Goopy();
    if (g->init(layer)) { g->autorelease(); return g; }
    delete g;
    return nullptr;
}

bool Goopy::init(PlayLayer* layer) {
    if (!CCNode::init()) return false;
    m_layer = layer;
    m_body = AnimSprite::create();
    m_body->scaleFactor = sheets::kScale;
    addChild(m_body);
    m_footOffset = sheets::kFootPx;
    // How far left Goopy's glove reaches on the stretched punch frames.
    auto const& punch = Assets::get().anim("punch");
    float tip = 0;
    for (int i = int(sheets::kPunchActiveFrom) - 1; i < int(sheets::kPunchActiveTo) && i < int(punch.size()); ++i)
        tip = std::min(tip, punch[i].centerX - punch[i].width / 2);
    m_punchTipOffset = tip;
    m_rng = static_cast<unsigned>(reinterpret_cast<uintptr_t>(layer)) | 1u;
    reset();
    return true;
}

float Goopy::rand01() {
    m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
    return (m_rng & 0xffffff) / float(0x1000000);
}

float Goopy::playerX() const { return m_layer->m_player1->getPositionX(); }

void Goopy::reset() {
    for (auto& h : m_hazards) if (h.sprite) h.sprite->getParent()->removeFromParent();
    m_hazards.clear();
    m_relX = sheets::kEnterFromX;
    m_targetRelX = sheets::kOffsetX;
    m_hop = 0;
    m_hopVel = 0;
    if (auto* p = m_layer->m_player1) m_groundY = p->getObjectRect().getMinY();
    m_untilAttack = Mod::get()->getSettingValue<double>("attack-interval");
    setVisible(false);
    setState(State::Waiting);
}

void Goopy::defeat() {
    if (m_state == State::Defeated || m_state == State::Waiting) return;
    setState(State::Defeated);
    Assets::get().play("death");
    Assets::get().play("deathVoice");
}

void Goopy::setState(State s) {
    m_state = s;
    m_stateTime = 0;
    switch (s) {
        case State::Waiting: break;
        case State::Entering:
            setVisible(true);
            test::report("goopy-appears", m_body->frameCount() > 0, "no animation frames");
            m_hopVel = sheets::kSlamJumpVel;
            m_body->play("airUp");
            Assets::get().play("bigJump");
            break;
        case State::Idle: m_body->play("idle"); break;
        case State::SlamWindup: m_body->play("jump"); break;
        case State::SlamAir:
            m_hopVel = sheets::kSlamJumpVel;
            m_body->play("airUp");
            Assets::get().play("bigJump");
            break;
        case State::PunchWindup: {
            m_body->play("punch");
            // Plant so the stretched glove ends just past the player's centre.
            m_targetRelX = std::max(80.f, sheets::kPunchTipRelX - m_punchTipOffset * sheets::kScale);
            Assets::get().play("punchVoice");
            break;
        }
        case State::Punching:
            Assets::get().play("punch");
            test::report("goopy-punch", true);
            break;
        case State::Recover: m_targetRelX = sheets::kOffsetX; m_body->play("idle"); break;
        case State::Defeated:
            m_body->play("death");
            test::report("goopy-defeated", m_body->frameCount() > 0, "no death frames");
            break;
    }
}

void Goopy::chooseAttack() {
    int total = 0;
    for (auto const& a : sheets::kAttacks) total += a.weight;
    float r = rand01() * total;
    for (auto const& a : sheets::kAttacks) {
        r -= a.weight;
        if (r <= 0) {
            setState(a.id == "punch" ? State::PunchWindup : State::SlamWindup);
            return;
        }
    }
    setState(State::SlamWindup);
}

CCRect Goopy::gloveRect() {
    // Only while the punch is stretched out.
    if (m_state != State::Punching || m_body->current() != "punch") return CCRect(0, 0, 0, 0);
    int f = m_body->frameIndex() + 1;
    if (f < sheets::kPunchActiveFrom || f > sheets::kPunchActiveTo) return CCRect(0, 0, 0, 0);
    auto const* fr = m_body->frame();
    if (!fr) return CCRect(0, 0, 0, 0);
    float s = sheets::kScale;
    float pivotX = getPositionX(), pivotY = getPositionY();
    float tip = pivotX + (fr->centerX - fr->width / 2) * s;
    return CCRect(tip, pivotY + sheets::kGloveBottomPx * s, sheets::kGloveLengthPx * s,
                  (sheets::kGloveTopPx - sheets::kGloveBottomPx) * s);
}

void Goopy::spawnDust() {
    auto const* atk = findAttack("slam");
    if (!atk || !atk->hazardAnim) return;
    auto* holder = CCNode::create();
    auto* sprite = AnimSprite::create();
    sprite->scaleFactor = atk->hazardScale;
    holder->addChild(sprite);
    sprite->play(atk->hazardAnim);
    m_layer->m_objectLayer->addChild(holder, 499);
    Hazard h;
    h.sprite = sprite;
    h.x = getPositionX() - 30.f;
    h.y = m_groundY;
    h.vx = -atk->hazardSpeed;
    h.width = atk->hazardWidth;
    h.height = atk->hazardHeight;
    holder->setPosition({h.x, h.y + atk->hazardFootPx * atk->hazardScale});
    m_hazards.push_back(h);
    test::report("goopy-slam", sprite->frameCount() > 0, "no dust frames");
}

bool Goopy::hitsPlayer(CCRect const& r) const {
    if (r.size.width <= 0 || !Mod::get()->getSettingValue<bool>("goopy-hurts")) return false;
    auto* p = m_layer->m_player1;
    return p && !p->m_isDead && p->getObjectRect().intersectsRect(r);
}

void Goopy::killPlayer() {
    auto* p = m_layer->m_player1;
    if (!p || p->m_isDead) return;
    test::report("goopy-hits", true);
    m_layer->destroyPlayer(p, nullptr);
}

void Goopy::updateHazards(float dt) {
    float px = playerX();
    auto const* atk = findAttack("slam");
    for (auto& h : m_hazards) {
        h.life += dt;
        h.x += h.vx * dt;
        h.sprite->step(dt);
        h.sprite->getParent()->setPosition({h.x, h.y + (atk ? atk->hazardFootPx * atk->hazardScale : 0)});
        if (hitsPlayer(CCRect(h.x - h.width / 2, h.y, h.width, h.height))) killPlayer();
    }
    auto gone = [&](Hazard const& h) { return h.x < px - 250 || h.life > 6; };
    for (auto& h : m_hazards) if (gone(h)) h.sprite->getParent()->removeFromParent();
    m_hazards.erase(std::remove_if(m_hazards.begin(), m_hazards.end(), gone), m_hazards.end());
}

void Goopy::tick(float dt) {
    if (!Assets::get().ready()) return;
    auto* p = m_layer->m_player1;
    if (!p) return;
    if (p->m_isDead) return; // freeze while GD plays the death effect

    // Follow the ground the player is running on.
    if (p->m_isOnGround && !p->m_isUpsideDown) {
        float target = p->getObjectRect().getMinY();
        m_groundY += (target - m_groundY) * std::min(1.f, dt * 8.f);
    }

    m_stateTime += dt;
    bool animDone = m_body->step(dt);
    float interval = static_cast<float>(Mod::get()->getSettingValue<double>("attack-interval"));

    switch (m_state) {
        case State::Waiting:
            if (m_stateTime >= sheets::kEnterDelay) setState(State::Entering);
            break;
        case State::Entering:
        case State::SlamAir:
            m_hopVel -= sheets::kGravity * dt;
            m_hop += m_hopVel * dt;
            if (m_hopVel < 0 && m_body->current() == "airUp") m_body->play("upDown");
            if (m_body->current() == "upDown" && animDone) m_body->play("airDown");
            if (m_hop <= 0) {
                m_hop = 0;
                Assets::get().play("bigLand");
                if (m_state == State::SlamAir) spawnDust();
                m_untilAttack = interval;
                setState(State::Idle);
            }
            break;
        case State::Idle:
            m_untilAttack -= dt;
            if (m_untilAttack <= 0 && !p->m_isUpsideDown) chooseAttack();
            break;
        case State::SlamWindup:
            if (animDone) setState(State::SlamAir);
            break;
        case State::PunchWindup:
            if (m_body->frameIndex() + 1 >= sheets::kPunchActiveFrom - 1) setState(State::Punching);
            break;
        case State::Punching:
            if (hitsPlayer(gloveRect())) killPlayer();
            if (animDone) setState(State::Recover);
            break;
        case State::Recover:
            if (std::fabs(m_relX - m_targetRelX) < 4.f) {
                m_untilAttack = interval;
                setState(State::Idle);
            }
            break;
        case State::Defeated:
            break;
    }

    if (m_state != State::Defeated) {
        // Glide toward the planted distance (punch) or back to cruising distance.
        float speed = m_state == State::Entering ? 260.f : 420.f;
        float d = m_targetRelX - m_relX;
        m_relX += std::clamp(d, -speed * dt, speed * dt);
        setPosition({playerX() + m_relX, m_groundY + m_footOffset * sheets::kScale + m_hop});
    }
    if (m_state != State::Waiting) updateHazards(dt);
}

} // namespace goopy

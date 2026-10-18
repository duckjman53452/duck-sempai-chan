#pragma once

#include <Geode/Geode.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace buffer {

// ---------------------------------------------------------------- player helpers

enum class Mode : uint8_t { Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing };

inline Mode modeOf(PlayerObject* p) {
    if (p->m_isDart) return Mode::Wave;
    if (p->m_isShip) return Mode::Ship;
    if (p->m_isSwing) return Mode::Swing;
    if (p->m_isBird) return Mode::Ufo;
    if (p->m_isBall) return Mode::Ball;
    if (p->m_isRobot) return Mode::Robot;
    if (p->m_isSpider) return Mode::Spider;
    return Mode::Cube;
}

inline char const* modeName(Mode m) {
    switch (m) {
        case Mode::Wave: return "wave";
        case Mode::Ship: return "ship";
        case Mode::Swing: return "swing";
        case Mode::Ufo: return "ufo";
        case Mode::Ball: return "ball";
        case Mode::Robot: return "robot";
        case Mode::Spider: return "spider";
        default: return "cube";
    }
}

// Ship, wave and swing: the button steers continuously.
inline bool isFlightMode(PlayerObject* p) {
    return p->m_isShip || p->m_isDart || p->m_isSwing;
}

// ---------------------------------------------------------------- simulator

// Live: short look-ahead during play. Anything we can't model (orbs, pads,
// portals, triggers) aborts the run so it never touches the real game.
// Scan: the full-level shadow run at level open. Portals, orbs and pads run
// for real on the clone; the level is reset afterwards.
enum class SimMode { Live, Scan };

struct SimResult {
    int beforeSurvived = 0;   // steps survived while still in the old input state
    int afterSurvived = 0;    // steps survived after switching to the new input state
    bool diedBefore = false;  // crashed before the switch even happened
    bool aborted = false;     // hit something the live simulator doesn't predict
};

struct SimCounters {
    int runs = 0;
    int crashes = 0;
    int unknown = 0;
};

// Steps hidden clone players with GD's own physics
// (PlayerObject::update + GJBaseGameLayer::checkCollisions). State is copied
// with the same save/load-checkpoint calls practice mode uses. SimHooks.cpp
// keeps the clones from touching real game state.
class Predictor {
public:
    static Predictor& get();

    void attach(GJBaseGameLayer* layer);  // idempotent per layer
    void detach();
    bool ready(GJBaseGameLayer* layer) const;

    void noteRealStep(float dt) { m_dt = dt; }
    float stepDt() const { return m_dt; }

    bool simulating() const { return m_simulating; }
    SimMode mode() const { return m_mode; }
    bool isFake(PlayerObject* p) const;
    void reportDeath(PlayerObject* p);
    void reportAbort() { m_aborted = true; }

    // ---- live look-ahead
    // Hold `before` for `delay` steps, then switch to `after` for up to `maxAfter` steps.
    // `trace` (optional) receives clone 0's position after every step.
    SimResult run(
        GJBaseGameLayer* layer,
        std::array<PlayerObject*, 2> const& real,
        bool before, bool after,
        int delay, int maxAfter,
        std::vector<cocos2d::CCPoint>* trace = nullptr
    );

    // ---- low-level API for the level scanner (TimingMap)
    void beginScan();
    void endScan();
    PlayerObject* clone(size_t i) const { return m_fakes[i]; }
    void loadState(PlayerObject* fake, PlayerCheckpoint* cp, bool held);
    void setHeldRaw(PlayerObject* fake, bool held);   // without click side effects
    void press(PlayerObject* fake, bool down);        // a real click / release
    bool stepClone(GJBaseGameLayer* layer, PlayerObject* fake);
    void clearFlags() { m_died = false; m_aborted = false; }
    bool died() const { return m_died; }
    bool aborted() const { return m_aborted; }

    SimCounters const& counters() const { return m_counters; }
    void resetCounters() { m_counters = {}; }

private:
    Predictor() = default;

    void syncFromReal(PlayerObject* fake, PlayerObject* real);

    GJBaseGameLayer* m_layer = nullptr;
    std::array<PlayerObject*, 2> m_fakes{};  // children of m_layer's object layer
    std::array<bool, 2> m_active{};
    PlayerCheckpoint* m_cp = nullptr;        // reusable scratch checkpoint (retained)
    float m_dt = .25f;                       // replaced by the real step delta once the level runs
    bool m_simulating = false;
    SimMode m_mode = SimMode::Live;
    bool m_died = false;
    bool m_aborted = false;
    SimCounters m_counters{};
};

} // namespace buffer

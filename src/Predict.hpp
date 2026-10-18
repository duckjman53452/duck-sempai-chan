#pragma once

#include <Geode/Geode.hpp>

#include <array>

namespace buffer {

// Outcome of one look-ahead run.
struct SimResult {
    int beforeSurvived = 0;   // steps survived while still in the old input state
    int afterSurvived = 0;    // steps survived after switching to the new input state
    bool diedBefore = false;  // crashed before the switch even happened
    bool aborted = false;     // hit something the simulator can't predict (orb, pad, portal...)
};

// Runs GD's own player physics on hidden clone players so we can ask
// "if the button changes N ticks from now, does the icon survive?".
//
// The clones step with PlayerObject::update + GJBaseGameLayer::checkCollisions,
// exactly like the real players, against the level as it is right now.
// While a run is in progress, hooks in SimHooks.cpp stop the clones from
// touching game state (coins, triggers, checkpoints, deaths).
class Predictor {
public:
    static Predictor& get();

    void attach(GJBaseGameLayer* layer);  // call once per PlayLayer
    void detach();
    bool ready(GJBaseGameLayer* layer) const;

    void noteRealStep(float dt) { m_dt = dt; }

    bool simulating() const { return m_simulating; }
    bool isFake(PlayerObject* p) const;
    void reportDeath(PlayerObject* p);
    void reportAbort() { m_aborted = true; }

    // Hold `before` for `delay` steps, then switch to `after` for up to `maxAfter` steps.
    // `real` lists the players one input side drives (second entry may be null).
    SimResult run(
        GJBaseGameLayer* layer,
        std::array<PlayerObject*, 2> const& real,
        bool before, bool after,
        int delay, int maxAfter
    );

private:
    Predictor() = default;

    void sync(PlayerObject* fake, PlayerObject* real);
    bool step(GJBaseGameLayer* layer, int count);
    void setHeld(PlayerObject* fake, bool held);

    GJBaseGameLayer* m_layer = nullptr;
    std::array<PlayerObject*, 2> m_fakes{};  // children of m_layer's object layer
    std::array<bool, 2> m_active{};
    float m_dt = .25f;  // replaced by the real step delta as soon as the level runs
    bool m_simulating = false;
    bool m_died = false;
    bool m_aborted = false;
};

} // namespace buffer

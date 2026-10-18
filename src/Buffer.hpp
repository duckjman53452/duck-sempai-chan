#pragma once

#include <Geode/Geode.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace buffer {

enum class Stage : int {
    Off = 0,
    Subtle = 1,    // 1-2 ticks
    Moderate = 2,  // ~4 ticks
    Showcase = 3,  // 8-16 ticks
};

// What a player looked like when the button went down. Any of these changing
// afterwards means the press has turned into an action (jump, orb, flip, dash).
struct PlayerSnap {
    double lastJumpTime = 0.0;
    double lastFlipTime = 0.0;
    double prevYVelocity = 0.0;  // refreshed every tick, used for impulse detection
    bool upsideDown = false;
    bool dashing = false;
};

// Discrete modes (cube, ball, UFO, robot, spider): an early release is held
// until the click has been used. One slot per input side (0 = P1, 1 = P2).
struct InputSlot {
    bool tracking = false;   // a press is live and hasn't been resolved yet
    bool consumed = false;   // the press produced an action
    bool deferred = false;   // finger is up early; we're holding the release for N ticks
    int button = 1;
    Stage stage = Stage::Off;  // stage at the moment the release was deferred
    uint64_t pressTick = 0;
    uint64_t releaseTick = 0;
    uint64_t consumedTick = 0;
    std::array<PlayerSnap, 2> snap{};
};

// Flight modes (ship, wave, swing): a press or release that would crash within
// the reaction margin is delayed to the nearest tick that keeps the icon alive.
struct PendingFlight {
    bool active = false;
    bool down = false;
    int button = 1;
    int delay = 0;          // ticks the input is being held back
    int wouldDieIn = 0;     // ticks to the crash had it been sent on time
    uint64_t applyTick = 0;
    Stage stage = Stage::Off;
    char const* mode = "";
};

struct Stats {
    int savesAttempt = 0;
    int savesSession = 0;
    int bestSave = 0;
    int lastSave = 0;
    int unusedAttempt = 0;
};

class Engine {
public:
    static Engine& get();

    // --- stage control ---
    Stage stage() const { return m_stage; }
    void setStage(Stage stage);
    void cycleStage();

    int windowTicks() const { return windowFor(m_stage); }
    static int windowFor(Stage stage);
    static std::string stageName(Stage stage);   // "Stage 2"
    static std::string stageChip(Stage stage);   // "S2 / 4t"
    static cocos2d::ccColor3B stageColor(Stage stage);

    // --- called from hooks ---
    // True if this button event should go through the buffer at all.
    bool handles(GJBaseGameLayer* layer, int button) const;
    // Returns true if the original handleButton should run now, false if swallowed.
    bool onButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1);
    // Runs once per physics step, before the game processes that step's inputs.
    void onTick(GJBaseGameLayer* layer, bool countTick);
    // New attempt (level start / respawn). Flushes any held input first.
    void onAttemptStart(GJBaseGameLayer* layer);
    // Leaving the level: drop everything, the layer is going away.
    void onLevelExit();

    uint64_t tick() const { return m_tick; }
    Stats const& stats() const { return m_stats; }

private:
    Engine();

    // discrete
    bool onDiscreteButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1);
    void dispatchRelease(GJBaseGameLayer* layer, InputSlot& slot, bool isPlayer1);

    // flight
    bool onFlightButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1);
    void dispatchFlight(GJBaseGameLayer* layer, size_t side, bool applyPress = true);

    void recordSave(Stage stage, int saved, uint64_t atTick, bool isPlayer1);
    void recordFlightSave(PendingFlight const& pend, uint64_t atTick, bool isPlayer1);
    void recordUnused(Stage stage, int waited, bool isPlayer1);
    void bumpStats(int saved);

    Stage m_stage = Stage::Off;
    uint64_t m_tick = 0;
    bool m_dispatching = false;
    std::array<InputSlot, 2> m_slots{};
    std::array<PendingFlight, 2> m_pending{};
    std::array<bool, 2> m_gameHeld{};  // button state the game has actually been given
    Stats m_stats{};
};

} // namespace buffer

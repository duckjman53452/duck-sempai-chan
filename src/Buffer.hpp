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

// Cube-style modes without map coverage: an early release is held until the
// click has been used. One slot per input side (0 = P1, 1 = P2).
struct InputSlot {
    bool tracking = false;
    bool consumed = false;
    bool deferred = false;
    int button = 1;
    Stage stage = Stage::Off;
    uint64_t pressTick = 0;
    uint64_t releaseTick = 0;
    uint64_t consumedTick = 0;
    cocos2d::CCPoint releasePos{};
    std::array<PlayerSnap, 2> snap{};
};

enum class ShiftSource { Map, Lookahead };

// A click or release that's being held back a few ticks (map or look-ahead).
struct PendingShift {
    bool active = false;
    bool down = false;
    int button = 1;
    int delay = 0;
    int wouldDieIn = 0;  // look-ahead only: ticks to the crash had it been sent on time
    uint64_t applyTick = 0;
    Stage stage = Stage::Off;
    ShiftSource source = ShiftSource::Map;
    char const* mode = "";
    cocos2d::CCPoint inputPos{};
};

struct Stats {
    int savesAttempt = 0;
    int savesSession = 0;
    int bestSave = 0;
    int lastSave = 0;
    int unusedAttempt = 0;
};

// Counters for the diagnostics line. If the buffer "does nothing", these show
// which link of the chain isn't firing.
struct Diag {
    int inputs = 0;        // jump presses/releases that reached the hook
    int mapMatches = 0;    // inputs where the player was on the shadow route
    int mapShifts = 0;
    int lookShifts = 0;
    int holdSaves = 0;
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
    static std::string stageName(Stage stage);
    static std::string stageChip(Stage stage);
    static cocos2d::ccColor3B stageColor(Stage stage);

    // --- called from hooks ---
    bool handles(GJBaseGameLayer* layer, int button) const;
    // Returns true if the original handleButton should run now, false if held back.
    bool onButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1);
    void onTick(GJBaseGameLayer* layer, bool countTick);
    void onAttemptStart(GJBaseGameLayer* layer);
    void onLevelExit();

    uint64_t tick() const { return m_tick; }
    Stats const& stats() const { return m_stats; }
    Diag const& diag() const { return m_diag; }

private:
    Engine();

    struct MapAdvice {
        bool onRoute = false;  // player matches the shadow route here
        int delay = 0;         // > 0: move the input this many ticks later
    };
    MapAdvice adviseFromMap(GJBaseGameLayer* layer, PlayerObject* p, bool before, bool down) const;

    bool onFlightButton(GJBaseGameLayer* layer, bool before, bool down, int button, bool isPlayer1);
    bool onDiscreteButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1);

    void hold(GJBaseGameLayer* layer, size_t side, PendingShift shift);
    void dispatchPending(GJBaseGameLayer* layer, size_t side, bool applyPress = true);
    void dispatchRelease(GJBaseGameLayer* layer, InputSlot& slot, bool isPlayer1);

    void recordShift(GJBaseGameLayer* layer, PendingShift const& shift, uint64_t atTick, bool isPlayer1);
    void recordHoldSave(GJBaseGameLayer* layer, InputSlot const& slot, int saved, uint64_t atTick, bool isPlayer1);
    void recordUnused(Stage stage, int waited, bool isPlayer1);
    void bumpStats(int saved);

    Stage m_stage = Stage::Off;
    uint64_t m_tick = 0;
    bool m_dispatching = false;
    std::array<InputSlot, 2> m_slots{};
    std::array<PendingShift, 2> m_pending{};
    std::array<bool, 2> m_gameHeld{};  // button state the game has actually been given
    Stats m_stats{};
    Diag m_diag{};
};

} // namespace buffer

#include "Buffer.hpp"
#include "Overlay.hpp"
#include "Predict.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace buffer {

namespace {

// A single physics tick can't add this much upward speed through gravity alone,
// so a jump this big between two ticks means an action fired (jump, orb, pad).
constexpr double kImpulse = 4.0;
constexpr double kMinActionSpeed = 3.0;

bool isPlayLayer(GJBaseGameLayer* layer) {
    auto pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer*>(pl) == layer;
}

// Which players a given input side drives.
// 2-player levels: each side drives its own icon. Otherwise one button drives
// player 1, plus player 2 while a dual portal is active.
std::array<PlayerObject*, 2> playersFor(GJBaseGameLayer* layer, bool isPlayer1) {
    bool twoPlayer = layer->m_levelSettings && layer->m_levelSettings->m_twoPlayerMode;
    if (twoPlayer) {
        return { isPlayer1 ? layer->m_player1 : layer->m_player2, nullptr };
    }
    return { layer->m_player1, layer->m_gameState.m_isDualMode ? layer->m_player2 : nullptr };
}

// Ship, wave and swing: the button steers continuously, so instead of waiting
// for a "valid frame" the flight path is simulated (see onFlightButton).
bool isFlightMode(PlayerObject* p) {
    return p->m_isShip || p->m_isDart || p->m_isSwing;
}

bool anyFlight(std::array<PlayerObject*, 2> const& players) {
    for (auto p : players) {
        if (p && isFlightMode(p)) return true;
    }
    return false;
}

bool anyDead(std::array<PlayerObject*, 2> const& players) {
    for (auto p : players) {
        if (p && p->m_isDead) return true;
    }
    return false;
}

char const* modeName(PlayerObject* p) {
    if (!p) return "";
    if (p->m_isDart) return "wave";
    if (p->m_isShip) return "ship";
    if (p->m_isSwing) return "swing";
    if (p->m_isBird) return "ufo";
    if (p->m_isBall) return "ball";
    if (p->m_isRobot) return "robot";
    if (p->m_isSpider) return "spider";
    return "cube";
}

char const* flightModeName(std::array<PlayerObject*, 2> const& players) {
    for (auto p : players) {
        if (p && isFlightMode(p)) return modeName(p);
    }
    return modeName(players[0]);
}

void snapshot(PlayerObject* p, PlayerSnap& snap) {
    snap.lastJumpTime = p->m_lastJumpTime;
    snap.lastFlipTime = p->m_lastFlipTime;
    snap.prevYVelocity = p->m_yVelocity;
    snap.upsideDown = p->m_isUpsideDown;
    snap.dashing = p->m_isDashing;
}

// Did this player do something since the press? Checked once per tick against
// the snapshot; prevYVelocity is refreshed by the caller after each check.
bool actionHappened(PlayerObject* p, PlayerSnap const& snap) {
    if (p->m_lastJumpTime != snap.lastJumpTime) return true;   // jump / ring jump
    if (p->m_lastFlipTime != snap.lastFlipTime) return true;   // ball / spider flip
    if (p->m_isUpsideDown != snap.upsideDown) return true;     // blue / green orb
    if (p->m_isDashing && !snap.dashing) return true;          // dash orb

    // Fallback for anything that doesn't touch the timestamps above.
    // m_yVelocity is gravity-relative in GD, so "up" is always positive.
    double v = p->m_yVelocity;
    double dv = v - snap.prevYVelocity;
    if (dv > kImpulse && v > kMinActionSpeed) return true;                         // upward launch
    if (-dv > kImpulse && v < -kMinActionSpeed && !p->m_isOnGround) return true;   // black orb
    return false;
}

bool consoleEnabled() {
    return Mod::get()->getSettingValue<bool>("console-log");
}

int intSetting(char const* key, int lo, int hi) {
    return static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>(key), lo, hi));
}

} // namespace

Engine& Engine::get() {
    static Engine instance;
    return instance;
}

Engine::Engine() {
    if (Mod::get()->getSettingValue<bool>("remember-stage")) {
        auto saved = Mod::get()->getSavedValue<int64_t>("stage", 0);
        m_stage = static_cast<Stage>(std::clamp<int64_t>(saved, 0, 3));
    }
}

// ---------------------------------------------------------------- stages

int Engine::windowFor(Stage stage) {
    switch (stage) {
        case Stage::Subtle: return intSetting("stage1-ticks", 1, 2);
        case Stage::Moderate: return intSetting("stage2-ticks", 3, 6);
        case Stage::Showcase: return intSetting("stage3-ticks", 8, 16);
        default: return 0;
    }
}

std::string Engine::stageName(Stage stage) {
    switch (stage) {
        case Stage::Subtle: return "Stage 1";
        case Stage::Moderate: return "Stage 2";
        case Stage::Showcase: return "Stage 3";
        default: return "Off";
    }
}

std::string Engine::stageChip(Stage stage) {
    if (stage == Stage::Off) return "OFF";
    return fmt::format("S{} / {}t", static_cast<int>(stage), windowFor(stage));
}

ccColor3B Engine::stageColor(Stage stage) {
    switch (stage) {
        case Stage::Subtle: return { 110, 225, 150 };
        case Stage::Moderate: return { 255, 200, 90 };
        case Stage::Showcase: return { 255, 100, 100 };
        default: return { 150, 150, 160 };
    }
}

void Engine::setStage(Stage stage) {
    if (stage == m_stage) return;
    m_stage = stage;
    Mod::get()->setSavedValue<int64_t>("stage", static_cast<int64_t>(stage));

    if (consoleEnabled()) {
        log::info("[Buffer] Mode -> {} ({} ticks)", stageName(stage), windowFor(stage));
    }
    if (auto overlay = Overlay::current()) overlay->rebuild();
    Overlay::toast(
        stage == Stage::Off
            ? std::string("Buffer Off")
            : fmt::format("{}  -  {} tick window", stageName(stage), windowFor(stage)),
        stageColor(stage)
    );
}

void Engine::cycleStage() {
    setStage(static_cast<Stage>((static_cast<int>(m_stage) + 1) % 4));
}

// ---------------------------------------------------------------- input entry

bool Engine::handles(GJBaseGameLayer* layer, int button) const {
    if (button != 1) return false;  // PlayerButton::Jump only; platformer left/right untouched
    if (!isPlayLayer(layer)) return false;
    if (layer->m_levelSettings && layer->m_levelSettings->m_platformerMode) return false;
    return true;
}

bool Engine::onButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1) {
    size_t side = isPlayer1 ? 0 : 1;

    // Our own delayed input passes straight through.
    if (m_dispatching) {
        m_gameHeld[side] = down;
        return true;
    }

    auto players = playersFor(layer, isPlayer1);
    bool flight = m_stage != Stage::Off
        && Mod::get()->getSettingValue<bool>("flight-enabled")
        && anyFlight(players);

    bool pass = flight
        ? onFlightButton(layer, down, button, isPlayer1)
        : onDiscreteButton(layer, down, button, isPlayer1);

    if (pass) m_gameHeld[side] = down;
    return pass;
}

// ---------------------------------------------------------------- discrete modes

bool Engine::onDiscreteButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1) {
    size_t side = isPlayer1 ? 0 : 1;
    auto& slot = m_slots[side];
    auto players = playersFor(layer, isPlayer1);

    // Left a flight section with a delayed input still queued: let it go first.
    if (m_pending[side].active) dispatchFlight(layer, side);

    if (down) {
        // A new click while we're still holding the last one: hand GD the
        // release first so this press arrives as a real, fresh click.
        if (slot.deferred) dispatchRelease(layer, slot, isPlayer1);

        slot = InputSlot{};
        slot.tracking = true;
        slot.button = button;
        slot.pressTick = m_tick;
        for (size_t i = 0; i < players.size(); ++i) {
            if (players[i]) snapshot(players[i], slot.snap[i]);
        }
        return true;
    }

    // ---- release ----
    bool shouldHold = slot.tracking && !slot.consumed && slot.button == button
        && m_stage != Stage::Off && !anyDead(players) && !anyFlight(players);
    if (!shouldHold) {
        slot.tracking = false;
        return true;  // normal release
    }

    // The click hasn't done anything yet. Keep it held (GD sees the button as
    // still down) and let onTick decide when to really let go.
    slot.deferred = true;
    slot.releaseTick = m_tick;
    slot.stage = m_stage;
    return false;
}

void Engine::dispatchRelease(GJBaseGameLayer* layer, InputSlot& slot, bool isPlayer1) {
    int button = slot.button;
    slot.deferred = false;
    slot.tracking = false;

    // Goes back through the full hook chain (so other mods and GD's own input
    // recording see a normal release); our hook lets it through untouched.
    m_dispatching = true;
    layer->handleButton(false, button, isPlayer1);
    m_dispatching = false;
}

// ---------------------------------------------------------------- flight modes
//
// When the button changes in ship / wave / swing:
//
//   1. Simulate sending it now for `margin` ticks. If the icon survives (or the
//      path hits an orb/pad/portal we can't predict), send it now. This is the
//      normal case and costs `margin` simulated steps.
//   2. Otherwise the input would crash within `margin` ticks, faster than
//      anyone can react and correct. Try sending it 1..N ticks later instead:
//      keep the old state for k ticks, then switch, and see how long the icon
//      lives after the switch (up to `lookahead` ticks).
//   3. Use the smallest k that survives the full look-ahead, or failing that
//      the k that survives longest, as long as it beats the margin. If the
//      old state crashes before tick k, larger delays can't help either.
//
// Only delays are possible: an input that comes too late can't be moved
// earlier without rewinding the icon, which would be visible.

bool Engine::onFlightButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1) {
    size_t side = isPlayer1 ? 0 : 1;
    auto players = playersFor(layer, isPlayer1);

    // Clear any older work on this side first.
    if (m_slots[side].deferred) dispatchRelease(layer, m_slots[side], isPlayer1);
    m_slots[side].tracking = false;
    if (m_pending[side].active) dispatchFlight(layer, side);

    bool before = m_gameHeld[side];
    if (before == down || anyDead(players)) return true;

    auto& predictor = Predictor::get();
    if (!predictor.ready(layer)) return true;

    int margin = intSetting("flight-margin", 2, 16);
    int lookahead = intSetting("flight-lookahead", 16, 120);
    int window = windowTicks();

    auto now = predictor.run(layer, players, before, down, 0, margin);
    if (now.aborted || now.afterSurvived >= margin) return true;

    int bestDelay = 0;
    int bestLife = now.afterSurvived;
    for (int k = 1; k <= window; ++k) {
        auto r = predictor.run(layer, players, before, down, k, lookahead);
        if (r.beforeSurvived < k) break;  // old state crashes first: later is no better
        if (r.aborted) continue;          // can't tell, skip this candidate
        if (r.afterSurvived > bestLife) {
            bestLife = r.afterSurvived;
            bestDelay = k;
            if (bestLife >= lookahead) break;  // fully safe: earliest such tick wins
        }
    }
    if (bestDelay == 0 || bestLife < margin) return true;  // nothing better, send as-is

    auto& pend = m_pending[side];
    pend = PendingFlight{};
    pend.active = true;
    pend.down = down;
    pend.button = button;
    pend.delay = bestDelay;
    pend.wouldDieIn = now.afterSurvived + 1;
    pend.applyTick = m_tick + bestDelay;
    pend.stage = m_stage;
    pend.mode = flightModeName(players);
    return false;
}

void Engine::dispatchFlight(GJBaseGameLayer* layer, size_t side, bool applyPress) {
    auto pend = m_pending[side];
    m_pending[side].active = false;
    if (!pend.active) return;
    if (pend.down && !applyPress) return;  // dropped press (e.g. on respawn)

    m_dispatching = true;
    layer->handleButton(pend.down, pend.button, side == 0);
    m_dispatching = false;
}

// ---------------------------------------------------------------- per tick

void Engine::onTick(GJBaseGameLayer* layer, bool countTick) {
    if (countTick) ++m_tick;

    // Flight: release delayed inputs on their tick.
    for (size_t s = 0; s < m_pending.size(); ++s) {
        auto& pend = m_pending[s];
        if (!pend.active) continue;
        auto players = playersFor(layer, s == 0);

        if (anyDead(players) || m_stage == Stage::Off) {
            dispatchFlight(layer, s, !anyDead(players));
            continue;
        }
        if (m_tick >= pend.applyTick) {
            auto copy = pend;
            dispatchFlight(layer, s);
            recordFlightSave(copy, m_tick, s == 0);
        }
    }

    // Discrete: watch held releases for the moment the click gets used.
    for (size_t s = 0; s < m_slots.size(); ++s) {
        auto& slot = m_slots[s];
        if (!slot.tracking) continue;

        bool isPlayer1 = s == 0;
        auto players = playersFor(layer, isPlayer1);
        bool dead = false;

        for (size_t i = 0; i < players.size(); ++i) {
            auto p = players[i];
            if (!p) continue;
            if (p->m_isDead) dead = true;
            if (!slot.consumed && actionHappened(p, slot.snap[i])) {
                slot.consumed = true;
                slot.consumedTick = m_tick;
            }
            slot.snap[i].prevYVelocity = p->m_yVelocity;
        }

        if (!slot.deferred) {
            // Still physically held. Once it has done its job there's nothing
            // left to watch; the eventual release goes through untouched.
            if (slot.consumed) slot.tracking = false;
            continue;
        }

        if (slot.consumed) {
            // The held click turned into an action on the tick before this one.
            // Ticks saved = how many ticks after the real release it happened.
            int saved = static_cast<int>(slot.consumedTick - slot.releaseTick);
            auto stage = slot.stage;
            auto at = slot.consumedTick;
            dispatchRelease(layer, slot, isPlayer1);
            recordSave(stage, saved, at, isPlayer1);
            continue;
        }

        uint64_t waited = m_tick - slot.releaseTick;
        if (dead || m_stage == Stage::Off || waited >= static_cast<uint64_t>(windowFor(m_stage))) {
            auto stage = slot.stage;
            dispatchRelease(layer, slot, isPlayer1);
            if (!dead && m_stage != Stage::Off) recordUnused(stage, static_cast<int>(waited), isPlayer1);
        }
    }
}

// ---------------------------------------------------------------- lifecycle

void Engine::onAttemptStart(GJBaseGameLayer* layer) {
    for (size_t s = 0; s < m_slots.size(); ++s) {
        if (layer) {
            if (m_slots[s].deferred) dispatchRelease(layer, m_slots[s], s == 0);
            dispatchFlight(layer, s, false);  // held releases go out, held presses are dropped
        }
        m_slots[s] = InputSlot{};
        m_pending[s] = PendingFlight{};
    }
    m_gameHeld = {};
    m_tick = 0;
    m_stats.savesAttempt = 0;
    m_stats.unusedAttempt = 0;
    if (auto overlay = Overlay::current()) overlay->rebuild();
}

void Engine::onLevelExit() {
    m_slots = {};
    m_pending = {};
    m_gameHeld = {};
    m_tick = 0;
    m_stats.savesAttempt = 0;
    m_stats.unusedAttempt = 0;
}

// ---------------------------------------------------------------- telemetry

void Engine::bumpStats(int saved) {
    m_stats.savesAttempt++;
    m_stats.savesSession++;
    m_stats.lastSave = saved;
    m_stats.bestSave = std::max(m_stats.bestSave, saved);
}

void Engine::recordSave(Stage stage, int saved, uint64_t atTick, bool isPlayer1) {
    bumpStats(saved);

    char const* side = isPlayer1 ? "P1" : "P2";
    if (consoleEnabled()) {
        log::info("[Buffer] {} saved {} tick{} on tick #{} ({})",
            stageName(stage), saved, saved == 1 ? "" : "s", atTick, side);
    }
    if (auto overlay = Overlay::current()) {
        overlay->pushLine(fmt::format("+{}t   tick #{}   {}", saved, atTick, side), stageColor(stage));
        overlay->pulse();
    }
}

void Engine::recordFlightSave(PendingFlight const& pend, uint64_t atTick, bool isPlayer1) {
    bumpStats(pend.delay);

    char const* side = isPlayer1 ? "P1" : "P2";
    char const* what = pend.down ? "press" : "release";
    if (consoleEnabled()) {
        log::info("[Buffer] {} delayed {} {} {} tick{} on tick #{} ({}), would have crashed in {}",
            stageName(pend.stage), pend.mode, what, pend.delay, pend.delay == 1 ? "" : "s",
            atTick, side, pend.wouldDieIn);
    }
    if (auto overlay = Overlay::current()) {
        overlay->pushLine(
            fmt::format("+{}t {} {}   #{}   {}", pend.delay, pend.mode, pend.down ? "click" : "rel", atTick, side),
            stageColor(pend.stage)
        );
        overlay->pulse();
    }
}

void Engine::recordUnused(Stage stage, int waited, bool isPlayer1) {
    m_stats.unusedAttempt++;
    if (!Mod::get()->getSettingValue<bool>("show-expired")) return;

    char const* side = isPlayer1 ? "P1" : "P2";
    if (consoleEnabled()) {
        log::info("[Buffer] {} held {} ticks, unused ({})", stageName(stage), waited, side);
    }
    if (auto overlay = Overlay::current()) {
        overlay->pushLine(fmt::format("held {}t, unused   {}", waited, side), { 130, 130, 140 });
    }
}

} // namespace buffer

#include "Buffer.hpp"
#include "Overlay.hpp"
#include "Predict.hpp"
#include "TimingMap.hpp"
#include "Visuals.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace buffer {

namespace {

constexpr double kImpulse = 4.0;
constexpr double kMinActionSpeed = 3.0;
constexpr float kRouteYTolerance = 6.f;  // how close to the shadow route counts as "on it"
constexpr int kEventSearch = 24;         // ticks around the player to look for the matching route input

bool isPlayLayer(GJBaseGameLayer* layer) {
    auto pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer*>(pl) == layer;
}

bool isTwoPlayer(GJBaseGameLayer* layer) {
    return layer->m_levelSettings && layer->m_levelSettings->m_twoPlayerMode;
}

// Which players a given input side drives.
std::array<PlayerObject*, 2> playersFor(GJBaseGameLayer* layer, bool isPlayer1) {
    if (isTwoPlayer(layer)) {
        return { isPlayer1 ? layer->m_player1 : layer->m_player2, nullptr };
    }
    return { layer->m_player1, layer->m_gameState.m_isDualMode ? layer->m_player2 : nullptr };
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

char const* sideModeName(std::array<PlayerObject*, 2> const& players) {
    for (auto p : players) {
        if (p && isFlightMode(p)) return modeName(modeOf(p));
    }
    return players[0] ? modeName(modeOf(players[0])) : "";
}

void snapshot(PlayerObject* p, PlayerSnap& snap) {
    snap.lastJumpTime = p->m_lastJumpTime;
    snap.lastFlipTime = p->m_lastFlipTime;
    snap.prevYVelocity = p->m_yVelocity;
    snap.upsideDown = p->m_isUpsideDown;
    snap.dashing = p->m_isDashing;
}

bool actionHappened(PlayerObject* p, PlayerSnap const& snap) {
    if (p->m_lastJumpTime != snap.lastJumpTime) return true;
    if (p->m_lastFlipTime != snap.lastFlipTime) return true;
    if (p->m_isUpsideDown != snap.upsideDown) return true;
    if (p->m_isDashing && !snap.dashing) return true;
    double v = p->m_yVelocity;
    double dv = v - snap.prevYVelocity;
    if (dv > kImpulse && v > kMinActionSpeed) return true;
    if (-dv > kImpulse && v < -kMinActionSpeed && !p->m_isOnGround) return true;
    return false;
}

bool setting(char const* key) {
    return Mod::get()->getSettingValue<bool>(key);
}

int intSetting(char const* key, int lo, int hi) {
    return static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>(key), lo, hi));
}

ccColor3B sourceColor(ShiftSource s) {
    return s == ShiftSource::Map ? visuals::kMapColor : visuals::kLookColor;
}

} // namespace

Engine& Engine::get() {
    static Engine instance;
    return instance;
}

Engine::Engine() {
    if (setting("remember-stage")) {
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

    if (setting("console-log")) {
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
//
// Every jump press/release goes through here, in this order:
//   1. Shadow map: if you're on the shadow's route and this input is a few
//      ticks earlier than its safe window, move it into the window.
//   2. Ship/wave/swing off the route: live look-ahead (simulate, delay if
//      sending it now crashes within the reaction margin).
//   3. Other modes off the route: hold an early release until the click is used.

bool Engine::handles(GJBaseGameLayer* layer, int button) const {
    if (button != 1) return false;  // PlayerButton::Jump only
    if (!isPlayLayer(layer)) return false;
    if (layer->m_isPlatformer || (layer->m_levelSettings && layer->m_levelSettings->m_platformerMode)) return false;
    return true;
}

bool Engine::onButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1) {
    size_t side = isPlayer1 ? 0 : 1;

    // Our own delayed input passes straight through.
    if (m_dispatching) {
        m_gameHeld[side] = down;
        return true;
    }
    m_diag.inputs++;

    // A newer input supersedes anything still held back on this side.
    if (m_pending[side].active) dispatchPending(layer, side);
    if (down && m_slots[side].deferred) dispatchRelease(layer, m_slots[side], isPlayer1);

    auto players = playersFor(layer, isPlayer1);
    bool before = m_gameHeld[side];
    bool pass = true;

    if (m_stage == Stage::Off || before == down || anyDead(players)) {
        if (down == false) m_slots[side].tracking = false;
        if (down && m_stage != Stage::Off) {
            // still feed the cube-style tracker so its state stays correct
            pass = onDiscreteButton(layer, down, button, isPlayer1);
        }
        if (pass) m_gameHeld[side] = down;
        return pass;
    }

    // 1. shadow map (single-icon play only)
    bool singleIcon = isPlayer1 && !isTwoPlayer(layer) && players[1] == nullptr;
    if (singleIcon && setting("map-enabled")) {
        auto advice = adviseFromMap(layer, players[0], before, down);
        if (advice.onRoute) {
            m_diag.mapMatches++;
            if (advice.delay > 0) {
                PendingShift shift;
                shift.down = down;
                shift.button = button;
                shift.delay = advice.delay;
                shift.source = ShiftSource::Map;
                shift.mode = sideModeName(players);
                hold(layer, side, shift);
                return false;
            }
            if (anyFlight(players)) {
                // on the route and inside the safe window: nothing to fix
                m_gameHeld[side] = down;
                return true;
            }
        }
    }

    // 2 / 3
    pass = (setting("flight-enabled") && anyFlight(players))
        ? onFlightButton(layer, before, down, button, isPlayer1)
        : onDiscreteButton(layer, down, button, isPlayer1);

    if (pass) m_gameHeld[side] = down;
    return pass;
}

// ---------------------------------------------------------------- 1. shadow map

Engine::MapAdvice Engine::adviseFromMap(GJBaseGameLayer* layer, PlayerObject* p, bool before, bool down) const {
    auto const& map = TimingMap::get();
    if (!p || !map.usable()) return {};

    int tp = map.tickAtX(p->getPositionX());
    if (tp < 0) return {};
    auto const& s = map.path()[tp];
    if (s.mode != modeOf(p) || s.upsideDown != p->m_isUpsideDown || s.held != before) return {};
    if (std::fabs(s.y - p->getPositionY()) > kRouteYTolerance) return {};

    auto ev = map.nearestEvent(tp, down, kEventSearch);
    if (!ev || !ev->windowKnown) return {};

    MapAdvice advice;
    advice.onRoute = true;
    int lo = ev->tick + ev->earliest;
    int hi = ev->tick + ev->latest;
    if (tp >= lo && tp <= hi) return advice;  // already inside the safe window

    if (tp < lo) {
        // Early: aim between the earliest safe tick and the shadow's own tick.
        int target = lo + (ev->tick - lo) / 2;
        int k = target - tp;
        if (k >= 1 && k <= windowTicks()) advice.delay = k;
    }
    // Late inputs can't be fixed without rewinding the icon; leave them.
    return advice;
}

// ---------------------------------------------------------------- 2. live look-ahead

bool Engine::onFlightButton(GJBaseGameLayer* layer, bool before, bool down, int button, bool isPlayer1) {
    size_t side = isPlayer1 ? 0 : 1;
    auto players = playersFor(layer, isPlayer1);
    m_slots[side].tracking = false;

    auto& predictor = Predictor::get();
    if (!predictor.ready(layer)) return true;

    int margin = intSetting("flight-margin", 2, 16);
    int lookahead = intSetting("flight-lookahead", 16, 120);
    int window = windowTicks();

    std::vector<CCPoint> nowTrace;
    auto now = predictor.run(layer, players, before, down, 0, margin, &nowTrace);
    if (now.aborted || now.afterSurvived >= margin) return true;

    int bestDelay = 0;
    int bestLife = now.afterSurvived;
    for (int k = 1; k <= window; ++k) {
        auto r = predictor.run(layer, players, before, down, k, lookahead);
        if (r.beforeSurvived < k) break;
        if (r.aborted) continue;
        if (r.afterSurvived > bestLife) {
            bestLife = r.afterSurvived;
            bestDelay = k;
            if (bestLife >= lookahead) break;
        }
    }
    if (bestDelay == 0 || bestLife < margin) return true;

    if (setting("show-lookahead")) {
        std::vector<CCPoint> chosen;
        predictor.run(layer, players, before, down, bestDelay, lookahead, &chosen);
        visuals::drawTrace(layer, nowTrace, { 1.f, .3f, .3f, .9f }, true);
        visuals::drawTrace(layer, chosen, { .4f, 1.f, .5f, .8f }, false);
    }

    PendingShift shift;
    shift.down = down;
    shift.button = button;
    shift.delay = bestDelay;
    shift.wouldDieIn = now.afterSurvived + 1;
    shift.source = ShiftSource::Lookahead;
    shift.mode = sideModeName(players);
    hold(layer, side, shift);
    return false;
}

// ---------------------------------------------------------------- 3. cube-style release hold

bool Engine::onDiscreteButton(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1) {
    size_t side = isPlayer1 ? 0 : 1;
    auto& slot = m_slots[side];
    auto players = playersFor(layer, isPlayer1);

    if (down) {
        slot = InputSlot{};
        slot.tracking = true;
        slot.button = button;
        slot.pressTick = m_tick;
        for (size_t i = 0; i < players.size(); ++i) {
            if (players[i]) snapshot(players[i], slot.snap[i]);
        }
        return true;
    }

    bool shouldHold = slot.tracking && !slot.consumed && slot.button == button
        && m_stage != Stage::Off && !anyDead(players) && !anyFlight(players);
    if (!shouldHold) {
        slot.tracking = false;
        return true;
    }

    slot.deferred = true;
    slot.releaseTick = m_tick;
    slot.stage = m_stage;
    if (players[0]) {
        slot.releasePos = players[0]->getPosition();
        visuals::markInput(layer, slot.releasePos, visuals::kHoldColor);
    }
    return false;
}

void Engine::dispatchRelease(GJBaseGameLayer* layer, InputSlot& slot, bool isPlayer1) {
    int button = slot.button;
    slot.deferred = false;
    slot.tracking = false;

    m_dispatching = true;
    layer->handleButton(false, button, isPlayer1);
    m_dispatching = false;
}

// ---------------------------------------------------------------- held shifts

void Engine::hold(GJBaseGameLayer* layer, size_t side, PendingShift shift) {
    auto players = playersFor(layer, side == 0);
    shift.active = true;
    shift.applyTick = m_tick + static_cast<uint64_t>(shift.delay);
    shift.stage = m_stage;
    if (players[0]) {
        shift.inputPos = players[0]->getPosition();
        visuals::markInput(layer, shift.inputPos, sourceColor(shift.source));
    }
    m_pending[side] = shift;
}

void Engine::dispatchPending(GJBaseGameLayer* layer, size_t side, bool applyPress) {
    auto pend = m_pending[side];
    m_pending[side].active = false;
    if (!pend.active) return;
    if (pend.down && !applyPress) return;

    m_dispatching = true;
    layer->handleButton(pend.down, pend.button, side == 0);
    m_dispatching = false;
}

// ---------------------------------------------------------------- per tick

void Engine::onTick(GJBaseGameLayer* layer, bool countTick) {
    if (countTick) ++m_tick;

    for (size_t s = 0; s < m_pending.size(); ++s) {
        auto& pend = m_pending[s];
        if (!pend.active) continue;
        auto players = playersFor(layer, s == 0);

        if (anyDead(players) || m_stage == Stage::Off) {
            dispatchPending(layer, s, !anyDead(players));
            continue;
        }
        if (m_tick >= pend.applyTick) {
            auto copy = pend;
            dispatchPending(layer, s);
            recordShift(layer, copy, m_tick, s == 0);
        }
    }

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
            if (slot.consumed) slot.tracking = false;
            continue;
        }

        if (slot.consumed) {
            int saved = static_cast<int>(slot.consumedTick - slot.releaseTick);
            auto copy = slot;
            dispatchRelease(layer, slot, isPlayer1);
            recordHoldSave(layer, copy, saved, copy.consumedTick, isPlayer1);
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
            dispatchPending(layer, s, false);
        }
        m_slots[s] = InputSlot{};
        m_pending[s] = PendingShift{};
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
    m_diag = {};
    Predictor::get().resetCounters();
}

// ---------------------------------------------------------------- telemetry

void Engine::bumpStats(int saved) {
    m_stats.savesAttempt++;
    m_stats.savesSession++;
    m_stats.lastSave = saved;
    m_stats.bestSave = std::max(m_stats.bestSave, saved);
}

void Engine::recordShift(GJBaseGameLayer* layer, PendingShift const& shift, uint64_t atTick, bool isPlayer1) {
    bumpStats(shift.delay);
    bool fromMap = shift.source == ShiftSource::Map;
    (fromMap ? m_diag.mapShifts : m_diag.lookShifts)++;

    auto color = sourceColor(shift.source);
    auto players = playersFor(layer, isPlayer1);
    if (players[0]) visuals::markApplied(layer, shift.inputPos, players[0]->getPosition(), shift.delay, color);

    char const* side = isPlayer1 ? "P1" : "P2";
    char const* what = shift.down ? "click" : "release";
    if (setting("console-log")) {
        if (fromMap) {
            log::info("[Buffer] {} map moved {} {} +{} tick{} on tick #{} ({}) into the shadow's safe window",
                stageName(shift.stage), shift.mode, what, shift.delay, shift.delay == 1 ? "" : "s", atTick, side);
        }
        else {
            log::info("[Buffer] {} look-ahead moved {} {} +{} tick{} on tick #{} ({}), would have crashed in {}",
                stageName(shift.stage), shift.mode, what, shift.delay, shift.delay == 1 ? "" : "s",
                atTick, side, shift.wouldDieIn);
        }
    }
    if (auto overlay = Overlay::current()) {
        overlay->pushLine(
            fmt::format("+{}t {} {}  {}  #{}", shift.delay, shift.mode, shift.down ? "click" : "rel",
                fromMap ? "map" : "look", atTick),
            color
        );
        overlay->pulse();
    }
}

void Engine::recordHoldSave(GJBaseGameLayer* layer, InputSlot const& slot, int saved, uint64_t atTick, bool isPlayer1) {
    bumpStats(saved);
    m_diag.holdSaves++;

    auto players = playersFor(layer, isPlayer1);
    if (players[0]) visuals::markApplied(layer, slot.releasePos, players[0]->getPosition(), saved, visuals::kHoldColor);

    char const* side = isPlayer1 ? "P1" : "P2";
    if (setting("console-log")) {
        log::info("[Buffer] {} held release {} tick{} on tick #{} ({})",
            stageName(slot.stage), saved, saved == 1 ? "" : "s", atTick, side);
    }
    if (auto overlay = Overlay::current()) {
        overlay->pushLine(fmt::format("+{}t release held  hold  #{}", saved, atTick), visuals::kHoldColor);
        overlay->pulse();
    }
}

void Engine::recordUnused(Stage stage, int waited, bool isPlayer1) {
    m_stats.unusedAttempt++;
    if (!setting("show-expired")) return;

    char const* side = isPlayer1 ? "P1" : "P2";
    if (setting("console-log")) {
        log::info("[Buffer] {} held {} ticks, unused ({})", stageName(stage), waited, side);
    }
    if (auto overlay = Overlay::current()) {
        overlay->pushLine(fmt::format("held {}t, unused   {}", waited, side), { 130, 130, 140 });
    }
}

} // namespace buffer

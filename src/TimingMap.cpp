#include "TimingMap.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>

using namespace geode::prelude;

namespace buffer {

namespace {

constexpr int kDecisionTicks = 2;   // the shadow may change its input every 2 ticks
constexpr int kMaxShift = 16;       // window search range (= largest stage window)
constexpr float kXMatch = 8.f;      // how close the player's x must be to a route sample

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Reuses PlayerCheckpoint objects. Creating millions of autoreleased ones
// inside a single frame would hold all of them in memory until the frame ends.
class CheckpointPool {
public:
    PlayerCheckpoint* take() {
        if (!m_free.empty()) {
            auto cp = m_free.back();
            m_free.pop_back();
            return cp;
        }
        auto cp = PlayerCheckpoint::create();
        cp->retain();
        m_all.push_back(cp);
        return cp;
    }
    void give(PlayerCheckpoint* cp) {
        if (cp) m_free.push_back(cp);
    }
    ~CheckpointPool() {
        for (auto cp : m_all) cp->release();
    }

private:
    std::vector<PlayerCheckpoint*> m_all;
    std::vector<PlayerCheckpoint*> m_free;
};

// Coarse fingerprint of the shadow's state. Two branches that land on the same
// fingerprint behave the same from here on, so only the first is explored.
uint64_t stateKey(PlayerObject* p, int tick, bool held) {
    auto y = static_cast<int64_t>(std::lround(p->getPositionY() * 2.0));
    auto vy = static_cast<int64_t>(std::lround(p->m_yVelocity * 10.0));
    uint64_t h = static_cast<uint64_t>(tick);
    auto mix = [&](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    mix(static_cast<uint64_t>(y));
    mix(static_cast<uint64_t>(vy));
    mix(static_cast<uint64_t>(modeOf(p)));
    mix((p->m_isUpsideDown ? 1u : 0u) | (held ? 2u : 0u) | (p->m_isOnGround ? 4u : 0u)
        | (p->m_vehicleSize < 1.f ? 8u : 0u));
    return h;
}

RouteSample sampleOf(PlayerObject* p, bool held) {
    RouteSample s;
    s.x = p->getPositionX();
    s.y = p->getPositionY();
    s.held = held;
    s.upsideDown = p->m_isUpsideDown;
    s.mode = modeOf(p);
    return s;
}

} // namespace

TimingMap& TimingMap::get() {
    static TimingMap instance;
    return instance;
}

void TimingMap::clear() {
    m_state = MapState::None;
    m_reason.clear();
    m_path.clear();
    m_events.clear();
    m_levelLength = 0.f;
    m_startX = 0.f;
    m_windowsKnown = 0;
    m_seconds = 0.0;
    m_steps = 0;
}

// ---------------------------------------------------------------- scan

void TimingMap::scan(PlayLayer* layer) {
    clear();
    auto t0 = Clock::now();

    auto& pred = Predictor::get();
    pred.attach(layer);
    if (!pred.ready(layer) || !layer->m_player1) {
        m_state = MapState::Failed;
        m_reason = "shadow player could not be created";
        return;
    }
    bool platformer = layer->m_isPlatformer || (layer->m_levelSettings && layer->m_levelSettings->m_platformerMode);
    bool twoPlayer = layer->m_levelSettings && layer->m_levelSettings->m_twoPlayerMode;
    if (platformer || twoPlayer || layer->m_gameState.m_isDualMode) {
        m_state = MapState::Unsupported;
        m_reason = platformer ? "platformer level" : (twoPlayer ? "2-player level" : "starts in dual");
        return;
    }

    double limitMs = 1000.0 * static_cast<double>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("map-time-limit"), 1, 20));
    double searchMs = limitMs * 0.75;
    int horizon = static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("flight-lookahead"), 16, 120));

    auto real = layer->m_player1;
    auto fake = pred.clone(0);
    m_levelLength = layer->m_levelLength > 0.f ? layer->m_levelLength : 1e9f;
    m_startX = real->getPositionX();
    float endX = m_levelLength - 2.f;

    CheckpointPool pool;
    auto startCp = pool.take();
    real->saveToCheckpoint(startCp);

    pred.beginScan();

    // ---------------- phase 1: find a route (depth-first, "keep the input" tried first)
    struct Node { int parent; int tick; bool held; };
    struct Frame { int node; PlayerCheckpoint* cp; uint8_t tried; };

    std::vector<Node> nodes;
    nodes.reserve(1 << 16);
    std::vector<Frame> stack;
    std::unordered_set<uint64_t> seen;

    nodes.push_back({ -1, 0, false });
    stack.push_back({ 0, startCp, 0 });
    int cloneAt = -1;
    int deepest = 0;
    int finalNode = -1;
    bool hitDual = false;
    bool outOfTime = false;
    long long iterations = 0;

    while (!stack.empty()) {
        if ((++iterations & 63) == 0 && msSince(t0) > searchMs) {
            outOfTime = true;
            break;
        }

        auto& top = stack.back();
        Node n = nodes[top.node];
        bool option;
        if (!(top.tried & 1)) { option = n.held; top.tried |= 1; }
        else if (!(top.tried & 2)) { option = !n.held; top.tried |= 2; }
        else {
            if (top.cp != startCp) pool.give(top.cp);
            if (cloneAt == top.node) cloneAt = -1;
            stack.pop_back();
            continue;
        }

        if (cloneAt != top.node) pred.loadState(fake, top.cp, n.held);
        if (option != n.held) pred.press(fake, option);
        int parent = top.node;
        cloneAt = -1;  // the shadow is about to move on from this node

        pred.clearFlags();
        bool alive = true;
        bool finished = false;
        for (int s = 0; s < kDecisionTicks; ++s) {
            if (!pred.stepClone(layer, fake)) {
                alive = false;
                if (pred.aborted()) hitDual = true;
                break;
            }
            ++m_steps;
            if (fake->getPositionX() >= endX) { finished = true; break; }
        }
        if (!alive) continue;

        int childTick = n.tick + kDecisionTicks;
        nodes.push_back({ parent, childTick, option });
        int child = static_cast<int>(nodes.size()) - 1;
        if (finished) { finalNode = child; break; }
        if (!seen.insert(stateKey(fake, childTick, option)).second) continue;
        if (childTick > nodes[deepest].tick) deepest = child;

        auto cp = pool.take();
        fake->saveToCheckpoint(cp);
        stack.push_back({ child, cp, 0 });  // `top` is invalid from here on
        cloneAt = child;
    }
    for (auto& f : stack) {
        if (f.cp != startCp) pool.give(f.cp);
    }

    bool complete = finalNode >= 0;
    if (!complete) finalNode = deepest;
    int finalTick = nodes[finalNode].tick;

    if (finalTick < 8) {
        pred.endScan();
        m_state = MapState::Failed;
        m_reason = "shadow crashes right at the start";
        m_seconds = msSince(t0) / 1000.0;
        log::warn("[Buffer] Shadow map failed: {}", m_reason);
        return;
    }

    // Decisions -> events (an event is a change of input on the route).
    std::vector<int> chain;
    for (int i = finalNode; i >= 0; i = nodes[i].parent) chain.push_back(i);
    std::reverse(chain.begin(), chain.end());
    for (size_t c = 1; c < chain.size(); ++c) {
        auto const& node = nodes[chain[c]];
        auto const& par = nodes[chain[c - 1]];
        if (node.held != par.held) {
            RouteEvent ev;
            ev.tick = par.tick;
            ev.down = node.held;
            m_events.push_back(ev);
        }
    }
    nodes.clear();
    nodes.shrink_to_fit();
    seen.clear();

    // ---------------- phase 2: replay the route, record it, keep restart points
    size_t nEv = m_events.size();
    std::vector<int> base(nEv);
    std::vector<PlayerCheckpoint*> baseCp(nEv, nullptr);
    for (size_t i = 0; i < nEv; ++i) {
        int prev = i ? m_events[i - 1].tick + 1 : 0;
        base[i] = std::max({ m_events[i].tick - kMaxShift, prev, 0 });
    }

    pred.loadState(fake, startCp, false);
    pred.clearFlags();
    bool held = false;
    size_t nextEv = 0, nextBase = 0;
    int replayDiedAt = -1;
    m_path.reserve(static_cast<size_t>(finalTick) + 1);

    for (int t = 0; t < finalTick; ++t) {
        while (nextBase < nEv && base[nextBase] == t) {
            baseCp[nextBase] = pool.take();
            fake->saveToCheckpoint(baseCp[nextBase]);
            ++nextBase;
        }
        m_path.push_back(sampleOf(fake, held));
        if (nextEv < nEv && m_events[nextEv].tick == t) {
            held = m_events[nextEv].down;
            pred.press(fake, held);
            ++nextEv;
        }
        if (!pred.stepClone(layer, fake)) { replayDiedAt = t; break; }
        ++m_steps;
        if (fake->getPositionX() >= endX) break;
    }
    if (replayDiedAt >= 0) {
        // The search and the replay disagree: physics isn't fully captured by
        // the checkpoint copy. Keep only the part that replayed cleanly.
        while (!m_events.empty() && m_events.back().tick >= replayDiedAt) m_events.pop_back();
        nEv = m_events.size();
    }

    // ---------------- phase 3: safe window for every input on the route
    auto testShift = [&](size_t i, int s) -> bool {
        auto const& ev = m_events[i];
        int a = ev.tick + s;
        if (a < base[i] || !baseCp[i]) return false;
        if (i > 0 && a <= m_events[i - 1].tick) return false;
        if (i + 1 < nEv && a >= m_events[i + 1].tick) return false;

        pred.loadState(fake, baseCp[i], !ev.down);
        pred.clearFlags();
        size_t j = i + 1;
        int end = std::min(a + horizon, static_cast<int>(m_path.size()));
        for (int t = base[i]; t < end; ++t) {
            if (t == a) pred.press(fake, ev.down);
            else if (j < nEv && t == m_events[j].tick) { pred.press(fake, m_events[j].down); ++j; }
            if (!pred.stepClone(layer, fake)) return false;
            ++m_steps;
        }
        return true;
    };

    for (size_t i = 0; i < nEv; ++i) {
        if (msSince(t0) > limitMs) break;
        auto& ev = m_events[i];
        for (int s = -1; s >= -kMaxShift && testShift(i, s); --s) ev.earliest = s;
        for (int s = 1; s <= kMaxShift && testShift(i, s); ++s) ev.latest = s;
        ev.windowKnown = true;
        ++m_windowsKnown;
    }
    for (auto cp : baseCp) pool.give(cp);

    pred.endScan();

    // ---------------- result
    m_seconds = msSince(t0) / 1000.0;
    if (complete && replayDiedAt < 0 && m_windowsKnown == static_cast<int>(nEv)) {
        m_state = MapState::Ready;
    }
    else {
        m_state = MapState::Partial;
        if (replayDiedAt >= 0) m_reason = "replay drifted";
        else if (!complete && hitDual) m_reason = "dual portal";
        else if (!complete && outOfTime) m_reason = "time limit";
        else if (!complete) m_reason = "no route found past here";
        else m_reason = "time limit (windows)";
    }

    log::info("[Buffer] Shadow map: {} - {} inputs, {}/{} windows, {} steps in {:.2f}s",
        statusText(), nEv, m_windowsKnown, nEv, m_steps, m_seconds);
}

// ---------------------------------------------------------------- lookup

int TimingMap::tickAtX(float x) const {
    if (m_path.empty()) return -1;
    auto it = std::lower_bound(m_path.begin(), m_path.end(), x,
        [](RouteSample const& s, float v) { return s.x < v; });
    int t = static_cast<int>(it - m_path.begin());
    if (t >= static_cast<int>(m_path.size())) t = static_cast<int>(m_path.size()) - 1;
    if (t > 0 && std::fabs(m_path[t - 1].x - x) < std::fabs(m_path[t].x - x)) --t;
    if (std::fabs(m_path[t].x - x) > kXMatch) return -1;
    return t;
}

RouteEvent const* TimingMap::nearestEvent(int tick, bool down, int range) const {
    RouteEvent const* best = nullptr;
    int bestDist = range + 1;
    auto it = std::lower_bound(m_events.begin(), m_events.end(), tick - range,
        [](RouteEvent const& e, int v) { return e.tick < v; });
    for (; it != m_events.end() && it->tick <= tick + range; ++it) {
        if (it->down != down) continue;
        int d = std::abs(it->tick - tick);
        if (d < bestDist) { bestDist = d; best = &*it; }
    }
    return best;
}

// ---------------------------------------------------------------- status

std::string TimingMap::statusText() const {
    auto pct = [&] {
        if (m_path.empty() || m_levelLength >= 1e8f) return 0;
        float span = std::max(1.f, m_levelLength - m_startX);
        return std::clamp(static_cast<int>(std::lround(100.f * (m_path.back().x - m_startX) / span)), 0, 100);
    };
    switch (m_state) {
        case MapState::None: return "MAP  not built";
        case MapState::Ready:
            return fmt::format("MAP 100%  {} inputs  {:.1f}s", m_events.size(), m_seconds);
        case MapState::Partial:
            return fmt::format("MAP {}%  {} inputs  ({})", pct(), m_events.size(), m_reason);
        case MapState::Unsupported: return fmt::format("MAP  off ({})", m_reason);
        case MapState::Failed: return fmt::format("MAP FAILED  {}", m_reason);
    }
    return "";
}

ccColor3B TimingMap::statusColor() const {
    switch (m_state) {
        case MapState::Ready: return { 120, 230, 150 };
        case MapState::Partial: return { 255, 205, 90 };
        case MapState::Failed: return { 255, 110, 110 };
        default: return { 150, 150, 165 };
    }
}

} // namespace buffer

#pragma once

#include <Geode/Geode.hpp>

#include "Predict.hpp"

#include <string>
#include <vector>

namespace buffer {

// Shadow route state at the start of one physics tick.
struct RouteSample {
    float x = 0.f;
    float y = 0.f;
    bool held = false;        // button state going into this tick
    bool upsideDown = false;
    Mode mode = Mode::Cube;
};

// One click or release on the shadow route, plus how far it can move.
struct RouteEvent {
    int tick = 0;
    bool down = false;
    int earliest = 0;          // ticks it can come early and still survive (<= 0)
    int latest = 0;            // ticks it can come late and still survive (>= 0)
    bool windowKnown = false;  // false if the scan ran out of time before testing it
};

enum class MapState { None, Ready, Partial, Failed, Unsupported };

// Built once when a level opens: a hidden shadow icon plays through the level
// (searching click/release timings until it finds a route that survives), then
// every input on that route is re-tested shifted earlier/later to find its
// safe window. During play, Engine looks up the route by x position.
class TimingMap {
public:
    static TimingMap& get();

    void clear();
    void scan(PlayLayer* layer);  // synchronous, bounded by the "map-time-limit" setting

    MapState state() const { return m_state; }
    bool usable() const { return (m_state == MapState::Ready || m_state == MapState::Partial) && !m_path.empty(); }
    std::vector<RouteSample> const& path() const { return m_path; }
    std::vector<RouteEvent> const& events() const { return m_events; }

    int tickAtX(float x) const;  // route tick whose x matches, or -1
    RouteEvent const* nearestEvent(int tick, bool down, int range) const;

    std::string statusText() const;
    cocos2d::ccColor3B statusColor() const;

private:
    TimingMap() = default;

    MapState m_state = MapState::None;
    std::string m_reason;
    std::vector<RouteSample> m_path;
    std::vector<RouteEvent> m_events;
    float m_levelLength = 0.f;
    float m_startX = 0.f;
    int m_windowsKnown = 0;
    double m_seconds = 0.0;
    long long m_steps = 0;
};

} // namespace buffer

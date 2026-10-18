#pragma once

#include <Geode/Geode.hpp>

#include <vector>

// Everything drawn inside the level (world space, scrolls with the level).
namespace buffer::visuals {

// Source colors, shared with the overlay log.
inline cocos2d::ccColor3B const kMapColor = { 90, 200, 255 };    // shift chosen from the shadow map
inline cocos2d::ccColor3B const kLookColor = { 255, 205, 80 };   // shift chosen by live look-ahead
inline cocos2d::ccColor3B const kHoldColor = { 120, 230, 150 };  // cube-style release hold

// Shadow route from the TimingMap: path line, click/release dots, safe windows.
void buildRoute(GJBaseGameLayer* layer);
void clearRoute(GJBaseGameLayer* layer);
void cullRoute(GJBaseGameLayer* layer);  // only keep chunks near the player visible

// Frame-shift markers: a ring where you actually pressed, then a dot where the
// game received it, joined by a line, with a floating "+3t".
void markInput(GJBaseGameLayer* layer, cocos2d::CCPoint at, cocos2d::ccColor3B color);
void markApplied(GJBaseGameLayer* layer, cocos2d::CCPoint from, cocos2d::CCPoint to, int ticks, cocos2d::ccColor3B color);

// Live look-ahead debug: the path that would have crashed (red, X at the
// crash) and the path that was chosen instead (green).
void drawTrace(GJBaseGameLayer* layer, std::vector<cocos2d::CCPoint> const& points, cocos2d::ccColor4F color, bool crossAtEnd);

} // namespace buffer::visuals

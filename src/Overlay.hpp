#pragma once

#include <Geode/Geode.hpp>

#include <deque>
#include <string>

namespace buffer {

// Compact status panel pinned to a screen corner while playing:
//
//   | INPUT BUFFER          S2 / 4t |   <- header, chip colored by stage
//   | 3 saved this attempt  best +4t |   <- stats
//   | ------------------------------ |
//   | +2t   tick #12044   P1         |   <- newest save first, older ones fade
//   | +1t   tick #11190   P1         |
//
// Always visible while a stage is active.
class Overlay : public cocos2d::CCNode {
public:
    static Overlay* create();
    static Overlay* current();  // the overlay in the running PlayLayer, if any

    // Short centered banner, used for stage changes.
    static void toast(std::string const& text, cocos2d::ccColor3B color);

    void rebuild();
    void pushLine(std::string text, cocos2d::ccColor3B color);
    void pulse();

protected:
    bool init() override;
    void place();

    struct Line {
        std::string text;
        cocos2d::ccColor3B color;
    };
    std::deque<Line> m_lines;

    cocos2d::CCLabelBMFont* m_chip = nullptr;
    cocos2d::CCLayerColor* m_accent = nullptr;
    cocos2d::ccColor3B m_color = { 150, 150, 160 };
};

} // namespace buffer

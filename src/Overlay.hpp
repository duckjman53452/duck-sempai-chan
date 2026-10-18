#pragma once

#include <Geode/Geode.hpp>

#include <cstdint>
#include <deque>
#include <string>

namespace buffer {

// Compact status panel pinned to a screen corner while playing:
//
//   | INPUT BUFFER               S2 / 4t |  header, chip colored by stage
//   | 3 shifts this attempt    best +4t  |  stats (stage on)
//   | MAP 100%  412 inputs  3.1s         |  shadow map status
//   | in 38 | 240 tps | route 12 | ...   |  diagnostics (live, 2x per second)
//   | ---------------------------------- |
//   | +2t wave click  map  #12044        |  newest shift first, older ones fade
class Overlay : public cocos2d::CCNode {
public:
    static Overlay* create();
    static Overlay* current();

    static void toast(std::string const& text, cocos2d::ccColor3B color);

    void rebuild();
    void pushLine(std::string text, cocos2d::ccColor3B color);
    void pulse();

protected:
    bool init() override;
    void place();
    void refreshDiag(float);
    std::string diagText();

    struct Line {
        std::string text;
        cocos2d::ccColor3B color;
    };
    std::deque<Line> m_lines;

    cocos2d::CCLabelBMFont* m_chip = nullptr;
    cocos2d::CCLabelBMFont* m_diag = nullptr;
    cocos2d::CCLayerColor* m_accent = nullptr;
    cocos2d::ccColor3B m_color = { 150, 150, 160 };
    uint64_t m_lastTick = 0;
    int m_tps = 0;
};

} // namespace buffer

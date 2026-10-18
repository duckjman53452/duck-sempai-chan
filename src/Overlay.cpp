#include "Overlay.hpp"
#include "Buffer.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace buffer {

namespace {
constexpr size_t kMaxStoredLines = 8;

constexpr float kWidth = 138.f;
constexpr float kPadX = 9.f;
constexpr float kPadY = 5.f;
constexpr float kHeaderH = 13.f;
constexpr float kStatsH = 10.f;
constexpr float kLineH = 9.f;
constexpr float kGap = 4.f;
constexpr float kMargin = 6.f;
} // namespace

Overlay* Overlay::create() {
    auto ret = new Overlay();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

Overlay* Overlay::current() {
    auto pl = PlayLayer::get();
    if (!pl) return nullptr;
    return typeinfo_cast<Overlay*>(pl->getChildByID("overlay"_spr));
}

bool Overlay::init() {
    if (!CCNode::init()) return false;
    this->rebuild();
    return true;
}

void Overlay::place() {
    auto win = CCDirector::get()->getWinSize();
    auto mod = Mod::get();
    auto corner = mod->getSettingValue<std::string>("overlay-corner");
    bool right = corner.find("Right") != std::string::npos;
    bool bottom = corner.find("Bottom") != std::string::npos;

    this->setScale(static_cast<float>(mod->getSettingValue<double>("overlay-scale")));
    this->setAnchorPoint({ right ? 1.f : 0.f, bottom ? 0.f : 1.f });
    this->setPosition({
        right ? win.width - kMargin : kMargin,
        bottom ? kMargin : win.height - kMargin,
    });
}

void Overlay::rebuild() {
    this->removeAllChildrenWithCleanup(true);
    m_chip = nullptr;
    m_accent = nullptr;

    auto& engine = Engine::get();
    auto mod = Mod::get();
    auto stage = engine.stage();
    bool off = stage == Stage::Off;

    if (off && mod->getSettingValue<bool>("hide-when-off")) {
        this->setVisible(false);
        return;
    }
    this->setVisible(true);

    m_color = Engine::stageColor(stage);
    int wanted = off ? 0 : static_cast<int>(mod->getSettingValue<int64_t>("log-lines"));
    int shown = std::min<int>(wanted, static_cast<int>(m_lines.size()));

    float height = kPadY * 2 + kHeaderH;
    if (!off) height += kStatsH;
    if (shown > 0) height += kGap + shown * kLineH;
    this->setContentSize({ kWidth, height });

    // Panel
    auto bg = CCScale9Sprite::create("square02_small.png");
    bg->setContentSize({ kWidth, height });
    bg->setAnchorPoint({ 0.f, 0.f });
    bg->setPosition({ 0.f, 0.f });
    bg->setOpacity(off ? 80 : 120);
    this->addChild(bg, 0);

    // Stage accent bar on the left edge
    m_accent = CCLayerColor::create({ m_color.r, m_color.g, m_color.b, 230 }, 2.f, height - 8.f);
    m_accent->setPosition({ 3.f, 4.f });
    this->addChild(m_accent, 1);

    // Header
    float y = height - kPadY - kHeaderH / 2;

    auto title = CCLabelBMFont::create("INPUT BUFFER", "bigFont.fnt");
    title->setScale(.3f);
    title->setAnchorPoint({ 0.f, .5f });
    title->setPosition({ kPadX, y });
    title->setOpacity(off ? 150 : 235);
    this->addChild(title, 2);

    m_chip = CCLabelBMFont::create(Engine::stageChip(stage).c_str(), "bigFont.fnt");
    m_chip->setScale(.3f);
    m_chip->setAnchorPoint({ 1.f, .5f });
    m_chip->setPosition({ kWidth - kPadX, y });
    m_chip->setColor(m_color);
    this->addChild(m_chip, 2);

    // Stats
    if (!off) {
        y -= kHeaderH / 2 + kStatsH / 2;
        auto const& st = engine.stats();
        auto statsText = st.savesAttempt == 0
            ? fmt::format("no saves this attempt   session {}", st.savesSession)
            : fmt::format("{} saved this attempt   best +{}t", st.savesAttempt, st.bestSave);

        auto stats = CCLabelBMFont::create(statsText.c_str(), "chatFont.fnt");
        stats->setAnchorPoint({ 0.f, .5f });
        stats->setPosition({ kPadX, y });
        stats->setColor({ 190, 190, 205 });
        stats->limitLabelWidth(kWidth - kPadX * 2, .5f, .1f);
        this->addChild(stats, 2);
    }

    // Log
    if (shown > 0) {
        float lineTop = y - kStatsH / 2 - kGap / 2;

        auto divider = CCLayerColor::create({ 255, 255, 255, 28 }, kWidth - kPadX * 2, 1.f);
        divider->setPosition({ kPadX, lineTop });
        this->addChild(divider, 2);

        for (int i = 0; i < shown; ++i) {
            auto const& line = m_lines[i];
            auto label = CCLabelBMFont::create(line.text.c_str(), "chatFont.fnt");
            label->setAnchorPoint({ 0.f, .5f });
            label->setPosition({ kPadX, lineTop - kGap / 2 - kLineH / 2 - i * kLineH });
            label->setColor(line.color);
            label->setOpacity(static_cast<GLubyte>(std::max(90, 255 - i * 40)));
            label->limitLabelWidth(kWidth - kPadX * 2, .45f, .1f);
            this->addChild(label, 2);
        }
    }

    this->place();
}

void Overlay::pushLine(std::string text, ccColor3B color) {
    m_lines.push_front({ std::move(text), color });
    while (m_lines.size() > kMaxStoredLines) m_lines.pop_back();
    this->rebuild();
}

void Overlay::pulse() {
    if (m_chip) {
        m_chip->stopAllActions();
        m_chip->setScale(.3f);
        m_chip->runAction(CCSequence::create(
            CCScaleTo::create(.06f, .37f),
            CCScaleTo::create(.16f, .3f),
            nullptr
        ));
    }
    if (m_accent) {
        m_accent->stopAllActions();
        m_accent->setColor({ 255, 255, 255 });
        m_accent->runAction(CCTintTo::create(.25f, m_color.r, m_color.g, m_color.b));
    }
}

void Overlay::toast(std::string const& text, ccColor3B color) {
    auto pl = PlayLayer::get();
    if (!pl) return;
    if (auto old = pl->getChildByID("toast"_spr)) old->removeFromParent();

    auto win = CCDirector::get()->getWinSize();
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setID("toast"_spr);
    label->setScale(.45f);
    label->setColor(color);
    label->setOpacity(0);
    label->setPosition({ win.width / 2, win.height - 48.f });
    pl->addChild(label, 1001);

    label->runAction(CCSequence::create(
        CCFadeIn::create(.12f),
        CCDelayTime::create(.9f),
        CCFadeOut::create(.35f),
        CCRemoveSelf::create(),
        nullptr
    ));
}

} // namespace buffer

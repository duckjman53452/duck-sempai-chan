#include "Overlay.hpp"
#include "Buffer.hpp"
#include "Predict.hpp"
#include "TimingMap.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace buffer {

namespace {
constexpr size_t kMaxStoredLines = 8;

constexpr float kWidth = 168.f;
constexpr float kPadX = 9.f;
constexpr float kPadY = 5.f;
constexpr float kHeaderH = 13.f;
constexpr float kRowH = 10.f;
constexpr float kLineH = 9.f;
constexpr float kGap = 4.f;
constexpr float kMargin = 6.f;

CCLabelBMFont* smallLabel(std::string const& text, ccColor3B color, float maxScale) {
    auto label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
    label->setAnchorPoint({ 0.f, .5f });
    label->setColor(color);
    label->limitLabelWidth(kWidth - kPadX * 2, maxScale, .1f);
    return label;
}
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
    this->schedule(schedule_selector(Overlay::refreshDiag), .5f);
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

std::string Overlay::diagText() {
    auto const& d = Engine::get().diag();
    auto const& c = Predictor::get().counters();
    return fmt::format("in {} | {} tps | route {} | map {} look {} hold {} | sims {} ({} crash, {} ?)",
        d.inputs, m_tps, d.mapMatches, d.mapShifts, d.lookShifts, d.holdSaves, c.runs, c.crashes, c.unknown);
}

void Overlay::refreshDiag(float) {
    auto tick = Engine::get().tick();
    m_tps = tick >= m_lastTick ? static_cast<int>((tick - m_lastTick) * 2) : 0;
    m_lastTick = tick;
    if (m_diag) {
        m_diag->setString(diagText().c_str());
        m_diag->limitLabelWidth(kWidth - kPadX * 2, .42f, .1f);
    }
}

void Overlay::rebuild() {
    this->removeAllChildrenWithCleanup(true);
    m_chip = nullptr;
    m_diag = nullptr;
    m_accent = nullptr;

    auto& engine = Engine::get();
    auto mod = Mod::get();
    auto stage = engine.stage();
    bool off = stage == Stage::Off;
    bool showMap = mod->getSettingValue<bool>("map-enabled");
    bool showDiag = mod->getSettingValue<bool>("diagnostics");

    if (off && mod->getSettingValue<bool>("hide-when-off") && !showDiag) {
        this->setVisible(false);
        return;
    }
    this->setVisible(true);

    m_color = Engine::stageColor(stage);
    int wanted = off ? 0 : static_cast<int>(mod->getSettingValue<int64_t>("log-lines"));
    int shown = std::min<int>(wanted, static_cast<int>(m_lines.size()));

    int rows = (off ? 0 : 1) + (showMap ? 1 : 0) + (showDiag ? 1 : 0);
    float height = kPadY * 2 + kHeaderH + rows * kRowH;
    if (shown > 0) height += kGap + shown * kLineH;
    this->setContentSize({ kWidth, height });

    auto bg = CCScale9Sprite::create("square02_small.png");
    bg->setContentSize({ kWidth, height });
    bg->setAnchorPoint({ 0.f, 0.f });
    bg->setOpacity(off ? 90 : 120);
    this->addChild(bg, 0);

    m_accent = CCLayerColor::create({ m_color.r, m_color.g, m_color.b, 230 }, 2.f, height - 8.f);
    m_accent->setPosition({ 3.f, 4.f });
    this->addChild(m_accent, 1);

    // header
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

    float rowY = y - kHeaderH / 2 - kRowH / 2;
    auto addRow = [&](CCLabelBMFont* label) {
        label->setPosition({ kPadX, rowY });
        this->addChild(label, 2);
        rowY -= kRowH;
    };

    if (!off) {
        auto const& st = engine.stats();
        auto text = st.savesAttempt == 0
            ? fmt::format("no shifts this attempt   session {}", st.savesSession)
            : fmt::format("{} shift{} this attempt   best +{}t", st.savesAttempt, st.savesAttempt == 1 ? "" : "s", st.bestSave);
        addRow(smallLabel(text, { 190, 190, 205 }, .5f));
    }
    if (showMap) {
        auto const& map = TimingMap::get();
        addRow(smallLabel(map.statusText(), map.statusColor(), .5f));
    }
    if (showDiag) {
        m_diag = smallLabel(diagText(), { 150, 150, 170 }, .42f);
        addRow(m_diag);
    }

    if (shown > 0) {
        float lineTop = rowY + kRowH / 2 - kGap / 2;
        auto divider = CCLayerColor::create({ 255, 255, 255, 28 }, kWidth - kPadX * 2, 1.f);
        divider->setPosition({ kPadX, lineTop });
        this->addChild(divider, 2);

        for (int i = 0; i < shown; ++i) {
            auto const& line = m_lines[i];
            auto label = smallLabel(line.text, line.color, .45f);
            label->setPosition({ kPadX, lineTop - kGap / 2 - kLineH / 2 - i * kLineH });
            label->setOpacity(static_cast<GLubyte>(std::max(90, 255 - i * 40)));
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

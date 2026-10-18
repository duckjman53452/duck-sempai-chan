#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Buffer.hpp"
#include "Overlay.hpp"
#include "Predict.hpp"

using namespace geode::prelude;
using buffer::Engine;

// ------------------------------------------------------------------ input path
//
//   keyboard / mouse
//        |
//   GD input queue  ->  processCommands(step)            <- one call per physics step
//                            |  Engine::onTick            (resolve held clicks first)
//                            |  original processCommands
//                            |     -> handleButton        <- Engine::onButton may swallow a release
//                            v
//                       player update / collisions

class $modify(BufferBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto& engine = Engine::get();
        if (engine.handles(this, button) && !engine.onButton(this, down, button, isPlayer1)) {
            return;  // release is being held for the buffer window
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (PlayLayer::get() == static_cast<GJBaseGameLayer*>(this)) {
            // Half ticks are sub-steps inside a tick; count only whole ticks so
            // the window stays in real 240/s physics ticks.
            Engine::get().onTick(this, !isHalfTick);
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

// ------------------------------------------------------------------ level lifecycle

class $modify(BufferPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        Engine::get().onLevelExit();  // clean slate for the new level
        buffer::Predictor::get().attach(this);
        if (auto overlay = buffer::Overlay::create()) {
            overlay->setID("overlay"_spr);
            this->addChild(overlay, 1000);
        }
        return true;
    }

    void resetLevel() {
        Engine::get().onAttemptStart(this);
        PlayLayer::resetLevel();
    }

    void onQuit() {
        Engine::get().onLevelExit();
        buffer::Predictor::get().detach();
        PlayLayer::onQuit();
    }
};

// ------------------------------------------------------------------ pause menu controls

class $modify(BufferPauseLayer, PauseLayer) {
    struct Fields {
        ButtonSprite* stageSprite = nullptr;
    };

    void customSetup() {
        PauseLayer::customSetup();

        auto win = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("buffer-menu"_spr);
        menu->setPosition({ win.width - 74.f, 24.f });
        menu->setContentSize({ 130.f, 26.f });
        menu->setAnchorPoint({ .5f, .5f });
        menu->ignoreAnchorPointForPosition(false);

        auto stageSpr = ButtonSprite::create("Buffer: Off", "bigFont.fnt", "GJ_button_04.png", .8f);
        stageSpr->setScale(.45f);
        m_fields->stageSprite = stageSpr;
        auto stageBtn = CCMenuItemSpriteExtra::create(stageSpr, this, menu_selector(BufferPauseLayer::onCycleBuffer));
        stageBtn->setID("buffer-stage-button"_spr);

        auto gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
        gearSpr->setScale(.5f);
        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(BufferPauseLayer::onBufferSettings));
        gearBtn->setID("buffer-settings-button"_spr);

        menu->addChild(stageBtn);
        menu->addChild(gearBtn);
        menu->setLayout(RowLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::End));
        this->addChild(menu, 10);

        this->refreshBufferButton();
    }

    void refreshBufferButton() {
        auto spr = m_fields->stageSprite;
        if (!spr) return;
        auto stage = Engine::get().stage();
        auto text = stage == buffer::Stage::Off
            ? std::string("Buffer: Off")
            : fmt::format("Buffer: S{} / {}t", static_cast<int>(stage), Engine::windowFor(stage));
        spr->setString(text.c_str());
        if (spr->m_label) spr->m_label->setColor(Engine::stageColor(stage));
        if (auto menu = this->getChildByID("buffer-menu"_spr)) menu->updateLayout();
    }

    void onCycleBuffer(CCObject*) {
        Engine::get().cycleStage();
        this->refreshBufferButton();
    }

    void onBufferSettings(CCObject*) {
        geode::openSettingsPopup(Mod::get());
    }
};

// ------------------------------------------------------------------ hotkey

$on_game(Loaded) {
    listenForKeybindSettingPresses("cycle-keybind", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        if (!PlayLayer::get()) return;  // only while in a level
        Engine::get().cycleStage();
    });
}

// Keeps the look-ahead clones from changing the real game while they're simulated.
//
// Two kinds of hook:
//   - "abort": the clone hit something whose effect we don't model (orbs, pads,
//     portals, touch triggers). The run stops and its result counts as unknown,
//     so the buffer doesn't intervene.
//   - "swallow": the effect is irrelevant to survival (coins, item pickups,
//     breakable blocks, event triggers, auto-checkpoints). It's skipped and the
//     run continues.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include "Predict.hpp"

using namespace geode::prelude;
using buffer::Predictor;

namespace {
Predictor& sim() { return Predictor::get(); }
}

class $modify(SimBaseLayer, GJBaseGameLayer) {
    // ---- abort: effects we don't predict
    void playerTouchedRing(PlayerObject* p, RingObject* o) {
        if (sim().simulating()) return sim().reportAbort();
        GJBaseGameLayer::playerTouchedRing(p, o);
    }
    void playerTouchedTrigger(PlayerObject* p, EffectGameObject* o) {
        if (sim().simulating()) return sim().reportAbort();
        GJBaseGameLayer::playerTouchedTrigger(p, o);
    }
    void bumpPlayer(PlayerObject* p, EffectGameObject* o) {
        if (sim().simulating()) return sim().reportAbort();
        GJBaseGameLayer::bumpPlayer(p, o);
    }
    void playerWillSwitchMode(PlayerObject* p, GameObject* o) {
        if (sim().simulating()) return sim().reportAbort();
        GJBaseGameLayer::playerWillSwitchMode(p, o);
    }
    void teleportPlayer(TeleportPortalObject* o, PlayerObject* p) {
        if (sim().simulating()) return sim().reportAbort();
        GJBaseGameLayer::teleportPlayer(o, p);
    }
    void toggleDualMode(GameObject* o, bool dual, PlayerObject* p, bool noEffects) {
        if (sim().simulating()) return sim().reportAbort();
        GJBaseGameLayer::toggleDualMode(o, dual, p, noEffects);
    }

    // ---- gravity portals: apply to the clone only, skip the level-wide effects
    void flipGravity(PlayerObject* p, bool flip, bool noEffects) {
        if (sim().simulating()) {
            if (sim().isFake(p)) p->flipGravity(flip, true);
            return;
        }
        GJBaseGameLayer::flipGravity(p, flip, noEffects);
    }

    // ---- swallow: irrelevant to survival
    void pickupItem(EffectGameObject* o) {
        if (sim().simulating()) return;
        GJBaseGameLayer::pickupItem(o);
    }
    void destroyObject(GameObject* o) {
        if (sim().simulating()) return;
        GJBaseGameLayer::destroyObject(o);
    }
    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if (sim().simulating()) return;
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }
};

class $modify(SimPlayLayer, PlayLayer) {
    // A clone crashing is the signal we're looking for. It must never reach the real death path.
    void destroyPlayer(PlayerObject* p, GameObject* o) {
        if (sim().isFake(p)) {
            sim().reportDeath(p);
            return;
        }
        PlayLayer::destroyPlayer(p, o);
    }
};

class $modify(SimPlayer, PlayerObject) {
    void update(float dt) {
        // Remember the real physics step so clones advance by the same amount.
        if (!sim().simulating() && m_gameLayer && this == m_gameLayer->m_player1) {
            sim().noteRealStep(dt);
        }
        PlayerObject::update(dt);
    }

    void tryPlaceCheckpoint() {
        if (sim().isFake(this)) return;  // no practice-mode auto checkpoints from clones
        PlayerObject::tryPlaceCheckpoint();
    }
};

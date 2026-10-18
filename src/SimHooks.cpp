// Keeps the shadow clones from changing the real game while they're simulated.
//
//   live  = short look-ahead during play: anything we don't model aborts the run
//   scan  = full shadow run at level open: portals / orbs / pads run for real on
//           the clone (the level is reset afterwards), triggers stay frozen
//   both  = irrelevant or irreversible effects are always swallowed

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include "Predict.hpp"

using namespace geode::prelude;
using buffer::Predictor;
using buffer::SimMode;

namespace {
Predictor& sim() { return Predictor::get(); }
bool simulating() { return sim().simulating(); }
bool live() { return sim().simulating() && sim().mode() == SimMode::Live; }
}

class $modify(SimBaseLayer, GJBaseGameLayer) {
    // ---- live: abort / scan: run on the clone
    void playerTouchedRing(PlayerObject* p, RingObject* o) {
        if (live()) return sim().reportAbort();
        GJBaseGameLayer::playerTouchedRing(p, o);
    }
    void bumpPlayer(PlayerObject* p, EffectGameObject* o) {
        if (live()) return sim().reportAbort();
        GJBaseGameLayer::bumpPlayer(p, o);
    }
    void playerWillSwitchMode(PlayerObject* p, GameObject* o) {
        if (live()) return sim().reportAbort();
        GJBaseGameLayer::playerWillSwitchMode(p, o);
    }
    void teleportPlayer(TeleportPortalObject* o, PlayerObject* p) {
        if (live()) return sim().reportAbort();
        GJBaseGameLayer::teleportPlayer(o, p);
    }

    // ---- live: abort / scan: swallow (the shadow sees the level frozen at open)
    void playerTouchedTrigger(PlayerObject* p, EffectGameObject* o) {
        if (live()) return sim().reportAbort();
        if (simulating()) return;
        GJBaseGameLayer::playerTouchedTrigger(p, o);
    }

    // ---- always abort: dual needs a second shadow we don't run
    void toggleDualMode(GameObject* o, bool dual, PlayerObject* p, bool noEffects) {
        if (simulating()) return sim().reportAbort();
        GJBaseGameLayer::toggleDualMode(o, dual, p, noEffects);
    }

    // ---- always: clone only, no level-wide effects
    void flipGravity(PlayerObject* p, bool flip, bool noEffects) {
        if (simulating()) {
            if (sim().isFake(p)) p->flipGravity(flip, true);
            return;
        }
        GJBaseGameLayer::flipGravity(p, flip, noEffects);
    }
    void updateDualGround(PlayerObject* p, int mode, bool instant, float duration) {
        if (simulating()) return;
        GJBaseGameLayer::updateDualGround(p, mode, instant, duration);
    }

    // ---- always swallow: irrelevant to survival
    void pickupItem(EffectGameObject* o) {
        if (simulating()) return;
        GJBaseGameLayer::pickupItem(o);
    }
    void destroyObject(GameObject* o) {
        if (simulating()) return;
        GJBaseGameLayer::destroyObject(o);
    }
    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if (simulating()) return;
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
    void levelComplete() {
        if (simulating()) return;  // the shadow reaching the end is not a completion
        PlayLayer::levelComplete();
    }
    void playEndAnimationToPos(CCPoint pos) {
        if (simulating()) return;
        PlayLayer::playEndAnimationToPos(pos);
    }
};

class $modify(SimPlayer, PlayerObject) {
    void update(float dt) {
        // Remember the real physics step so clones advance by the same amount.
        if (!simulating() && m_gameLayer && this == m_gameLayer->m_player1) {
            sim().noteRealStep(dt);
        }
        PlayerObject::update(dt);
    }
    void playerDestroyed(bool noEffects) {
        if (sim().isFake(this)) {
            sim().reportDeath(this);
            return;
        }
        PlayerObject::playerDestroyed(noEffects);
    }
    void tryPlaceCheckpoint() {
        if (sim().isFake(this)) return;  // no practice-mode auto checkpoints from clones
        PlayerObject::tryPlaceCheckpoint();
    }
};

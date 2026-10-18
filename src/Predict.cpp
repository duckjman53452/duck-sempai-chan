#include "Predict.hpp"

using namespace geode::prelude;

namespace buffer {

Predictor& Predictor::get() {
    static Predictor instance;
    return instance;
}

void Predictor::attach(GJBaseGameLayer* layer) {
    // Old clones (if any) belonged to the previous layer and died with it.
    m_fakes = {};
    m_layer = layer;
    if (!layer || !layer->m_objectLayer) return;

    for (auto& fake : m_fakes) {
        fake = PlayerObject::create(1, 1, layer, layer->m_objectLayer, false);
        if (!fake) continue;
        fake->setID("sim-player"_spr);
        fake->setVisible(false);
        fake->m_playEffects = false;
        layer->m_objectLayer->addChild(fake, -100);
    }
}

void Predictor::detach() {
    m_fakes = {};
    m_layer = nullptr;
    m_simulating = false;
}

bool Predictor::ready(GJBaseGameLayer* layer) const {
    return layer && layer == m_layer && m_fakes[0] && m_fakes[1]
        && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == layer;
}

bool Predictor::isFake(PlayerObject* p) const {
    return p && (p == m_fakes[0] || p == m_fakes[1]);
}

void Predictor::reportDeath(PlayerObject* p) {
    if (isFake(p)) m_died = true;
}

void Predictor::sync(PlayerObject* fake, PlayerObject* real) {
    fake->copyAttributes(real);
    // Belt and braces for the fields the physics depends on most.
    fake->setPosition(real->getPosition());
    fake->m_yVelocity = real->m_yVelocity;
    fake->m_isUpsideDown = real->m_isUpsideDown;
    fake->m_isDead = false;
    fake->m_playEffects = false;
    fake->setVisible(false);
}

void Predictor::setHeld(PlayerObject* fake, bool held) {
    if (held) fake->pushButton(PlayerButton::Jump);
    else fake->releaseButton(PlayerButton::Jump);
}

bool Predictor::step(GJBaseGameLayer* layer, int count) {
    for (int s = 0; s < count; ++s) {
        for (size_t i = 0; i < m_fakes.size(); ++i) {
            if (!m_active[i]) continue;
            m_fakes[i]->update(m_dt);
            if (m_died || m_aborted) return false;
            layer->checkCollisions(m_fakes[i], m_dt, false);
            if (m_died || m_aborted) return false;
        }
    }
    return true;
}

SimResult Predictor::run(
    GJBaseGameLayer* layer,
    std::array<PlayerObject*, 2> const& real,
    bool before, bool after,
    int delay, int maxAfter
) {
    SimResult result;
    if (!ready(layer)) {
        result.aborted = true;
        return result;
    }

    m_simulating = true;
    m_died = false;
    m_aborted = false;

    for (size_t i = 0; i < m_fakes.size(); ++i) {
        m_active[i] = real[i] != nullptr;
        if (!m_active[i]) continue;
        sync(m_fakes[i], real[i]);
        setHeld(m_fakes[i], before);
    }

    [&] {
        // Phase 1: the old input state, for `delay` steps.
        for (int s = 0; s < delay; ++s) {
            if (!step(layer, 1)) {
                result.beforeSurvived = s;
                result.diedBefore = m_died;
                result.aborted = m_aborted;
                return;
            }
        }
        result.beforeSurvived = delay;

        // Phase 2: the new input state.
        for (size_t i = 0; i < m_fakes.size(); ++i) {
            if (m_active[i]) setHeld(m_fakes[i], after);
        }
        for (int s = 0; s < maxAfter; ++s) {
            if (!step(layer, 1)) {
                result.aborted = m_aborted;
                return;
            }
            result.afterSurvived = s + 1;
        }
    }();

    for (size_t i = 0; i < m_fakes.size(); ++i) {
        if (!m_active[i]) continue;
        m_fakes[i]->releaseButton(PlayerButton::Jump);
        m_fakes[i]->resetStreak();
        m_fakes[i]->setVisible(false);
    }
    m_simulating = false;
    return result;
}

} // namespace buffer

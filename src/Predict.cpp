#include "Predict.hpp"

using namespace geode::prelude;

namespace buffer {

Predictor& Predictor::get() {
    static Predictor instance;
    return instance;
}

void Predictor::attach(GJBaseGameLayer* layer) {
    if (layer && layer == m_layer && m_fakes[0] && m_fakes[0]->getParent()) return;

    // Old clones (if any) belonged to the previous layer and died with it.
    m_fakes = {};
    m_layer = layer;
    if (!layer || !layer->m_objectLayer) return;

    for (auto& fake : m_fakes) {
        fake = PlayerObject::create(1, 1, layer, layer->m_objectLayer, false);
        if (!fake) continue;
        fake->setID("shadow-player"_spr);
        fake->setVisible(false);
        fake->m_playEffects = false;
        layer->m_objectLayer->addChild(fake, -100);
    }
    if (!m_cp) {
        m_cp = PlayerCheckpoint::create();
        m_cp->retain();  // kept for the whole session
    }
}

void Predictor::detach() {
    m_fakes = {};
    m_layer = nullptr;
    m_simulating = false;
    m_mode = SimMode::Live;
}

bool Predictor::ready(GJBaseGameLayer* layer) const {
    return layer && layer == m_layer && m_fakes[0] && m_fakes[1] && m_cp
        && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == layer;
}

bool Predictor::isFake(PlayerObject* p) const {
    return p && (p == m_fakes[0] || p == m_fakes[1]);
}

void Predictor::reportDeath(PlayerObject* p) {
    if (isFake(p)) m_died = true;
}

// ---------------------------------------------------------------- state

void Predictor::setHeldRaw(PlayerObject* fake, bool held) {
    fake->m_holdingButtons[1] = held;  // PlayerButton::Jump
}

void Predictor::press(PlayerObject* fake, bool down) {
    if (down) fake->pushButton(PlayerButton::Jump);
    else fake->releaseButton(PlayerButton::Jump);
}

void Predictor::loadState(PlayerObject* fake, PlayerCheckpoint* cp, bool held) {
    fake->loadFromCheckpoint(cp);
    setHeldRaw(fake, held);
    fake->m_isDead = false;
    fake->m_playEffects = false;
    fake->setVisible(false);
}

void Predictor::syncFromReal(PlayerObject* fake, PlayerObject* real) {
    real->saveToCheckpoint(m_cp);
    fake->loadFromCheckpoint(m_cp);
    setHeldRaw(fake, real->m_holdingButtons[1]);
    fake->m_isDead = false;
    fake->m_playEffects = false;
    fake->setVisible(false);
}

bool Predictor::stepClone(GJBaseGameLayer* layer, PlayerObject* fake) {
    fake->update(m_dt);
    if (fake->m_isDead) m_died = true;
    if (m_died || m_aborted) return false;

    layer->checkCollisions(fake, m_dt, false);
    if (fake->m_isDead) m_died = true;
    if (m_died || m_aborted) return false;
    return true;
}

// ---------------------------------------------------------------- scan mode

void Predictor::beginScan() {
    m_simulating = true;
    m_mode = SimMode::Scan;
    clearFlags();
}

void Predictor::endScan() {
    for (auto fake : m_fakes) {
        if (!fake) continue;
        setHeldRaw(fake, false);
        fake->resetStreak();
        fake->setVisible(false);
    }
    m_simulating = false;
    m_mode = SimMode::Live;
}

// ---------------------------------------------------------------- live look-ahead

SimResult Predictor::run(
    GJBaseGameLayer* layer,
    std::array<PlayerObject*, 2> const& real,
    bool before, bool after,
    int delay, int maxAfter,
    std::vector<CCPoint>* trace
) {
    SimResult result;
    if (!ready(layer)) {
        result.aborted = true;
        return result;
    }

    m_simulating = true;
    m_mode = SimMode::Live;
    clearFlags();
    m_counters.runs++;

    for (size_t i = 0; i < m_fakes.size(); ++i) {
        m_active[i] = real[i] != nullptr;
        if (!m_active[i]) continue;
        syncFromReal(m_fakes[i], real[i]);
        setHeldRaw(m_fakes[i], before);
    }
    if (trace) trace->push_back(m_fakes[0]->getPosition());

    auto stepAll = [&]() {
        for (size_t i = 0; i < m_fakes.size(); ++i) {
            if (m_active[i] && !stepClone(layer, m_fakes[i])) return false;
        }
        if (trace) trace->push_back(m_fakes[0]->getPosition());
        return true;
    };

    [&] {
        // Phase 1: the old input state, for `delay` steps.
        for (int s = 0; s < delay; ++s) {
            if (!stepAll()) {
                result.beforeSurvived = s;
                result.diedBefore = m_died;
                result.aborted = m_aborted;
                return;
            }
        }
        result.beforeSurvived = delay;

        // Phase 2: the new input state (a real click / release on the clone).
        for (size_t i = 0; i < m_fakes.size(); ++i) {
            if (m_active[i]) press(m_fakes[i], after);
        }
        for (int s = 0; s < maxAfter; ++s) {
            if (!stepAll()) {
                result.aborted = m_aborted;
                return;
            }
            result.afterSurvived = s + 1;
        }
    }();

    if (m_died) m_counters.crashes++;
    if (m_aborted) m_counters.unknown++;

    for (size_t i = 0; i < m_fakes.size(); ++i) {
        if (!m_active[i]) continue;
        setHeldRaw(m_fakes[i], false);
        m_fakes[i]->resetStreak();
        m_fakes[i]->setVisible(false);
    }
    m_simulating = false;
    return result;
}

} // namespace buffer

#include "Visuals.hpp"
#include "TimingMap.hpp"

#include <cmath>

using namespace geode::prelude;

namespace buffer::visuals {

namespace {

constexpr int kChunkTicks = 240;
constexpr int kZRoute = 900;
constexpr int kZMarks = 950;
constexpr float kMarkLife = 1.4f;

std::vector<std::pair<float, float>> s_chunkRanges;  // x-range of each route chunk

ccColor4F rgba(ccColor3B c, float a) {
    return { c.r / 255.f, c.g / 255.f, c.b / 255.f, a };
}

CCNode* objectLayer(GJBaseGameLayer* layer) {
    return layer ? layer->m_objectLayer : nullptr;
}

void ring(CCDrawNode* node, CCPoint center, float radius, ccColor4F color) {
    constexpr int kSides = 18;
    CCPoint verts[kSides];
    for (int i = 0; i < kSides; ++i) {
        float a = 6.2831853f * i / kSides;
        verts[i] = CCPoint(center.x + radius * std::cos(a), center.y + radius * std::sin(a));
    }
    node->drawPolygon(verts, kSides, { 0.f, 0.f, 0.f, 0.f }, .9f, color);
}

void expire(CCNode* node, float after) {
    node->runAction(CCSequence::create(CCDelayTime::create(after), CCRemoveSelf::create(), nullptr));
}

} // namespace

// ---------------------------------------------------------------- shadow route

void clearRoute(GJBaseGameLayer* layer) {
    s_chunkRanges.clear();
    if (auto parent = objectLayer(layer)) {
        if (auto old = parent->getChildByID("shadow-route"_spr)) old->removeFromParent();
    }
}

void buildRoute(GJBaseGameLayer* layer) {
    clearRoute(layer);
    auto parent = objectLayer(layer);
    auto const& map = TimingMap::get();
    if (!parent || !map.usable()) return;
    if (!Mod::get()->getSettingValue<bool>("show-shadow-path")) return;

    auto const& path = map.path();
    auto const& events = map.events();

    auto root = CCNode::create();
    root->setID("shadow-route"_spr);
    parent->addChild(root, kZRoute);

    ccColor4F heldCol = { .35f, .85f, 1.f, .55f };
    ccColor4F freeCol = { 1.f, 1.f, 1.f, .28f };
    ccColor4F windowCol = { .45f, 1.f, .55f, .5f };
    ccColor4F pressCol = { .35f, .85f, 1.f, .95f };
    ccColor4F releaseCol = { 1.f, .45f, .9f, .95f };

    int n = static_cast<int>(path.size());
    size_t ev = 0;
    for (int start = 0; start < n; start += kChunkTicks) {
        int end = std::min(n - 1, start + kChunkTicks);
        auto node = CCDrawNode::create();
        root->addChild(node);
        s_chunkRanges.emplace_back(path[start].x, path[end].x);

        // path line, every 2 ticks
        for (int t = start; t + 2 <= end; t += 2) {
            auto const& a = path[t];
            auto const& b = path[t + 2];
            node->drawSegment({ a.x, a.y }, { b.x, b.y }, .55f, a.held ? heldCol : freeCol);
        }

        // inputs + their safe windows
        for (; ev < events.size() && events[ev].tick <= end; ++ev) {
            auto const& e = events[ev];
            if (e.tick < 0 || e.tick >= n) continue;
            if (e.windowKnown && (e.earliest < 0 || e.latest > 0)) {
                int lo = std::clamp(e.tick + e.earliest, 0, n - 1);
                int hi = std::clamp(e.tick + e.latest, 0, n - 1);
                for (int t = lo; t < hi; ++t) {
                    node->drawSegment({ path[t].x, path[t].y }, { path[t + 1].x, path[t + 1].y }, 1.6f, windowCol);
                }
            }
            auto const& p = path[e.tick];
            node->drawDot({ p.x, p.y }, 2.3f, e.down ? pressCol : releaseCol);
        }
    }
    cullRoute(layer);
}

void cullRoute(GJBaseGameLayer* layer) {
    auto parent = objectLayer(layer);
    if (!parent || !layer->m_player1) return;
    auto root = parent->getChildByID("shadow-route"_spr);
    if (!root) return;

    float px = layer->m_player1->getPositionX();
    auto children = root->getChildren();
    if (!children) return;
    for (unsigned i = 0; i < children->count() && i < s_chunkRanges.size(); ++i) {
        auto [x0, x1] = s_chunkRanges[i];
        bool near = x1 > px - 700.f && x0 < px + 1000.f;
        static_cast<CCNode*>(children->objectAtIndex(i))->setVisible(near);
    }
}

// ---------------------------------------------------------------- frame-shift markers

void markInput(GJBaseGameLayer* layer, CCPoint at, ccColor3B color) {
    auto parent = objectLayer(layer);
    if (!parent || !Mod::get()->getSettingValue<bool>("show-shifts")) return;

    auto node = CCDrawNode::create();
    ring(node, at, 5.5f, rgba(color, .95f));
    parent->addChild(node, kZMarks);
    expire(node, kMarkLife);
}

void markApplied(GJBaseGameLayer* layer, CCPoint from, CCPoint to, int ticks, ccColor3B color) {
    auto parent = objectLayer(layer);
    if (!parent || !Mod::get()->getSettingValue<bool>("show-shifts")) return;

    auto node = CCDrawNode::create();
    node->drawSegment(from, to, .8f, rgba(color, .8f));
    node->drawDot(to, 3.2f, rgba(color, 1.f));
    parent->addChild(node, kZMarks);
    expire(node, kMarkLife);

    auto label = CCLabelBMFont::create(fmt::format("+{}t", ticks).c_str(), "bigFont.fnt");
    label->setScale(.32f);
    label->setColor(color);
    label->setPosition({ to.x, to.y + 14.f });
    parent->addChild(label, kZMarks + 1);
    label->runAction(CCSequence::create(
        CCSpawn::create(CCMoveBy::create(kMarkLife, { 0.f, 14.f }), CCFadeOut::create(kMarkLife), nullptr),
        CCRemoveSelf::create(),
        nullptr
    ));
}

// ---------------------------------------------------------------- look-ahead traces

void drawTrace(GJBaseGameLayer* layer, std::vector<CCPoint> const& points, ccColor4F color, bool crossAtEnd) {
    auto parent = objectLayer(layer);
    if (!parent || points.size() < 2 || !Mod::get()->getSettingValue<bool>("show-lookahead")) return;

    auto node = CCDrawNode::create();
    for (size_t i = 1; i < points.size(); ++i) {
        node->drawSegment(points[i - 1], points[i], .7f, color);
    }
    if (crossAtEnd) {
        auto c = points.back();
        constexpr float r = 4.f;
        node->drawSegment({ c.x - r, c.y - r }, { c.x + r, c.y + r }, .9f, color);
        node->drawSegment({ c.x - r, c.y + r }, { c.x + r, c.y - r }, .9f, color);
    }
    parent->addChild(node, kZMarks - 1);
    expire(node, kMarkLife);
}

} // namespace buffer::visuals

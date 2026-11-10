#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <algorithm>
#include <deque>

using namespace geode::prelude;

// Rhythm-game style click indicator: a tapered "highway" with a baseline and
// chevrons. Each click spawns a note that scrolls up the track. No sounds.

namespace {
    constexpr float kHeight     = 170.f; // track height
    constexpr float kHalfBottom = 34.f;  // half width at baseline
    constexpr float kHalfTop    = 24.f;  // half width at top (perspective taper)
    constexpr float kBarThick   = 6.f;   // wide bar of each note
    constexpr float kMinBody    = 10.f;  // min length of the note body

    float halfWidthAt(float y) {
        float t = std::clamp(y / kHeight, 0.f, 1.f);
        return kHalfBottom + (kHalfTop - kHalfBottom) * t;
    }

    ccColor4F rgba(int r, int g, int b, float a) {
        return { r / 255.f, g / 255.f, b / 255.f, a };
    }

    float getFloat(char const* key) {
        return static_cast<float>(Mod::get()->getSettingValue<double>(key));
    }
}

struct Note {
    double press;
    double release = -1.0; // -1 while still held
};

class ClickIndicator : public CCNode {
    CCDrawNode* m_draw = nullptr;
    CCLabelBMFont* m_perfect = nullptr;
    std::deque<Note> m_notes;
    double m_time = 0.0;
    bool m_held = false;
    float m_alpha = 1.f;
    float m_speed = 140.f;

public:
    static ClickIndicator* create() {
        auto ret = new ClickIndicator();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        m_alpha = getFloat("opacity");
        m_speed = getFloat("scroll-speed");

        m_draw = CCDrawNode::create();
        this->addChild(m_draw);

        if (Mod::get()->getSettingValue<bool>("show-perfect")) {
            m_perfect = CCLabelBMFont::create("PERFECT", "bigFont.fnt");
            m_perfect->setColor({ 90, 255, 110 });
            m_perfect->setScale(0.55f);
            m_perfect->setPosition({ 0.f, kHeight + 22.f });
            m_perfect->setOpacity(0); // appears on first click
            this->addChild(m_perfect, 2);
        }

        this->setID("click-indicator"_spr);
        this->scheduleUpdate();
        this->redraw();
        return true;
    }

    void press() {
        if (m_held) return;
        m_held = true;
        m_notes.push_back({ m_time });

        if (m_perfect) {
            m_perfect->stopAllActions();
            m_perfect->setOpacity(static_cast<GLubyte>(255 * m_alpha));
            m_perfect->setScale(0.68f);
            m_perfect->runAction(CCEaseOut::create(CCScaleTo::create(0.12f, 0.55f), 2.f));
        }
    }

    void release() {
        if (!m_held) return;
        m_held = false;
        if (!m_notes.empty() && m_notes.back().release < 0)
            m_notes.back().release = m_time;
    }

    void update(float dt) override {
        m_time += dt;
        // drop notes that have scrolled off the top
        while (!m_notes.empty() && m_notes.front().release >= 0 &&
               (m_time - m_notes.front().release) * m_speed > kHeight) {
            m_notes.pop_front();
        }
        this->redraw();
    }

private:
    // trapezoid slice of the track between y0 and y1, `frac` = fraction of track width
    void quad(float y0, float y1, float frac, ccColor4F fill, float border = 0.f,
              ccColor4F borderCol = { 0, 0, 0, 0 }) {
        if (y1 <= y0) return;
        CCPoint v[4] = {
            { -frac * halfWidthAt(y0), y0 },
            {  frac * halfWidthAt(y0), y0 },
            {  frac * halfWidthAt(y1), y1 },
            { -frac * halfWidthAt(y1), y1 },
        };
        m_draw->drawPolygon(v, 4, fill, border, borderCol);
    }

    void redraw() {
        m_draw->clear();
        float a = m_alpha;

        auto green    = rgba(60, 235, 95, a);
        auto gray     = rgba(170, 170, 170, 0.9f * a);
        auto baseline = rgba(215, 232, 240, a);

        // glow under the baseline while held
        if (m_held) {
            quad(-26.f, 0.f, 0.75f, rgba(60, 235, 95, 0.2f * a));
        }

        // notes
        for (auto const& n : m_notes) {
            bool held = n.release < 0;
            float yPress = static_cast<float>((m_time - n.press) * m_speed);
            float yRel   = held ? 0.f : static_cast<float>((m_time - n.release) * m_speed);

            float barTop  = yRel + kBarThick;
            float bodyTop = std::max(yPress, barTop + kMinBody);

            auto barCol    = held ? rgba(70, 240, 100, a)        : rgba(255, 186, 45, a);
            auto bodyCol   = held ? rgba(70, 240, 100, 0.55f * a) : rgba(230, 150, 40, 0.5f * a);
            auto barBorder = held ? rgba(30, 150, 60, a)         : rgba(200, 120, 20, a);

            if (barTop < kHeight)
                quad(barTop, std::min(bodyTop, kHeight), 0.42f, bodyCol);
            if (yRel < kHeight)
                quad(yRel, std::min(barTop, kHeight), 0.98f, barCol, 0.6f, barBorder);
        }

        // rails (tapered ladder sides)
        m_draw->drawSegment({ -kHalfBottom, 0.f }, { -kHalfTop, kHeight }, 1.8f, gray);
        m_draw->drawSegment({  kHalfBottom, 0.f }, {  kHalfTop, kHeight }, 1.8f, gray);

        // baseline
        m_draw->drawSegment({ -kHalfBottom - 3.f, 0.f }, { kHalfBottom + 3.f, 0.f }, 2.4f, baseline);

        // chevrons: ">" on the left, "<" on the right, green while held
        auto chev = m_held ? green : gray;
        float tipL = -kHalfBottom - 6.f, outL = -kHalfBottom - 22.f;
        m_draw->drawSegment({ outL,  16.f }, { tipL, 0.f }, 2.6f, chev);
        m_draw->drawSegment({ outL, -16.f }, { tipL, 0.f }, 2.6f, chev);
        m_draw->drawSegment({ -outL,  16.f }, { -tipL, 0.f }, 2.6f, chev);
        m_draw->drawSegment({ -outL, -16.f }, { -tipL, 0.f }, 2.6f, chev);
    }
};

class $modify(CIPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (!Mod::get()->getSettingValue<bool>("enabled")) return true;

        auto indicator = ClickIndicator::create();
        auto win = CCDirector::get()->getWinSize();
        indicator->setPosition({ win.width * getFloat("pos-x"), win.height * getFloat("pos-y") });
        indicator->setScale(getFloat("scale"));
        this->addChild(indicator, 1000);
        return true;
    }
};

class $modify(CIGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        auto pl = PlayLayer::get();
        if (!pl || button != 1 || !isPlayer1) return; // jump only, P1, not in editor

        if (auto ind = typeinfo_cast<ClickIndicator*>(pl->getChildByID("click-indicator"_spr))) {
            down ? ind->press() : ind->release();
        }

        // small "PERFECT" that floats up from the player (silent)
        if (down && Mod::get()->getSettingValue<bool>("floating-perfect") && m_player1 && m_objectLayer) {
            auto lbl = CCLabelBMFont::create("PERFECT", "bigFont.fnt");
            lbl->setColor({ 70, 200, 90 });
            lbl->setScale(0.4f);
            lbl->setOpacity(220);
            lbl->setPosition(m_player1->getPosition() + CCPoint{ 0.f, 25.f });
            m_objectLayer->addChild(lbl, 9999);
            lbl->runAction(CCSequence::create(
                CCSpawn::createWithTwoActions(
                    CCMoveBy::create(0.6f, { 0.f, 20.f }),
                    CCFadeOut::create(0.6f)
                ),
                CCRemoveSelf::create(),
                nullptr
            ));
        }
    }
};

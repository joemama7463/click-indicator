#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace geode::prelude;

// Click Guide: notes come down a rhythm-game track and reach the baseline
// exactly when you should click. The route for a level is recorded from your
// own completion (practice mode works: inputs after a checkpoint respawn are
// rewound). Timing uses the level clock, so it lines up with the level itself.
// No sound effects.

namespace {
    constexpr float kHeight     = 170.f;
    constexpr float kHalfBottom = 34.f;
    constexpr float kHalfTop    = 24.f;
    constexpr float kBarThick   = 6.f;
    constexpr float kBelow      = 22.f; // how far notes travel past the baseline

    float halfWidthAt(float y) {
        float t = std::clamp(y / kHeight, 0.f, 1.f);
        return kHalfBottom + (kHalfTop - kHalfBottom) * t;
    }
    ccColor4F rgba(int r, int g, int b, float a) { return { r / 255.f, g / 255.f, b / 255.f, a }; }
    float getFloat(char const* key) { return static_cast<float>(Mod::get()->getSettingValue<double>(key)); }
    bool getBool(char const* key) { return Mod::get()->getSettingValue<bool>(key); }

    std::filesystem::path routePath(GJGameLevel* level) {
        auto dir = Mod::get()->getSaveDir() / "routes";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        int id = level->m_levelID.value();
        std::string key = id > 0
            ? std::to_string(id)
            : fmt::format("local_{}", std::hash<std::string>{}(std::string(level->m_levelName.c_str())));
        return dir / (key + ".txt");
    }
}

enum class NoteState { Pending, Perfect, Great, Missed };

struct Note {
    double press;
    double release;
    NoteState state = NoteState::Pending;
    double judgedAt = 0.0;
};

struct Input {
    double press;
    double release = -1.0; // -1 while held
};

class ClickGuide : public CCNode {
    CCDrawNode* m_draw = nullptr;
    CCLabelBMFont* m_judge = nullptr;
    CCLabelBMFont* m_status = nullptr;

    std::vector<Note> m_route;
    std::vector<Input> m_rec;
    std::filesystem::path m_path;

    double m_now = 0.0;
    bool m_held = false;
    float m_alpha = 1.f;
    float m_approach = 1.f;   // seconds a note takes to travel the track
    double m_perfect = 0.035; // seconds
    double m_great = 0.08;

public:
    static ClickGuide* create(GJGameLevel* level) {
        auto ret = new ClickGuide();
        if (ret->init(level)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(GJGameLevel* level) {
        if (!CCNode::init()) return false;
        m_alpha    = getFloat("opacity");
        m_approach = getFloat("approach-time");
        m_perfect  = getFloat("perfect-window") / 1000.0;
        m_great    = std::max(m_perfect, getFloat("great-window") / 1000.0);

        m_path = routePath(level);
        this->loadRoute();

        m_draw = CCDrawNode::create();
        this->addChild(m_draw);

        m_judge = CCLabelBMFont::create("PERFECT", "bigFont.fnt");
        m_judge->setScale(0.55f);
        m_judge->setPosition({ 0.f, kHeight + 22.f });
        m_judge->setOpacity(0);
        m_judge->setVisible(getBool("show-judgement"));
        this->addChild(m_judge, 2);

        m_status = CCLabelBMFont::create("", "bigFont.fnt");
        m_status->setScale(0.3f);
        m_status->setPosition({ 0.f, -42.f });
        this->addChild(m_status, 2);
        this->updateStatus();

        this->setID("click-guide"_spr);
        this->scheduleUpdate();
        return true;
    }

    // ---- input from the game ----

    void press(double t) {
        if (m_held) return;
        m_held = true;
        m_rec.push_back({ t });

        // judge against the closest pending note inside the window
        Note* best = nullptr;
        double bestDiff = m_great + 1.0;
        for (auto& n : m_route) {
            if (n.press > t + m_great) break;
            if (n.state != NoteState::Pending) continue;
            double d = std::abs(n.press - t);
            if (d <= m_great && d < bestDiff) { best = &n; bestDiff = d; }
        }
        if (best) {
            best->state = bestDiff <= m_perfect ? NoteState::Perfect : NoteState::Great;
            best->judgedAt = t;
            this->showJudgement(best->state);
        }
    }

    void release(double t) {
        if (!m_held) return;
        m_held = false;
        if (!m_rec.empty() && m_rec.back().release < 0) m_rec.back().release = t;
    }

    // called after a death/restart; t = level time we respawned at
    void onReset(double t) {
        // rewind the recording (practice checkpoints keep everything before t)
        m_rec.erase(std::remove_if(m_rec.begin(), m_rec.end(),
            [t](Input const& i) { return i.press >= t; }), m_rec.end());
        for (auto& i : m_rec) {
            if (i.release < 0 || i.release > t) i.release = t;
        }
        m_held = false;

        // notes ahead of the respawn point can be hit again
        for (auto& n : m_route) {
            if (n.press >= t - m_great) n.state = NoteState::Pending;
        }
        m_now = t;
    }

    void onComplete(double t, bool validRun) {
        if (!m_rec.empty() && m_rec.back().release < 0) m_rec.back().release = t;
        if (!validRun || m_rec.empty()) return;
        if (!m_route.empty() && !getBool("overwrite-route")) return;

        std::ofstream out(m_path, std::ios::trunc);
        if (!out) return;
        for (auto const& i : m_rec) {
            out << fmt::format("{:.6f} {:.6f}\n", i.press, std::max(i.release, i.press));
        }
        out.close();

        this->loadRoute();
        m_status->setString("ROUTE SAVED");
        m_status->setColor({ 90, 255, 110 });
        m_status->setOpacity(static_cast<GLubyte>(255 * m_alpha));
    }

    void update(float) override {
        if (auto pl = PlayLayer::get()) m_now = pl->m_gameState.m_levelTime;

        for (auto& n : m_route) {
            if (n.press > m_now) break;
            if (n.state == NoteState::Pending && m_now - n.press > m_great) {
                n.state = NoteState::Missed;
                n.judgedAt = m_now;
                this->showJudgement(NoteState::Missed);
            }
        }
        this->redraw();
    }

private:
    void loadRoute() {
        m_route.clear();
        std::ifstream in(m_path);
        double p, r;
        while (in >> p >> r) m_route.push_back({ p, std::max(r, p) });
        std::sort(m_route.begin(), m_route.end(),
            [](Note const& a, Note const& b) { return a.press < b.press; });
    }

    void updateStatus() {
        if (m_route.empty()) {
            m_status->setString("NO ROUTE YET - BEAT THE LEVEL TO RECORD ONE");
            m_status->setColor({ 200, 200, 200 });
            m_status->setOpacity(static_cast<GLubyte>(200 * m_alpha));
        } else {
            m_status->setOpacity(0);
        }
    }

    void showJudgement(NoteState s) {
        char const* text = "PERFECT";
        ccColor3B color = { 90, 255, 110 };
        if (s == NoteState::Great) { text = "GREAT"; color = { 255, 220, 70 }; }
        if (s == NoteState::Missed) { text = "MISS"; color = { 255, 80, 80 }; }

        if (m_judge->isVisible()) {
            m_judge->setString(text);
            m_judge->setColor(color);
            m_judge->stopAllActions();
            m_judge->setOpacity(static_cast<GLubyte>(255 * m_alpha));
            m_judge->setScale(0.68f);
            m_judge->runAction(CCEaseOut::create(CCScaleTo::create(0.12f, 0.55f), 2.f));
        }

        // small floating copy above the player
        auto pl = PlayLayer::get();
        if (getBool("floating-judgement") && pl && pl->m_player1 && pl->m_objectLayer) {
            auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
            lbl->setColor(color);
            lbl->setScale(0.4f);
            lbl->setOpacity(220);
            lbl->setPosition(pl->m_player1->getPosition() + CCPoint{ 0.f, 25.f });
            pl->m_objectLayer->addChild(lbl, 9999);
            lbl->runAction(CCSequence::create(
                CCSpawn::createWithTwoActions(CCMoveBy::create(0.6f, { 0.f, 20.f }), CCFadeOut::create(0.6f)),
                CCRemoveSelf::create(),
                nullptr
            ));
        }
    }

    void quad(float y0, float y1, float frac, ccColor4F fill, float border = 0.f,
              ccColor4F borderCol = { 0, 0, 0, 0 }) {
        y0 = std::max(y0, -kBelow);
        y1 = std::min(y1, kHeight);
        if (y1 <= y0) return;
        CCPoint v[4] = {
            { -frac * halfWidthAt(y0), y0 },
            {  frac * halfWidthAt(y0), y0 },
            {  frac * halfWidthAt(y1), y1 },
            { -frac * halfWidthAt(y1), y1 },
        };
        m_draw->drawPolygon(v, 4, fill, border, borderCol);
    }

    float yFor(double t) const {
        return static_cast<float>((t - m_now) / m_approach) * kHeight;
    }

    void redraw() {
        m_draw->clear();
        float a = m_alpha;
        auto green = rgba(60, 235, 95, a);
        auto gray  = rgba(170, 170, 170, 0.9f * a);

        if (m_held) quad(-26.f, 0.f, 0.75f, rgba(60, 235, 95, 0.2f * a));

        double windowStart = m_now - (kBelow / kHeight) * m_approach;
        double windowEnd   = m_now + m_approach;

        for (auto const& n : m_route) {
            if (n.press > windowEnd) break;
            if (n.release < windowStart && n.press < windowStart) continue;

            float alphaMul = 1.f;
            ccColor4F bar, body, edge;
            switch (n.state) {
                case NoteState::Pending:
                    bar  = rgba(255, 186, 45, a);
                    body = rgba(230, 150, 40, 0.5f * a);
                    edge = rgba(200, 120, 20, a);
                    break;
                case NoteState::Perfect:
                case NoteState::Great: {
                    // hit notes flash green and fade out quickly
                    alphaMul = 1.f - static_cast<float>((m_now - n.judgedAt) / 0.15);
                    if (alphaMul <= 0.f) continue;
                    bar  = rgba(70, 240, 100, a * alphaMul);
                    body = rgba(70, 240, 100, 0.5f * a * alphaMul);
                    edge = rgba(30, 150, 60, a * alphaMul);
                    break;
                }
                case NoteState::Missed:
                    bar  = rgba(150, 90, 90, 0.8f * a);
                    body = rgba(150, 90, 90, 0.35f * a);
                    edge = rgba(110, 60, 60, 0.8f * a);
                    break;
            }

            float y0 = yFor(n.press);
            float y1 = yFor(n.release);
            // body for holds, then the bar marking the moment to click
            quad(y0 + kBarThick, std::max(y1, y0 + kBarThick), 0.42f, body);
            quad(y0, y0 + kBarThick, 0.98f, bar, 0.6f, edge);
        }

        // track
        m_draw->drawSegment({ -kHalfBottom, 0.f }, { -kHalfTop, kHeight }, 1.8f, gray);
        m_draw->drawSegment({  kHalfBottom, 0.f }, {  kHalfTop, kHeight }, 1.8f, gray);
        m_draw->drawSegment({ -kHalfBottom - 3.f, 0.f }, { kHalfBottom + 3.f, 0.f }, 2.4f, rgba(215, 232, 240, a));

        auto chev = m_held ? green : gray;
        float tipL = -kHalfBottom - 6.f, outL = -kHalfBottom - 22.f;
        m_draw->drawSegment({ outL,  16.f }, { tipL, 0.f }, 2.6f, chev);
        m_draw->drawSegment({ outL, -16.f }, { tipL, 0.f }, 2.6f, chev);
        m_draw->drawSegment({ -outL,  16.f }, { -tipL, 0.f }, 2.6f, chev);
        m_draw->drawSegment({ -outL, -16.f }, { -tipL, 0.f }, 2.6f, chev);
    }
};

static ClickGuide* getGuide() {
    if (auto pl = PlayLayer::get())
        return typeinfo_cast<ClickGuide*>(pl->getChildByID("click-guide"_spr));
    return nullptr;
}

class $modify(CGPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (!getBool("enabled")) return true;

        auto guide = ClickGuide::create(level);
        auto win = CCDirector::get()->getWinSize();
        guide->setPosition({ win.width * getFloat("pos-x"), win.height * getFloat("pos-y") });
        guide->setScale(getFloat("scale"));
        this->addChild(guide, 1000);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (auto g = getGuide()) g->onReset(m_gameState.m_levelTime);
    }

    void levelComplete() {
        // only runs from 0% (no start position) count as a route
        if (auto g = getGuide()) g->onComplete(m_gameState.m_levelTime, !m_isTestMode);
        PlayLayer::levelComplete();
    }
};

class $modify(CGGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (button != 1 || !isPlayer1) return;
        if (auto g = getGuide()) {
            double t = m_gameState.m_levelTime;
            down ? g->press(t) : g->release(t);
        }
    }
};

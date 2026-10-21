#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/HardStreak.hpp>

#include <algorithm>
#include <cstdint>

using namespace geode::prelude;

class TrajectoryBufferSim {
private:
    bool m_simulating = false;
    bool m_firingBuffered = false;
    int m_appliedStage = -1;
    bool m_enabled = false;

public:
    void applySettings() {
        int stage = static_cast<int>(
            Mod::get()->getSettingValue<int64_t>("stage")
        );

        stage = std::clamp(stage, 0, 4);

        if (stage == m_appliedStage) {
            return;
        }

        m_appliedStage = stage;

        // Diagnostic build: all buffering is temporarily disabled.
        m_enabled = false;

        log::info(
            "[InputBuffer] Stage {} selected; diagnostic buffering disabled",
            stage
        );
    }

    void init() {
        m_simulating = false;
        m_firingBuffered = false;
        m_appliedStage = -1;
        applySettings();
    }

    void quit() {
        m_simulating = false;
        m_firingBuffered = false;
    }

    void resetForRestart() {
        // Do not synthesize presses or releases during restart.
        m_simulating = false;
        m_firingBuffered = false;

        log::info("[InputBuffer] Restart: input state cleared");
    }

    bool isSimulating() const {
        return m_simulating;
    }

    bool isFiringBuffered() const {
        return m_firingBuffered;
    }

    bool checkSimulationDeath(PlayerObject*) {
        return false;
    }

    void setFrameDelta(float) {
        // Prediction is disabled in this diagnostic build.
    }

    int getSurvivalTicks(
        PlayLayer*,
        PlayerObject*,
        PlayerObject*,
        bool,
        int,
        bool,
        int maxTicks
    ) {
        // Do not update simulated players or run simulated collisions.
        return maxTicks;
    }

    bool interceptInput(
        GJBaseGameLayer*,
        bool,
        bool
    ) {
        // Pass all input through without buffering.
        return true;
    }

    void onPlayerUpdate(PlayerObject*) {
        // Do not replay or synthesize input events.
    }

    void m_SimFireButton(PlayLayer*, bool) {
        // Intentionally disabled for diagnosis.
    }
};

static TrajectoryBufferSim s_sim;

$on_mod(Loaded) {
    log::info("[InputBuffer] Loaded");
    log::info("[InputBuffer] Diagnostic build: prediction disabled");
    s_sim.applySettings();
}

class $modify(BufPlayLayer, PlayLayer) {
    bool init(
        GJGameLevel* level,
        bool useReplay,
        bool dontCreateObjects
    ) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        s_sim.init();
        return true;
    }

    void resetLevel() {
        s_sim.resetForRestart();
        PlayLayer::resetLevel();
    }

    void destroyPlayer(PlayerObject* player, GameObject* gameObject) {
        if (s_sim.checkSimulationDeath(player)) {
            return;
        }

        PlayLayer::destroyPlayer(player, gameObject);
    }

    void playEndAnimationToPos(cocos2d::CCPoint pos) {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayLayer::playEndAnimationToPos(pos);
    }

    void flipGravity(PlayerObject* player, bool first, bool second) {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayLayer::flipGravity(player, first, second);
    }

    void onQuit() {
        s_sim.quit();
        PlayLayer::onQuit();
    }
};

class $modify(BufEditorLayer, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool unk) {
        bool result = LevelEditorLayer::init(level, unk);
        s_sim.quit();
        return result;
    }
};

class $modify(BufHardStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint point) {
        if (s_sim.isSimulating()) {
            return;
        }

        HardStreak::addPoint(point);
    }
};

class $modify(BufEffectGameObject, EffectGameObject) {
    void triggerObject(
        GJBaseGameLayer* layer,
        int id,
        const gd::vector<int>* objects
    ) {
        if (s_sim.isSimulating()) {
            return;
        }

        EffectGameObject::triggerObject(layer, id, objects);
    }
};

class $modify(BufGameObject, GameObject) {
    void playShineEffect() {
        if (s_sim.isSimulating()) {
            return;
        }

        GameObject::playShineEffect();
    }
};

class $modify(BufPlayerObject, PlayerObject) {
    void playSpiderDashEffect(
        cocos2d::CCPoint from,
        cocos2d::CCPoint to
    ) {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayerObject::playSpiderDashEffect(from, to);
    }

    void incrementJumps() {
        if (s_simulating) {
            return;
        }

        PlayerObject::incrementJumps();
    }

    void update(float dt) {
        PlayerObject::update(dt);
    }

    void ringJump(RingObject* ring, bool second) {
        if (s_simulating) {
            return;
        }

        PlayerObject::ringJump(ring, second);
    }
};

class $modify(BufBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        // Never intercept or replay input in this diagnostic build.
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    bool canBeActivatedByPlayer(
        PlayerObject* player,
        EffectGameObject* object
    ) {
        return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
    }

    void collisionCheckObjects(
        PlayerObject* player,
        gd::vector<GameObject*>* objects,
        int count,
        float dt
    ) {
        GJBaseGameLayer::collisionCheckObjects(
            player,
            objects,
            count,
            dt
        );
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }
};

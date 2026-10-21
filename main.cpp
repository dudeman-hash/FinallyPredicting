
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>

using namespace geode::prelude;

constexpr int SPAM_INTERVAL_TICKS = 8;
constexpr int SPAM_STREAK_REQUIRED = 3;

class TrajectoryBufferSim {
private:
    PlayerObject* m_simPlayer1 = nullptr;
    PlayerObject* m_simPlayer2 = nullptr;

    bool m_simulating = false;
    bool m_firingBuffered = false;
    bool m_simulationDead = false;

    bool m_physicalHolding = false;
    bool m_gameHolding = false;
    bool m_ignoreHeldInput = false;
    int m_appliedStage = -1;
    bool m_enabled = true;
    int m_maxTicksNormal = 2;
    int m_maxTicksWave = 2;

    int m_waitTicks = -1;
    bool m_bufferedPush = true;
    std::deque<bool> m_pendingInputs;
    float m_frameDt = 0.25f;

    int m_ticksSinceLastInput = 999;
    int m_rapidInputStreak = 0;

    PlayerObject* createTrajectoryPlayer() {
        PlayLayer* pl = PlayLayer::get();
        if (!pl || !pl->m_objectLayer) {
            return nullptr;
        }

        PlayerObject* player =
            PlayerObject::create(1, 1, pl, pl, true);

        if (!player) {
            return nullptr;
        }

        player->retain();
        player->setPosition({0, 105});
        player->setVisible(false);
        pl->m_objectLayer->addChild(player);

        return player;
    }

    void resetCollisionLog(PlayerObject* player) {
        if (!player) {
            return;
        }

        if (player->m_collisionLogTop) {
            player->m_collisionLogTop->removeAllObjects();
        }
        if (player->m_collisionLogBottom) {
            player->m_collisionLogBottom->removeAllObjects();
        }
        if (player->m_collisionLogLeft) {
            player->m_collisionLogLeft->removeAllObjects();
        }
        if (player->m_collisionLogRight) {
            player->m_collisionLogRight->removeAllObjects();
        }
    }

    void syncSimPlayer(PlayerObject* sim, PlayerObject* real) {
        if (!sim || !real) {
            return;
        }

        sim->copyAttributes(real);
        sim->setPosition(real->getPosition());
        sim->setRotation(real->getRotation());
        sim->setScale(real->getScale());
        sim->setContentSize(real->getContentSize());
        sim->setVisible(false);

        sim->m_width = real->m_width;
        sim->m_height = real->m_height;
        sim->m_gravityMod = real->m_gravityMod;
        sim->m_isOnGround = real->m_isOnGround;
        sim->m_isUpsideDown = real->m_isUpsideDown;
        sim->m_vehicleSize = real->m_vehicleSize;
        sim->m_playerSpeed = real->m_playerSpeed;
    }

    float getValidDt() const {
        if (m_frameDt > 0.001f && m_frameDt < 2.0f) {
            return m_frameDt;
        }

        return 0.25f;
    }

    void clearPendingInput() {
        m_waitTicks = -1;
        m_pendingInputs.clear();
        m_bufferedPush = true;
        m_ticksSinceLastInput = 999;
        m_rapidInputStreak = 0;
    }

public:
    int getStage() const {
        return std::clamp(
            static_cast<int>(
                Mod::get()->getSettingValue<int64_t>("stage")
            ),
            0,
            8
        );
    }

    void setStage(int stage) {
        stage = std::clamp(stage, 0, 8);

        Mod::get()->setSettingValue<int64_t>(
            "stage",
            static_cast<int64_t>(stage)
        );

        applySettings();
    }

    void applySettings() {
        int stage = getStage();

        if (stage == m_appliedStage) {
            return;
        }

        m_appliedStage = stage;
        clearPendingInput();

        switch (stage) {
            case 0:
                m_enabled = false;
                m_maxTicksNormal = 0;
                m_maxTicksWave = 0;
                log::info("[InputBuffer] Stage 0: OFF");
                break;

            case 1:
                m_enabled = true;
                m_maxTicksNormal = 2;
                m_maxTicksWave = 2;
                log::info("[InputBuffer] Stage 1: 2 normal / 2 wave ticks");
                break;

            case 2:
                m_enabled = true;
                m_maxTicksNormal = 4;
                m_maxTicksWave = 4;
                log::info("[InputBuffer] Stage 2: 4 normal / 4 wave ticks");
                break;

            case 3:
                m_enabled = true;
                m_maxTicksNormal = 8;
                m_maxTicksWave = 8;
                log::info("[InputBuffer] Stage 3: 8 normal / 8 wave ticks");
                break;

            case 4:
                m_enabled = true;
                m_maxTicksNormal = 16;
                m_maxTicksWave = 8;
                log::info("[InputBuffer] Stage 4: 16 normal / 8 wave ticks");
                break;

            case 5:
                m_enabled = true;
                m_maxTicksNormal = 32;
                m_maxTicksWave = 16;
                log::info("[InputBuffer] Stage 5: 32 normal / 16 wave ticks");
                break;

            case 6:
                m_enabled = true;
                m_maxTicksNormal = 32;
                m_maxTicksWave = 32;
                log::info("[InputBuffer] Stage 6: 32 normal / 32 wave ticks");
                break;

            case 7:
                m_enabled = true;
                m_maxTicksNormal = 64;
                m_maxTicksWave = 32;
                log::info("[InputBuffer] Stage 7: 64 normal / 32 wave ticks");
                break;

            case 8:
                m_enabled = true;
                m_maxTicksNormal = 64;
                m_maxTicksWave = 64;
                log::info("[InputBuffer] Stage 8: 64 normal / 64 wave ticks");
                break;
        }
    }

    void init() {
        quit();

        m_simPlayer1 = createTrajectoryPlayer();
        m_simPlayer2 = createTrajectoryPlayer();

        m_physicalHolding = false;
        m_gameHolding = false;
        m_ignoreHeldInput = false;
        m_simulating = false;
        m_firingBuffered = false;
        m_simulationDead = false;

        clearPendingInput();
        m_appliedStage = -1;
        applySettings();
    }

    void quit() {
        if (m_simPlayer1) {
            m_simPlayer1->removeFromParent();
            m_simPlayer1->release();
            m_simPlayer1 = nullptr;
        }

        if (m_simPlayer2) {
            m_simPlayer2->removeFromParent();
            m_simPlayer2->release();
            m_simPlayer2 = nullptr;
        }

        m_simulating = false;
        m_firingBuffered = false;
        m_simulationDead = false;

        clearPendingInput();

        m_physicalHolding = false;
        m_gameHolding = false;
        m_ignoreHeldInput = false;
    }

    void resetForRestart() {
        bool wasHolding = m_physicalHolding || m_gameHolding;

        clearPendingInput();

        m_simulating = false;
        m_firingBuffered = false;
        m_simulationDead = false;

        m_ignoreHeldInput = wasHolding;
        m_physicalHolding = wasHolding;
        m_gameHolding = false;

        log::info(
            "[InputBuffer] Level restart: cleared pending input; held input guarded={}",
            m_ignoreHeldInput
        );
    }

    bool isSimulating() const {
        return m_simulating;
    }

    bool isFiringBuffered() const {
        return m_firingBuffered;
    }

    bool checkSimulationDeath(PlayerObject* player) {
        if (m_simulating &&
            (player == m_simPlayer1 || player == m_simPlayer2)) {
            m_simulationDead = true;
            return true;
        }

        clearPendingInput();
        m_gameHolding = false;
        return false;
    }

    void setFrameDelta(float dt) {
        PlayLayer* pl = PlayLayer::get();

        if (pl && pl->m_gameState.m_timeWarp > 0.f) {
            m_frameDt = dt / pl->m_gameState.m_timeWarp;
        } else {
            m_frameDt = dt;
        }
    }

    int getSurvivalTicks(
        PlayLayer* pl,
        PlayerObject* realP1,
        PlayerObject* realP2,
        bool isDual,
        int delayFrames,
        bool actionPush,
        int maxTicks
    ) {
        if (!pl || !m_simPlayer1 || !realP1) {
            log::warn(
                "[InputBuffer] Prediction unavailable: missing player or layer"
            );
            return 0;
        }

        m_simulating = true;
        m_simulationDead = false;

        syncSimPlayer(m_simPlayer1, realP1);

        if (isDual && m_simPlayer2 && realP2) {
            syncSimPlayer(m_simPlayer2, realP2);
        }

        if (delayFrames > 0) {
            if (!actionPush) {
                m_simPlayer1->pushButton(PlayerButton::Jump);

                if (isDual && m_simPlayer2) {
                    m_simPlayer2->pushButton(PlayerButton::Jump);
                }
            } else {
                m_simPlayer1->releaseButton(PlayerButton::Jump);

                if (isDual && m_simPlayer2) {
                    m_simPlayer2->releaseButton(PlayerButton::Jump);
                }
            }
        }

        float dt = getValidDt();
        int survived = 0;

        bool isContinuousMode =
            realP1->m_isDart ||
            realP1->m_isBird ||
            realP1->m_isSwing ||
            (isDual && realP2 &&
                (realP2->m_isDart ||
                 realP2->m_isBird ||
                 realP2->m_isSwing));

        int airborneTicks = 0;
        int safeLandedTicks = 0;

        for (int i = 0; i < delayFrames + maxTicks; i++) {
            if (i == delayFrames) {
                if (actionPush) {
                    m_simPlayer1->pushButton(PlayerButton::Jump);

                    if (isDual && m_simPlayer2) {
                        m_simPlayer2->pushButton(PlayerButton::Jump);
                    }
                } else {
                    m_simPlayer1->releaseButton(PlayerButton::Jump);

                    if (isDual && m_simPlayer2) {
                        m_simPlayer2->releaseButton(PlayerButton::Jump);
                    }
                }
            }

            m_simPlayer1->update(dt);
            m_simPlayer1->updateRotation(dt);

            if (isDual && m_simPlayer2) {
                m_simPlayer2->update(dt);
                m_simPlayer2->updateRotation(dt);
            }

            if (m_simulationDead) {
                break;
            }

            resetCollisionLog(m_simPlayer1);
            pl->checkCollisions(m_simPlayer1, dt, false);

            if (m_simulationDead) {
                break;
            }

            if (isDual && m_simPlayer2 && realP2) {
                resetCollisionLog(m_simPlayer2);
                pl->checkCollisions(m_simPlayer2, dt, false);

                if (m_simulationDead) {
                    break;
                }
            }

            if (i >= delayFrames) {
                survived++;

                if (!isContinuousMode) {
                    bool p1Grounded = m_simPlayer1->m_isOnGround;
                    bool p2Grounded =
                        (!isDual || !m_simPlayer2)
                            ? true
                            : m_simPlayer2->m_isOnGround;

                    if (!p1Grounded || !p2Grounded) {
                        airborneTicks++;
                        safeLandedTicks = 0;
                    } else if (airborneTicks >= 15) {
                        safeLandedTicks++;

                        if (safeLandedTicks >= 10) {
                            survived = maxTicks;
                            break;
                        }
                    }
                }
            }
        }

        m_simulating = false;
        return survived;
    }

    bool interceptInput(
        GJBaseGameLayer* layer,
        bool push,
        bool isPlayer1
    ) {
        PlayLayer* pl = PlayLayer::get();

        if (!pl) {
            return true;
        }

        applySettings();

        if (m_ignoreHeldInput) {
            if (push) {
                m_physicalHolding = true;
                return false;
            }

            m_physicalHolding = false;
            m_gameHolding = false;
            m_ignoreHeldInput = false;
            clearPendingInput();
            return true;
        }

        bool isDual =
            pl->m_gameState.m_isDualMode &&
            pl->m_player2 != nullptr;

        bool isTwoPlayerControl =
            pl->m_level && pl->m_level->m_twoPlayerMode;

        if (!isPlayer1) {
            if (isDual && !isTwoPlayerControl &&
                (m_waitTicks >= 0 || !m_pendingInputs.empty())) {
                return false;
            }

            return true;
        }

        bool wasPhysicallyHolding = m_physicalHolding;
        m_physicalHolding = push;

        if (!m_enabled) {
            m_gameHolding = push;
            return true;
        }

        if (push == wasPhysicallyHolding) {
            return false;
        }

        PlayerObject* p1 = layer->m_player1;
        PlayerObject* p2 = isDual ? pl->m_player2 : nullptr;

        if (!p1 || p1->m_isDead) {
            clearPendingInput();
            m_gameHolding = push;
            return true;
        }

        if (p1->m_isShip || (isDual && p2 && p2->m_isShip)) {
            clearPendingInput();
            m_gameHolding = push;
            return true;
        }

        bool isWave =
            p1->m_isDart ||
            (isDual && p2 && p2->m_isDart);

        bool isMiniWave =
            (p1->m_isDart && p1->m_vehicleSize < 1.0f) ||
            (isDual && p2 && p2->m_isDart &&
                p2->m_vehicleSize < 1.0f);

        if (isWave && m_waitTicks >= 0) {
            m_waitTicks = -1;
            m_pendingInputs.clear();

            m_ticksSinceLastInput = 0;
            m_rapidInputStreak = 0;

            if (push == m_gameHolding) {
                return false;
            }

            m_gameHolding = push;
            return true;
        }

        if (m_waitTicks >= 0) {
            m_pendingInputs.push_back(push);
            m_ticksSinceLastInput = 0;
            return false;
        }

        if (m_ticksSinceLastInput < SPAM_INTERVAL_TICKS) {
            m_rapidInputStreak++;
        } else {
            m_rapidInputStreak = 0;
        }

        m_ticksSinceLastInput = 0;

        if (isWave &&
            m_rapidInputStreak >= SPAM_STREAK_REQUIRED) {
            m_gameHolding = push;
            return true;
        }

        bool releaseAffectsPhysics =
            p1->m_isDart ||
            p1->m_isRobot ||
            (isDual && p2 &&
                (p2->m_isDart || p2->m_isRobot));

        if (!push && !releaseAffectsPhysics) {
            m_gameHolding = false;
            return true;
        }

        const char* act = push ? "PRESS" : "RELEASE";

        int targetHorizon =
            isMiniWave ? 50 :
            (isWave ? 65 : 160);

        int maxDelayFrames =
            isWave ? m_maxTicksWave : m_maxTicksNormal;

        int ticksFrame0 = getSurvivalTicks(
            pl, p1, p2, isDual, 0, push, targetHorizon
        );

        if (ticksFrame0 >= targetHorizon) {
            log::debug(
                "[InputBuffer] {} passed immediately; survival={}",
                act,
                ticksFrame0
            );

            m_gameHolding = push;
            return true;
        }

        int bestDelay = 0;
        int bestTicks = ticksFrame0;

        for (int delay = 1; delay <= maxDelayFrames; delay++) {
            int ticks = getSurvivalTicks(
                pl, p1, p2, isDual, delay, push, targetHorizon
            );

            log::info(
                "[InputBuffer] Prediction: delay={} baseline={} candidate={}",
                delay,
                ticksFrame0,
                ticks
            );

            if (ticks > bestTicks) {
                bestDelay = delay;
                bestTicks = ticks;
            }

            if (ticks >= targetHorizon && ticks > ticksFrame0) {
                bestDelay = delay;
                bestTicks = ticks;
                break;
            }
        }

        if (bestDelay > 0) {
            log::info(
                "[InputBuffer] Buffered {}: delay={} ticks, survival {} -> {}, limit={}",
                act,
                bestDelay,
                ticksFrame0,
                bestTicks,
                maxDelayFrames
            );

            m_bufferedPush = push;
            m_waitTicks = bestDelay;
            return false;
        }

        log::debug(
            "[InputBuffer] No beneficial delay for {}; input passed through",
            act
        );

        m_gameHolding = push;
        return true;
    }

    void onPlayerUpdate(PlayerObject* player) {
        PlayLayer* pl = PlayLayer::get();

        if (!pl || player != pl->m_player1) {
            return;
        }

        if (m_ignoreHeldInput) {
            return;
        }

        applySettings();

        if (!m_enabled) {
            return;
        }

        if (m_ticksSinceLastInput < 999) {
            m_ticksSinceLastInput++;
        }

        if (player->m_isShip ||
            (pl->m_gameState.m_isDualMode &&
             pl->m_player2 &&
             pl->m_player2->m_isShip)) {
            clearPendingInput();

            if (m_gameHolding != m_physicalHolding) {
                m_SimFireButton(pl, m_physicalHolding);
            }

            return;
        }

        if (m_waitTicks == 0) {
            m_SimFireButton(pl, m_bufferedPush);
            m_waitTicks = -1;
            return;
        }

        if (m_waitTicks > 0) {
            m_waitTicks--;
            return;
        }

        if (!m_pendingInputs.empty()) {
            bool nextInput = m_pendingInputs.front();
            m_pendingInputs.pop_front();

            if (nextInput != m_gameHolding) {
                m_SimFireButton(pl, nextInput);
            }

            return;
        }

        if (m_waitTicks == -1 &&
            m_gameHolding != m_physicalHolding) {
            m_SimFireButton(pl, m_physicalHolding);
        }
    }

    void m_SimFireButton(PlayLayer* pl, bool push) {
        if (!pl || m_ignoreHeldInput) {
            return;
        }

        m_firingBuffered = true;
        m_gameHolding = push;

        pl->handleButton(push, 1, true);

        if (pl->m_gameState.m_isDualMode &&
            (!pl->m_level || !pl->m_level->m_twoPlayerMode)) {
            pl->handleButton(push, 1, false);
        }

        m_firingBuffered = false;
    }
};

static TrajectoryBufferSim s_sim;

$on_mod(Loaded) {
    log::info("[InputBuffer] Loaded");
    log::info("[InputBuffer] Stage setting is read from Geode settings.");
    s_sim.applySettings();
}

class $modify(BufPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", -1000000);
        (void)self.setHookPriority("PlayLayer::playEndAnimationToPos", -1000000);
        (void)self.setHookPriority("PlayLayer::flipGravity", -1000000);
    }

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

    void playEndAnimationToPos(cocos2d::CCPoint p0) {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayLayer::playEndAnimationToPos(p0);
    }

    void flipGravity(PlayerObject* p0, bool p1, bool p2) {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayLayer::flipGravity(p0, p1, p2);
    }

    void onQuit() {
        s_sim.quit();
        PlayLayer::onQuit();
    }
};

class $modify(BufPauseLayer, PauseLayer) {
    struct Fields {
        CCLabelBMFont* m_bufferStageLabel = nullptr;
    };

    void updateBufferStageLabel() {
        if (!m_fields->m_bufferStageLabel) {
            return;
        }

        char label[32];
        std::snprintf(
            label,
            sizeof(label),
            "STAGE %d",
            s_sim.getStage()
        );

        m_fields->m_bufferStageLabel->setString(label);
    }

    void onPreviousBufferStage(CCObject*) {
        s_sim.setStage(s_sim.getStage() - 1);
        updateBufferStageLabel();
    }

    void onNextBufferStage(CCObject*) {
        s_sim.setStage(s_sim.getStage() + 1);
        updateBufferStageLabel();
    }

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto stageMenu = CCMenu::create();
        if (!stageMenu) {
            return;
        }

        stageMenu->setPosition({
            winSize.width / 2.f,
            winSize.height * 0.22f
        });

        auto previousLabel = CCLabelBMFont::create(
            "<",
            "bigFont.fnt"
        );

        auto nextLabel = CCLabelBMFont::create(
            ">",
            "bigFont.fnt"
        );

        auto stageLabel = CCLabelBMFont::create(
            "STAGE 0",
            "bigFont.fnt"
        );

        if (!previousLabel || !nextLabel || !stageLabel) {
            stageMenu->release();
            return;
        }

        auto previousButton = CCMenuItemLabel::create(
            previousLabel,
            this,
            menu_selector(BufPauseLayer::onPreviousBufferStage)
        );

        auto nextButton = CCMenuItemLabel::create(
            nextLabel,
            this,
            menu_selector(BufPauseLayer::onNextBufferStage)
        );

        if (!previousButton || !nextButton) {
            stageMenu->release();
            return;
        }

        previousButton->setPosition({-90.f, 0.f});
        stageLabel->setPosition({0.f, 0.f});
        nextButton->setPosition({90.f, 0.f});

        stageMenu->addChild(previousButton);
        stageMenu->addChild(stageLabel);
        stageMenu->addChild(nextButton);

        this->addChild(stageMenu, 1000);

        m_fields->m_bufferStageLabel = stageLabel;
        updateBufferStageLabel();
    }
};

class $modify(BufEditorLayer, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool unk) {
        bool res = LevelEditorLayer::init(level, unk);
        s_sim.quit();
        return res;
    }
};

class $modify(BufHardStreak, HardStreak) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("HardStreak::addPoint", -1000000);
    }

    void addPoint(cocos2d::CCPoint p0) {
        if (s_sim.isSimulating()) {
            return;
        }

        HardStreak::addPoint(p0);
    }
};

class $modify(BufEffectGameObject, EffectGameObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority(
            "EffectGameObject::triggerObject",
            -1000000
        );
    }

    void triggerObject(
        GJBaseGameLayer* p0,
        int p1,
        const gd::vector<int>* p2
    ) {
        if (s_sim.isSimulating()) {
            return;
        }

        EffectGameObject::triggerObject(p0, p1, p2);
    }
};

class $modify(BufGameObject, GameObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority(
            "GameObject::playShineEffect",
            -1000000
        );
    }

    void playShineEffect() {
        if (s_sim.isSimulating()) {
            return;
        }

        GameObject::playShineEffect();
    }
};

class $modify(BufPlayerObject, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority(
            "PlayerObject::playSpiderDashEffect",
            -1000000
        );
        (void)self.setHookPriority(
            "PlayerObject::incrementJumps",
            -1000000
        );
        (void)self.setHookPriority(
            "PlayerObject::ringJump",
            -1000000
        );
    }

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
        if (s_sim.isSimulating()) {
            return;
        }

        PlayerObject::incrementJumps();
    }

    void update(float dt) {
        if (!s_sim.isSimulating()) {
            s_sim.onPlayerUpdate(this);
        }

        PlayerObject::update(dt);

        if (PlayLayer::get() && !s_sim.isSimulating()) {
            s_sim.setFrameDelta(dt);
        }
    }

    void ringJump(RingObject* p0, bool p1) {
        if (s_sim.isSimulating()) {
            return;
        }

        PlayerObject::ringJump(p0, p1);
    }
};

class $modify(BufBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority(
            "GJBaseGameLayer::canBeActivatedByPlayer",
            -1000000
        );
        (void)self.setHookPriority(
            "GJBaseGameLayer::handleButton",
            -1000000
        );
        (void)self.setHookPriority(
            "GJBaseGameLayer::collisionCheckObjects",
            -1000000
        );
        (void)self.setHookPriority(
            "GJBaseGameLayer::playerTouchedRing",
            -1000000
        );
    }

    bool canBeActivatedByPlayer(
        PlayerObject* p0,
        EffectGameObject* p1
    ) {
        if (s_sim.isSimulating()) {
            return false;
        }

        return GJBaseGameLayer::canBeActivatedByPlayer(p0, p1);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (!s_sim.isSimulating() &&
            !s_sim.isFiringBuffered() &&
            button == 1 &&
            PlayLayer::get()) {
            if (!s_sim.interceptInput(this, down, isPlayer1)) {
                return;
            }
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void collisionCheckObjects(
        PlayerObject* player,
        gd::vector<GameObject*>* vec,
        int objectsCount,
        float dt
    ) {
        if (s_sim.isSimulating()) {
            gd::vector<GameObject*> extra;
            extra.reserve(objectsCount);

            for (int i = 0; i < objectsCount; i++) {
                GameObject* obj = vec->at(i);

                if (!obj) {
                    continue;
                }

                if (obj->m_objectType == GameObjectType::Solid ||
                    obj->m_objectType == GameObjectType::Hazard ||
                    obj->m_objectType == GameObjectType::AnimatedHazard ||
                    obj->m_objectType == GameObjectType::CollisionObject ||
                    obj->m_objectType == GameObjectType::Slope) {
                    extra.push_back(obj);
                }
            }

            GJBaseGameLayer::collisionCheckObjects(
                player,
                &extra,
                static_cast<int>(extra.size()),
                dt
            );

            return;
        }

        GJBaseGameLayer::collisionCheckObjects(
            player,
            vec,
            objectsCount,
            dt
        );
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (s_sim.isSimulating()) {
            return;
        }

        GJBaseGameLayer::playerTouchedRing(player, ring);
    }
};

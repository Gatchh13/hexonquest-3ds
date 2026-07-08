#include "core/Engine.h"
#include "ai/AIController.h"
#include "save/SaveManager.h"
#include "core/Logger.h"
#include "game/TechTree.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace poly {

namespace {
struct MapSizeOption {
    const char* label;
    int32_t width;
    int32_t height;
};
constexpr MapSizeOption kMapSizes[] = {
    {"Small", 14, 11},
    {"Medium", 20, 16},
    {"Large", 26, 20},
};
constexpr int32_t kMapSizeCount = static_cast<int32_t>(sizeof(kMapSizes) / sizeof(kMapSizes[0]));
constexpr int32_t kMaxOpponentsPerSize[] = {3, 5, 5}; // indexed by kMapSizes index
constexpr uint64_t kMapSeed = 1337ULL;
constexpr float kCameraPanSpeed = 140.0f; // world px/sec at full circle-pad deflection

// Action ids used by the various touch-button panels. Kept in one place
// so handlers and button builders agree on their meaning.
constexpr int32_t kActionCityProduceBase = 1000; // + UnitType index
constexpr int32_t kActionCityOpenResearch = 2000;
constexpr int32_t kActionCityClose = 2001;
constexpr int32_t kActionTechBase = 3000; // + TechId index
constexpr int32_t kActionTechBack = 3999;
constexpr int32_t kActionMenuNewGame = 4000;
constexpr int32_t kActionMenuContinue = 4001;
constexpr int32_t kActionMenuSettings = 4002;
constexpr int32_t kActionMenuQuit = 4003;
constexpr int32_t kActionPauseResume = 5000;
constexpr int32_t kActionPauseSave = 5001;
constexpr int32_t kActionPauseLoad = 5002;
constexpr int32_t kActionPauseSettings = 5003;
constexpr int32_t kActionPauseMainMenu = 5004;
constexpr int32_t kActionPauseQuitApp = 5005;
constexpr int32_t kActionSettingsMusicDown = 6000;
constexpr int32_t kActionSettingsMusicUp = 6001;
constexpr int32_t kActionSettingsSfxDown = 6002;
constexpr int32_t kActionSettingsSfxUp = 6003;
constexpr int32_t kActionSettingsToggleGrid = 6004;
constexpr int32_t kActionSettingsBack = 6005;
constexpr int32_t kActionQuickBuildRoad = 7000;
constexpr int32_t kActionQuickHeal = 7001;
constexpr int32_t kActionQuickDisband = 7002;
constexpr int32_t kActionQuickNextUnit = 7003;
constexpr int32_t kActionPauseTechnology = 5006;
constexpr int32_t kActionSlotBase = 8000; // + slot index
constexpr int32_t kActionSlotBack = 8999;
constexpr int32_t kActionSetupMapSizeDown = 9000;
constexpr int32_t kActionSetupMapSizeUp = 9001;
constexpr int32_t kActionSetupOpponentsDown = 9002;
constexpr int32_t kActionSetupOpponentsUp = 9003;
constexpr int32_t kActionSetupStart = 9004;
constexpr int32_t kActionSetupBack = 9005;

bool anySlotHasSave() {
    for (int32_t i = 0; i < SaveManager::kSlotCount; ++i) {
        char path[64];
        SaveManager::getSlotPath(i, path, sizeof(path));
        if (SaveManager::peekSlot(path).exists) return true;
    }
    return false;
}

} // namespace

bool Engine::init() {
    Logger::init();

    if (!renderer_.init()) {
        reportFatalError("Failed to initialize graphics (citro2d/citro3d).");
        return false;
    }
    Logger::log(LogLevel::Info, "Renderer initialized");

    romfsMounted_ = (romfsInit() == 0);
    if (!romfsMounted_) {
        Logger::log(LogLevel::Warning, "romfs not mounted; bundled assets unavailable");
    }

    settings_.load();
    showDebugOverlay_ = settings_.showGridCoordinates;
    setupMapSizeIndex_ = std::clamp(settings_.lastMapSizeIndex, 0, kMapSizeCount - 1);
    setupOpponentCount_ = std::clamp(settings_.lastOpponentCount, 1, kMaxOpponents);

    if (!audio_.init()) {
        Logger::log(LogLevel::Warning, "Audio init failed; continuing without sound");
    } else {
        audio_.setMasterMusicVolume(settings_.musicVolume);
        audio_.setMasterSfxVolume(settings_.sfxVolume);
        sfxMove_ = audio_.loadSound("romfs:/audio/move.wav");
        sfxAttack_ = audio_.loadSound("romfs:/audio/attack.wav");
        sfxFound_ = audio_.loadSound("romfs:/audio/found.wav");
        musicTrack_ = audio_.loadSound("romfs:/audio/theme.wav");
        if (musicTrack_ != kInvalidSoundHandle) {
            audio_.playMusic(musicTrack_, 1.0f, true);
        }
    }

    camera_.setViewportSize(400.0f, 240.0f);

    state_ = EngineState::MainMenu;
    lastTickNs_ = svcGetSystemTick();
    running_ = true;
    Logger::log(LogLevel::Info, "Engine initialized OK");
    return true;
}

void Engine::shutdown() {
    settings_.save();
    audio_.shutdown();
    if (romfsMounted_) romfsExit();
    renderer_.shutdown();
    Logger::log(LogLevel::Info, "Engine shutdown complete");
    Logger::shutdown();
}

int Engine::run() {
    while (running_ && aptMainLoop()) {
        const u64 now = svcGetSystemTick();
        float dt = static_cast<float>(now - lastTickNs_) / static_cast<float>(SYSCLOCK_ARM11);
        lastTickNs_ = now;
        if (dt > 0.1f) dt = 0.1f;

        smoothedFrameTimeMs_ = lerp(smoothedFrameTimeMs_, dt * 1000.0f, 0.1f);

        input_.poll();
        handleInput(dt);
        update(dt);

        renderer_.beginFrame();
        render();
        renderer_.endFrame();
    }
    return 0;
}

void Engine::setStatus(const char* msg, float durationSeconds) {
    std::snprintf(statusMessage_, sizeof(statusMessage_), "%s", msg);
    statusMessageTimer_ = durationSeconds;
}

void Engine::startNewGame() {
    const MapSizeOption& size = kMapSizes[setupMapSizeIndex_];
    const int32_t numPlayers = setupOpponentCount_ + 1;

    gameState_.newGame(size.width, size.height, kMapSeed, numPlayers, kHumanPlayerId);
    turnManager_.start(gameState_, [](GameState& state, PlayerId pid) {
        AIController::takeTurn(state, pid);
    });

    const float margin = kBaseHexSize * 4.0f;
    const Vec2 topLeft = hexToPixel(HexCoord(0, 0), kBaseHexSize);
    const Vec2 bottomRight = hexToPixel(HexCoord(size.width, size.height), kBaseHexSize);
    camera_.setWorldBounds(topLeft.x - margin, topLeft.y - margin,
                            bottomRight.x + margin, bottomRight.y + margin);

    if (const Player* human = gameState_.player(kHumanPlayerId)) {
        if (const City* cap = gameState_.city(human->capitalCityId)) {
            cursor_ = cap->coord;
            camera_.setPosition(hexToPixel(cap->coord, kBaseHexSize));
        }
    }

    selectedUnitId_ = kInvalidUnitId;
    activeCityId_ = kInvalidCityId;
    gameOver_ = false;
    winnerId_ = kInvalidPlayerId;
    wonByScore_ = false;
    particles_.clear();
    anims_.clear();
    lastBannerPlayerId_ = kHumanPlayerId;
    turnBannerTimer_ = 0.0f;
    state_ = EngineState::Playing;
    setStatus("New game started");
    startTutorialIfNeeded();
}

namespace {
constexpr const char* kTutorialMessages[] = {
    "Welcome to Hexonquest!\nUse the Circle Pad to pan the camera\nand the D-Pad to move the cursor.",
    "Press A on one of your units to select it,\nthen press A on a tile to move there\nor attack an enemy.",
    "Press X on a city you own to open its\nproduction and research panel.",
    "Land units can board a Boat: select the unit\nnext to the boat and press A on its tile.\n"
    "To disembark, select the boat and press X\nover an adjacent land tile.",
    "Press Y to end your turn.\nPress START any time to pause, save, or load.",
};
constexpr int32_t kTutorialMessageCount =
    static_cast<int32_t>(sizeof(kTutorialMessages) / sizeof(kTutorialMessages[0]));
} // namespace

void Engine::startTutorialIfNeeded() {
    if (settings_.tutorialSeen) return;
    tutorialIndex_ = 0;
}

void Engine::renderTutorialOverlay() {
    if (tutorialIndex_ < 0 || tutorialIndex_ >= kTutorialMessageCount) return;

    renderer_.drawRect(20.0f, 45.0f, 360.0f, 140.0f, Color4(10, 10, 10, 230));
    renderer_.drawRectOutline(20.0f, 45.0f, 360.0f, 140.0f, Color4(255, 235, 150), 2.0f);
    renderer_.drawTextf(30.0f, 53.0f, 0.4f, Color4(255, 235, 150), "Tip %d/%d",
                         tutorialIndex_ + 1, kTutorialMessageCount);
    renderer_.drawText(30.0f, 72.0f, 0.42f, Color4(230, 230, 235), kTutorialMessages[tutorialIndex_]);
    renderer_.drawText(30.0f, 165.0f, 0.4f, Color4(180, 220, 180),
                        "Press A to continue, START to skip");
}

// =========================================================================
// Input dispatch
// =========================================================================

void Engine::handleInput(float dt) {
    const FrameInput& in = input_.frame();

    switch (state_) {
        case EngineState::MainMenu:     handleMainMenuInput(in); return;
        case EngineState::NewGameSetup: handleNewGameSetupInput(in); return;
        case EngineState::PauseMenu:    handlePauseMenuInput(in); return;
        case EngineState::CityPanel:    handleCityPanelInput(in); return;
        case EngineState::TechPanel:    handleTechPanelInput(in); return;
        case EngineState::SettingsPanel: handleSettingsPanelInput(in); return;
        case EngineState::SaveSlotPanel: handleSaveSlotPanelInput(in); return;
        case EngineState::Playing:      handlePlayingInput(in, dt); return;
    }
}

void Engine::handlePlayingInput(const FrameInput& in, float dt) {
    if (tutorialIndex_ >= 0) {
        if (in.pressed(KEY_START)) {
            tutorialIndex_ = -1;
            settings_.tutorialSeen = true;
            settings_.save();
        } else if (in.pressed(KEY_A) || in.gesture == TouchGesture::Tap) {
            ++tutorialIndex_;
            if (tutorialIndex_ >= kTutorialMessageCount) {
                tutorialIndex_ = -1;
                settings_.tutorialSeen = true;
                settings_.save();
            }
        }
        return;
    }

    if (in.pressed(KEY_START)) {
        pendingPauseConfirmAction_ = -1;
        pendingPauseConfirmTimer_ = 0.0f;
        state_ = EngineState::PauseMenu;
        return;
    }

    if (gameOver_) {
        return; // world is frozen once a match ends; PauseMenu/START still works above
    }

    if (in.held(KEY_SELECT) && in.pressed(KEY_X)) {
        doSave(0);
    } else if (in.held(KEY_SELECT) && in.pressed(KEY_Y)) {
        doLoad(0);
    } else if (in.held(KEY_SELECT) && in.pressed(KEY_A) && turnManager_.isCurrentPlayerHuman()) {
        cycleToNextReadyUnit(true);
    } else if (in.held(KEY_SELECT) && in.pressed(KEY_B) && turnManager_.isCurrentPlayerHuman()) {
        cycleToNextReadyUnit(false);
    } else if (in.pressed(KEY_SELECT)) {
        showDebugOverlay_ = !showDebugOverlay_;
        settings_.showGridCoordinates = showDebugOverlay_;
    }

    if (in.circlePad.lengthSq() > 0.0f) {
        camera_.pan(in.circlePad * (kCameraPanSpeed * dt));
    }

    HexCoord moved = cursor_;
    if (in.pressed(KEY_DUP))    moved = hexNeighbor(cursor_, 2);
    if (in.pressed(KEY_DDOWN))  moved = hexNeighbor(cursor_, 5);
    if (in.pressed(KEY_DLEFT))  moved = hexNeighbor(cursor_, 3);
    if (in.pressed(KEY_DRIGHT)) moved = hexNeighbor(cursor_, 0);
    if (moved != cursor_ && gameState_.grid().isInBounds(moved)) {
        cursor_ = moved;
    }

    if (in.held(KEY_L)) camera_.zoomBy(1.0f - dt);
    if (in.held(KEY_R)) camera_.zoomBy(1.0f + dt);

    if (in.gesture == TouchGesture::Dragging) {
        const Vec2 worldDelta = Vec2(-in.dragDelta.x, -in.dragDelta.y) / camera_.zoom();
        camera_.pan(worldDelta);
    } else if (in.gesture == TouchGesture::Tap) {
        bool consumedByButton = false;
        if (turnManager_.isCurrentPlayerHuman()) {
            for (const UIButton& b : buildQuickActionButtons()) {
                if (!b.enabled || !b.contains(in.tapPos)) continue;
                consumedByButton = true;
                if (b.actionId == kActionQuickBuildRoad) {
                    if (gameState_.buildRoad(selectedUnitId_)) setStatus("Road built");
                } else if (b.actionId == kActionQuickHeal) {
                    if (gameState_.healUnitAtCity(selectedUnitId_)) setStatus("Unit healed");
                } else if (b.actionId == kActionQuickDisband) {
                    if (pendingDisbandUnitId_ == selectedUnitId_) {
                        if (gameState_.disbandUnit(selectedUnitId_)) {
                            setStatus("Unit disbanded");
                        }
                        selectedUnitId_ = kInvalidUnitId;
                        pendingDisbandUnitId_ = kInvalidUnitId;
                        pendingDisbandTimer_ = 0.0f;
                    } else {
                        pendingDisbandUnitId_ = selectedUnitId_;
                        pendingDisbandTimer_ = 3.0f;
                        setStatus("Tap Disband again to confirm");
                    }
                } else if (b.actionId == kActionQuickNextUnit) {
                    cycleToNextReadyUnit(true);
                }
                break;
            }
        }
        if (!consumedByButton) {
            const Vec2 world = camera_.screenToWorld(in.tapPos);
            const HexCoord picked = pixelToHex(world, camera_.effectiveHexSize(kBaseHexSize));
            if (gameState_.grid().isInBounds(picked)) {
                cursor_ = picked;
            }
        }
    }

    if (!turnManager_.isCurrentPlayerHuman()) {
        return; // watch the AI play; no unit/city actions during its turn
    }

    if (in.pressed(KEY_A)) {
        selectOrActOnCursor();
    }
    if (in.pressed(KEY_B)) {
        selectedUnitId_ = kInvalidUnitId;
    }
    if (in.pressed(KEY_X) && !in.held(KEY_SELECT)) {
        if (City* c = gameState_.cityAt(cursor_); c && c->owner == kHumanPlayerId) {
            activeCityId_ = c->id;
            state_ = EngineState::CityPanel;
        } else if (Unit* sel = gameState_.unit(selectedUnitId_);
                   sel && sel->isBoat() && sel->passengerId != kInvalidUnitId) {
            if (gameState_.disembarkUnit(sel->passengerId, cursor_)) {
                setStatus("Disembarked");
            } else {
                setStatus("Can't disembark there");
            }
        } else {
            tryFoundCity();
        }
    }
    if (in.pressed(KEY_Y) && !in.held(KEY_SELECT)) {
        endHumanTurn();
    }
}

void Engine::selectOrActOnCursor() {
    Unit* cursorUnit = gameState_.unitAt(cursor_);

    if (selectedUnitId_ == kInvalidUnitId) {
        if (cursorUnit && cursorUnit->owner == kHumanPlayerId && cursorUnit->canAct()) {
            selectedUnitId_ = cursorUnit->id;
        }
        return;
    }

    Unit* selected = gameState_.unit(selectedUnitId_);
    if (!selected || !selected->alive || selected->owner != kHumanPlayerId) {
        selectedUnitId_ = kInvalidUnitId;
        return;
    }

    if (selected->coord == cursor_) {
        selectedUnitId_ = kInvalidUnitId;
        return;
    }

    if (cursorUnit) {
        if (cursorUnit->owner == kHumanPlayerId) {
            if (gameState_.canEmbark(selectedUnitId_, cursorUnit->id)) {
                if (gameState_.embarkUnit(selectedUnitId_, cursorUnit->id)) {
                    setStatus("Boarded boat");
                    selectedUnitId_ = kInvalidUnitId;
                }
                return;
            }
            if (cursorUnit->canAct()) selectedUnitId_ = cursorUnit->id;
            return;
        }

        if (gameState_.canAttack(selectedUnitId_, cursorUnit->id)) {
            const Vec2 attackerWorld = hexWorldCenter(selected->coord);
            const Vec2 defenderWorld = hexWorldCenter(cursorUnit->coord);
            const CombatResult result = gameState_.attack(selectedUnitId_, cursorUnit->id);

            anims_.playAttackLunge(selectedUnitId_, attackerWorld, defenderWorld);
            audio_.playSfx(sfxAttack_);

            if (result.defenderKilled) {
                particles_.emitBurst(camera_.worldToScreen(defenderWorld), 10, Color4(255, 140, 60),
                                      Color4(80, 20, 10, 0), 55.0f, 0.5f);
                if (result.attackerPromoted) {
                    for (int i = 0; i < 6; ++i) {
                        particles_.emitSparkle(camera_.worldToScreen(attackerWorld), Color4(180, 220, 255));
                    }
                    setStatus(result.cityCaptured ? "City captured! Veteran!" : "Veteran promotion!");
                } else {
                    setStatus(result.cityCaptured ? "City captured!" : "Enemy defeated!");
                }
            } else {
                particles_.emitBurst(camera_.worldToScreen(defenderWorld), 5, Color4(255, 220, 120),
                                      Color4(120, 60, 20, 0), 35.0f, 0.3f);
                setStatus("Attack!");
            }
            Unit* after = gameState_.unit(selectedUnitId_);
            if (!after || !after->canAct()) selectedUnitId_ = kInvalidUnitId;
        }
        return;
    }

    const Vec2 fromWorld = hexWorldCenter(selected->coord);
    if (gameState_.moveUnit(selectedUnitId_, cursor_)) {
        const Vec2 toWorld = hexWorldCenter(cursor_);
        anims_.playUnitMove(selectedUnitId_, fromWorld, toWorld);
        audio_.playSfx(sfxMove_);
        Unit* after = gameState_.unit(selectedUnitId_);
        if (!after || (after->movesLeft <= 0.0f && after->hasAttacked)) {
            selectedUnitId_ = kInvalidUnitId;
        }
    }
}

void Engine::cycleToNextReadyUnit(bool forward) {
    const Player* p = gameState_.player(kHumanPlayerId);
    if (!p) return;

    std::vector<UnitId> ready;
    for (UnitId uid : p->unitIds) {
        const Unit* u = gameState_.unit(uid);
        if (u && u->alive && !u->embarked && u->canAct()) {
            ready.push_back(uid);
        }
    }
    if (ready.empty()) {
        setStatus("No units left to act");
        return;
    }

    int32_t currentIdx = -1;
    for (size_t i = 0; i < ready.size(); ++i) {
        if (ready[i] == selectedUnitId_) {
            currentIdx = static_cast<int32_t>(i);
            break;
        }
    }

    int32_t nextIdx;
    if (currentIdx < 0) {
        nextIdx = forward ? 0 : static_cast<int32_t>(ready.size()) - 1;
    } else {
        const int32_t count = static_cast<int32_t>(ready.size());
        nextIdx = forward ? (currentIdx + 1) % count : (currentIdx - 1 + count) % count;
    }

    selectedUnitId_ = ready[static_cast<size_t>(nextIdx)];
    if (const Unit* u = gameState_.unit(selectedUnitId_)) {
        cursor_ = u->coord;
        camera_.setPosition(hexWorldCenter(u->coord));
    }
}

void Engine::tryFoundCity() {
    if (selectedUnitId_ == kInvalidUnitId) {
        setStatus("Select a unit first");
        return;
    }
    Unit* u = gameState_.unit(selectedUnitId_);
    if (!u || u->owner != kHumanPlayerId) return;
    if (u->coord != cursor_) return;

    const Tile* t = gameState_.grid().tileAt(u->coord);
    const bool alreadyWorked = t && t->parentCityId != kInvalidCityId;
    if (!t || !t->isPassableLand() || t->cityId != kInvalidCityId || alreadyWorked) {
        setStatus("Can't found here");
        return;
    }

    const CityId newCityId = gameState_.foundCity(kHumanPlayerId, u->coord, false);
    const Vec2 world = hexWorldCenter(u->coord);
    const Vec2 screen = camera_.worldToScreen(world);
    for (int i = 0; i < 5; ++i) {
        particles_.emitSparkle(screen, Color4(255, 235, 120));
    }
    anims_.playCityPulse(newCityId);
    audio_.playSfx(sfxFound_);
    setStatus("City founded!");
}

void Engine::endHumanTurn() {
    selectedUnitId_ = kInvalidUnitId;
    turnManager_.endTurn();
    setStatus("Turn ended");
}

void Engine::doSave(int32_t slot) {
    SaveManager::ensureSaveDirectory();
    char path[64];
    SaveManager::getSlotPath(slot, path, sizeof(path));
    const bool ok = SaveManager::saveGame(gameState_, turnManager_.turnNumber(),
                                           turnManager_.currentPlayerIndex(), path);
    setStatus(ok ? "Game saved" : "Save failed");
}

void Engine::doLoad(int32_t slot) {
    char path[64];
    SaveManager::getSlotPath(slot, path, sizeof(path));
    int32_t turnNumber = 1;
    int32_t currentPlayerIndex = 0;
    const bool ok = SaveManager::loadGame(gameState_, turnNumber, currentPlayerIndex, path);
    if (ok) {
        turnManager_.restore(gameState_,
                              [](GameState& state, PlayerId pid) { AIController::takeTurn(state, pid); },
                              turnNumber, currentPlayerIndex);
        selectedUnitId_ = kInvalidUnitId;
        activeCityId_ = kInvalidCityId;
        gameOver_ = false;
        winnerId_ = kInvalidPlayerId;
        wonByScore_ = false;
        particles_.clear();
        anims_.clear();
        state_ = EngineState::Playing;
        setStatus("Game loaded");
    } else {
        setStatus("Load failed");
    }
}

// =========================================================================
// Update / top-level render dispatch
// =========================================================================

std::vector<UIButton> Engine::buildQuickActionButtons() const {
    std::vector<UIButton> buttons;
    constexpr float y = 150.0f;
    constexpr float w = 70.0f;
    constexpr float h = 20.0f;
    constexpr float gap = 4.0f;
    float x = 8.0f;

    const bool canRoad = selectedUnitId_ != kInvalidUnitId && gameState_.canBuildRoad(selectedUnitId_);
    const bool canHeal = selectedUnitId_ != kInvalidUnitId && gameState_.canHealUnit(selectedUnitId_);
    const bool canDisband = selectedUnitId_ != kInvalidUnitId && gameState_.canDisbandUnit(selectedUnitId_);

    buttons.emplace_back(x, y, w, h, "Build Road", kActionQuickBuildRoad, canRoad);
    x += w + gap;
    buttons.emplace_back(x, y, w, h, "Heal", kActionQuickHeal, canHeal);
    x += w + gap;
    const bool awaitingConfirm = canDisband && pendingDisbandUnitId_ == selectedUnitId_;
    buttons.emplace_back(x, y, w, h, awaitingConfirm ? "Confirm?" : "Disband", kActionQuickDisband,
                          canDisband, awaitingConfirm);
    x += w + gap;
    buttons.emplace_back(x, y, w, h, "Next Unit", kActionQuickNextUnit, true);
    return buttons;
}

void Engine::update(float dt) {
    camera_.update(dt);
    particles_.update(dt);
    anims_.update(dt);

    if (statusMessageTimer_ > 0.0f) {
        statusMessageTimer_ -= dt;
    }
    if (turnBannerTimer_ > 0.0f) {
        turnBannerTimer_ -= dt;
    }
    if (pendingDisbandTimer_ > 0.0f) {
        pendingDisbandTimer_ -= dt;
        if (pendingDisbandTimer_ <= 0.0f) {
            pendingDisbandUnitId_ = kInvalidUnitId;
        }
    }
    if (pendingPauseConfirmTimer_ > 0.0f) {
        pendingPauseConfirmTimer_ -= dt;
        if (pendingPauseConfirmTimer_ <= 0.0f) {
            pendingPauseConfirmAction_ = -1;
        }
    }

    if (state_ == EngineState::Playing && !gameOver_) {
        std::vector<CityId> humanCitiesBefore;
        CityId humanCapitalBefore = kInvalidCityId;
        if (const Player* human = gameState_.player(kHumanPlayerId)) {
            humanCitiesBefore = human->cityIds;
            humanCapitalBefore = human->capitalCityId;
        }
        const bool wasHumanTurn = turnManager_.isCurrentPlayerHuman();

        turnManager_.update();

        if (!wasHumanTurn) {
            // One or more AI turns resolved this frame; check whether the
            // human lost any city to capture during it (destroyed cities
            // don't exist as a concept -- only captures change hands --
            // so any id that vanished from the human's list but still
            // resolves to a City was taken by an enemy).
            if (const Player* human = gameState_.player(kHumanPlayerId)) {
                for (CityId cid : humanCitiesBefore) {
                    const bool stillOwned =
                        std::find(human->cityIds.begin(), human->cityIds.end(), cid) != human->cityIds.end();
                    if (stillOwned) continue;
                    if (const City* c = gameState_.city(cid)) {
                        const bool wasCapital = (cid == humanCapitalBefore);
                        setStatus(wasCapital ? "Your capital has fallen!" : "A city was captured!", 3.0f);
                        cursor_ = c->coord;
                        camera_.setPosition(hexWorldCenter(c->coord));
                    }
                }
            }
        }

        const PlayerId current = turnManager_.currentPlayerId();
        if (current != lastBannerPlayerId_) {
            lastBannerPlayerId_ = current;
            turnBannerTimer_ = 1.6f;
        }

        const PlayerId dominationWinner = gameState_.checkVictory();
        if (dominationWinner != kInvalidPlayerId && gameState_.players().size() > 1) {
            gameOver_ = true;
            winnerId_ = dominationWinner;
            wonByScore_ = false;
        } else if (turnManager_.turnNumber() > GameState::kTurnLimit) {
            PlayerId best = kInvalidPlayerId;
            int32_t bestScore = -1;
            for (const Player& p : gameState_.players()) {
                const int32_t score = gameState_.computeScore(p.id);
                if (score > bestScore) {
                    bestScore = score;
                    best = p.id;
                }
            }
            gameOver_ = true;
            winnerId_ = best;
            wonByScore_ = true;
        }
    }
}

void Engine::render() {
    switch (state_) {
        case EngineState::MainMenu:
            renderMainMenu();
            return;
        case EngineState::NewGameSetup:
            renderNewGameSetup();
            return;
        case EngineState::PauseMenu:
            // Render the frozen game world behind the pause overlay so
            // context isn't lost while paused.
            renderTopScreen();
            renderPauseMenu();
            return;
        case EngineState::CityPanel:
            renderTopScreen();
            renderCityPanel();
            return;
        case EngineState::TechPanel:
            renderTopScreen();
            renderTechPanel();
            return;
        case EngineState::SettingsPanel:
            renderTopScreen();
            renderSettingsPanel();
            return;
        case EngineState::SaveSlotPanel:
            renderTopScreen();
            renderSaveSlotPanel();
            return;
        case EngineState::Playing:
            renderTopScreen();
            renderBottomScreen();
            return;
    }
}

Vec2 Engine::hexWorldCenter(const HexCoord& c) const {
    return hexToPixel(c, kBaseHexSize);
}

bool Engine::tileVisibleToHuman(const HexCoord& c) const {
    return gameState_.fog().stateAt(kHumanPlayerId, c, gameState_.grid()) != VisibilityState::Unexplored;
}

// =========================================================================
// World rendering (top screen) + HUD
// =========================================================================

void Engine::renderTopScreen() {
    renderer_.beginTopScreen();
    renderer_.clear(Color4(6, 6, 14));

    const float hexRadius = camera_.effectiveHexSize(kBaseHexSize);
    const HexGrid& grid = gameState_.grid();

    grid.forEachTile([&](const Tile& tile) {
        const VisibilityState vis = gameState_.fog().stateAt(kHumanPlayerId, tile.coord, grid);
        if (vis == VisibilityState::Unexplored) return;

        const Vec2 worldPos = hexWorldCenter(tile.coord);
        const Vec2 screenPos = camera_.worldToScreen(worldPos);
        if (screenPos.x < -hexRadius * 2.0f || screenPos.x > 400.0f + hexRadius * 2.0f ||
            screenPos.y < -hexRadius * 2.0f || screenPos.y > 240.0f + hexRadius * 2.0f) {
            return;
        }

        Color4 fill = Renderer::terrainColor(tile.terrain);
        if (vis == VisibilityState::Explored) {
            fill.r = static_cast<uint8_t>(fill.r * 0.55f);
            fill.g = static_cast<uint8_t>(fill.g * 0.55f);
            fill.b = static_cast<uint8_t>(fill.b * 0.55f);
        }
        renderer_.drawHex(screenPos, hexRadius, fill);

        if (tile.owner != kInvalidPlayerId) {
            if (const Player* owner = gameState_.player(tile.owner)) {
                renderer_.drawHexOutline(screenPos, hexRadius * 0.92f, owner->color, 1.0f);
            }
        }

        if (vis == VisibilityState::Visible && tile.resource != ResourceType::None) {
            renderer_.drawCircle(screenPos, hexRadius * 0.22f,
                                  Renderer::resourceMarkerColor(tile.resource));
        }

        if (vis == VisibilityState::Visible) {
            if (const City* c = gameState_.cityAt(tile.coord)) {
                if (const Player* owner = gameState_.player(c->owner)) {
                    const float pulse = anims_.getCityPulseScale(c->id);
                    const float half = hexRadius * 0.35f * pulse;
                    renderer_.drawRect(screenPos.x - half, screenPos.y - half, half * 2.0f, half * 2.0f,
                                        owner->color);
                    renderer_.drawRectOutline(screenPos.x - half, screenPos.y - half, half * 2.0f,
                                               half * 2.0f, Color4(0, 0, 0), 1.0f);
                    if (c->isProducing()) {
                        renderer_.drawCircle(Vec2(screenPos.x, screenPos.y + half + hexRadius * 0.18f),
                                              hexRadius * 0.12f, Color4(255, 220, 100));
                    }
                }
            }
            if (const Unit* u = gameState_.unitAt(tile.coord)) {
                if (const Player* owner = gameState_.player(u->owner)) {
                    const Vec2 animOffsetWorld = anims_.getUnitOffset(u->id);
                    const Vec2 animatedScreenPos = camera_.worldToScreen(worldPos + animOffsetWorld);
                    renderer_.drawCircle(animatedScreenPos, hexRadius * 0.45f, owner->color);
                    if (u->veteran) {
                        renderer_.drawCircle(animatedScreenPos, hexRadius * 0.18f, Color4(255, 215, 90));
                    }
                    const float pct = static_cast<float>(u->health) / static_cast<float>(u->maxHealth());
                    const float barW = hexRadius * 1.1f;
                    renderer_.drawRect(animatedScreenPos.x - barW * 0.5f,
                                        animatedScreenPos.y + hexRadius * 0.55f,
                                        barW, 2.5f, Color4(40, 40, 40));
                    renderer_.drawRect(animatedScreenPos.x - barW * 0.5f,
                                        animatedScreenPos.y + hexRadius * 0.55f,
                                        barW * clampf(pct, 0.0f, 1.0f), 2.5f, Color4(90, 220, 90));
                    if (u->id == selectedUnitId_) {
                        renderer_.drawHexOutline(animatedScreenPos, hexRadius, Color4(255, 255, 255), 2.0f);
                    }
                }
            }
        }

        if (tile.coord == cursor_) {
            renderer_.drawHexOutline(screenPos, hexRadius, Color4(255, 240, 80), 2.0f);
        }

        if (showDebugOverlay_ && vis == VisibilityState::Visible) {
            renderer_.drawTextf(screenPos.x - hexRadius * 0.4f, screenPos.y - hexRadius * 0.15f,
                                 0.28f, Color4(255, 255, 255, 200), "%d,%d", tile.coord.q, tile.coord.r);
        }
    });

    particles_.render(renderer_);

    if (state_ == EngineState::Playing) {
        renderHUD();
        renderTutorialOverlay();
    }
}

void Engine::renderHUD() {
    if (gameOver_) {
        renderer_.drawRect(50.0f, 70.0f, 300.0f, 100.0f, Color4(10, 10, 10, 230));
        renderer_.drawRectOutline(50.0f, 70.0f, 300.0f, 100.0f, Color4(255, 255, 255), 2.0f);
        const Player* winner = gameState_.player(winnerId_);
        char winnerName[24];
        if (winner && winner->id == kHumanPlayerId) {
            std::snprintf(winnerName, sizeof(winnerName), "You");
        } else if (winner) {
            std::snprintf(winnerName, sizeof(winnerName), "Rival %d", winner->id);
        } else {
            std::snprintf(winnerName, sizeof(winnerName), "Nobody");
        }
        renderer_.drawTextf(65.0f, 82.0f, 0.55f, Color4(255, 235, 120), "%s win%s!", winnerName,
                             (winner && winner->id == kHumanPlayerId) ? "" : "s");
        if (wonByScore_) {
            renderer_.drawText(65.0f, 104.0f, 0.4f, Color4(220, 220, 220), "Turn limit reached -- highest score wins");
            float y = 122.0f;
            for (const Player& p : gameState_.players()) {
                char label[16];
                if (p.id == kHumanPlayerId) {
                    std::snprintf(label, sizeof(label), "You");
                } else {
                    std::snprintf(label, sizeof(label), "Rival %d", p.id);
                }
                renderer_.drawTextf(65.0f, y, 0.38f, Color4(200, 210, 230), "%s: %d pts",
                                     label, gameState_.computeScore(p.id));
                y += 14.0f;
            }
        } else {
            renderer_.drawText(65.0f, 104.0f, 0.4f, Color4(220, 220, 220), "Victory by domination!");
        }
        renderer_.drawText(65.0f, 152.0f, 0.4f, Color4(220, 220, 220), "Press START for menu");
        return;
    }

    if (const Player* human = gameState_.player(kHumanPlayerId)) {
        renderer_.drawTextf(4.0f, 4.0f, 0.5f, Color4(255, 240, 200), "Turn %d/%d   Stars: %d",
                             turnManager_.turnNumber(), GameState::kTurnLimit, human->stars);
    }

    if (turnBannerTimer_ > 0.0f) {
        const Player* current = gameState_.player(turnManager_.currentPlayerId());
        char label[32];
        if (current && current->id == kHumanPlayerId) {
            std::snprintf(label, sizeof(label), "Your turn!");
        } else if (current) {
            std::snprintf(label, sizeof(label), "Rival %d's turn", current->id);
        } else {
            label[0] = '\0';
        }
        if (label[0] != '\0') {
            const float alpha = clampf(turnBannerTimer_ / 1.6f, 0.0f, 1.0f);
            renderer_.drawRect(120.0f, 30.0f, 160.0f, 22.0f, Color4(10, 10, 10, static_cast<uint8_t>(180 * alpha)));
            renderer_.drawText(130.0f, 34.0f, 0.42f,
                                Color4(255, 235, 150, static_cast<uint8_t>(255 * alpha)), label);
        }
    }

    if (statusMessageTimer_ > 0.0f && statusMessage_[0] != '\0') {
        renderer_.drawText(4.0f, 20.0f, 0.5f, Color4(200, 255, 200), statusMessage_);
    }

    if (showDebugOverlay_) {
        renderer_.drawTextf(4.0f, 206.0f, 0.4f, Color4(180, 255, 180), "FPS: %.0f (%.1fms)",
                             smoothedFrameTimeMs_ > 0.0f ? 1000.0f / smoothedFrameTimeMs_ : 0.0f,
                             smoothedFrameTimeMs_);
        renderer_.drawTextf(4.0f, 220.0f, 0.4f, Color4(180, 180, 255),
                             "cursor (%d,%d) cam(%.0f,%.0f) zoom %.2f sel:%d",
                             cursor_.q, cursor_.r, camera_.position().x, camera_.position().y,
                             camera_.zoom(), selectedUnitId_);
    }
}

void Engine::renderBottomScreen() {
    renderer_.beginBottomScreen();
    renderer_.clear(Color4(18, 18, 28));

    // Minimap occupies a fixed-height strip at the top so it never
    // collides with the unit info / action panel below it, regardless
    // of map size.
    constexpr float miniHexSize = 3.1f;
    constexpr float miniOffsetY = 4.0f;
    const HexGrid& grid = gameState_.grid();
    const float offsetX = 160.0f - (static_cast<float>(grid.width()) * miniHexSize * 0.75f);

    grid.forEachTile([&](const Tile& tile) {
        const VisibilityState vis = gameState_.fog().stateAt(kHumanPlayerId, tile.coord, grid);
        if (vis == VisibilityState::Unexplored) return;

        const Vec2 basePos = hexToPixel(tile.coord, miniHexSize);
        const Vec2 screenPos(basePos.x + offsetX, basePos.y + miniOffsetY);
        Color4 fill = Renderer::terrainColor(tile.terrain);
        if (vis == VisibilityState::Explored) {
            fill.r = static_cast<uint8_t>(fill.r * 0.5f);
            fill.g = static_cast<uint8_t>(fill.g * 0.5f);
            fill.b = static_cast<uint8_t>(fill.b * 0.5f);
        }
        renderer_.drawHex(screenPos, miniHexSize, fill);
        if (tile.coord == cursor_) {
            renderer_.drawHexOutline(screenPos, miniHexSize, Color4(255, 240, 80), 1.5f);
        }
    });

    renderer_.drawRectOutline(offsetX - 4.0f, miniOffsetY - 4.0f,
                               static_cast<float>(grid.width()) * miniHexSize * 1.5f + 8.0f,
                               static_cast<float>(grid.height()) * miniHexSize * 1.732f + 8.0f,
                               Color4(60, 60, 70), 1.0f);

    // Selected-unit info block.
    if (selectedUnitId_ != kInvalidUnitId) {
        if (const Unit* u = gameState_.unit(selectedUnitId_)) {
            const UnitStats& stats = statsFor(u->type);
            renderer_.drawTextf(8.0f, 98.0f, 0.42f, Color4(255, 255, 255),
                                 "%s%s  HP %d/%d  MOV %.1f/%d", stats.name, u->veteran ? " (Vet)" : "",
                                 u->health, u->maxHealth(), u->movesLeft, stats.movement);
            renderer_.drawTextf(8.0f, 113.0f, 0.38f, Color4(200, 200, 200),
                                 "ATK %d  DEF %d  RNG %d  Kills %d", stats.attack, stats.defense,
                                 stats.attackRange, u->killCount);
            if (u->isBoat() && u->passengerId != kInvalidUnitId) {
                if (const Unit* cargo = gameState_.unit(u->passengerId)) {
                    renderer_.drawTextf(8.0f, 128.0f, 0.36f, Color4(180, 220, 255),
                                         "Carrying: %s", statsFor(cargo->type).name);
                }
            } else if (u->embarked) {
                renderer_.drawText(8.0f, 128.0f, 0.36f, Color4(180, 220, 255), "Aboard a boat");
            }
        }
    } else {
        renderer_.drawText(8.0f, 98.0f, 0.42f, Color4(180, 180, 180), "No unit selected");
    }

    // Quick action buttons (Build Road / Heal / Disband / Next Unit).
    for (const UIButton& b : buildQuickActionButtons()) {
        renderer_.drawButton(b);
    }
    renderer_.drawText(8.0f, 172.0f, 0.32f, Color4(150, 150, 155), "Road: 2 stars + Roads tech, on land");
    renderer_.drawText(8.0f, 184.0f, 0.32f, Color4(150, 150, 155), "Heal: stand on your city | Disband: half refund");

    renderer_.drawText(8.0f, 200.0f, 0.4f, Color4(180, 180, 180), "A:act  B:cancel  X:city/found");
    renderer_.drawText(8.0f, 214.0f, 0.4f, Color4(180, 180, 180), "Y:end turn   START:menu");
}

// =========================================================================
// Main menu
// =========================================================================

void Engine::handleMainMenuInput(const FrameInput& in) {
    if (in.gesture != TouchGesture::Tap) return;

    const float cx = 160.0f;
    const float w = 200.0f;
    const float x = cx - w * 0.5f;
    float y = 70.0f;
    constexpr float h = 30.0f;
    constexpr float gap = 12.0f;

    UIButton newGame(x, y, w, h, "New Game", kActionMenuNewGame);
    y += h + gap;
    UIButton continueGame(x, y, w, h, "Continue", kActionMenuContinue, anySlotHasSave());
    y += h + gap;
    UIButton settingsBtn(x, y, w, h, "Settings", kActionMenuSettings);
    y += h + gap;
    UIButton quitBtn(x, y, w, h, "Quit", kActionMenuQuit);

    const Vec2 p = in.tapPos;
    if (newGame.contains(p)) {
        state_ = EngineState::NewGameSetup;
    } else if (continueGame.contains(p) && continueGame.enabled) {
        slotPanelMode_ = SlotPanelMode::Load;
        slotPanelReturnState_ = EngineState::MainMenu;
        state_ = EngineState::SaveSlotPanel;
    } else if (settingsBtn.contains(p)) {
        settingsReturnState_ = EngineState::MainMenu;
        state_ = EngineState::SettingsPanel;
    } else if (quitBtn.contains(p)) {
        running_ = false;
    }
}

void Engine::renderMainMenu() {
    renderer_.beginTopScreen();
    renderer_.clear(Color4(12, 18, 30));

    // Decorative hex backdrop using the same palette as in-game terrain.
    constexpr float bgHexSize = 16.0f;
    for (int32_t row = -1; row < 16; ++row) {
        for (int32_t col = -1; col < 16; ++col) {
            const HexCoord hc(col - (row - (row & 1)) / 2, row);
            const Vec2 pos = hexToPixel(hc, bgHexSize) + Vec2(20.0f, -20.0f);
            const TerrainType t = ((row + col) % 5 == 0) ? TerrainType::Forest :
                                   ((row + col) % 3 == 0) ? TerrainType::Hills : TerrainType::Plains;
            Color4 c = Renderer::terrainColor(t);
            c.r = static_cast<uint8_t>(c.r * 0.4f);
            c.g = static_cast<uint8_t>(c.g * 0.4f);
            c.b = static_cast<uint8_t>(c.b * 0.4f);
            renderer_.drawHex(pos, bgHexSize, c);
        }
    }

    renderer_.drawText(90.0f, 30.0f, 0.9f, Color4(255, 235, 150), "HEXONQUEST");
    renderer_.drawText(95.0f, 55.0f, 0.4f, Color4(200, 200, 210), "A tribe-building strategy game");

    renderer_.beginBottomScreen();
    renderer_.clear(Color4(16, 16, 24));

    const bool hasSave = anySlotHasSave();
    const float cx = 160.0f;
    const float w = 200.0f;
    const float x = cx - w * 0.5f;
    float y = 70.0f;
    constexpr float h = 30.0f;
    constexpr float gap = 12.0f;

    renderer_.drawButton(UIButton(x, y, w, h, "New Game", kActionMenuNewGame));
    y += h + gap;
    renderer_.drawButton(UIButton(x, y, w, h, "Continue", kActionMenuContinue, hasSave));
    y += h + gap;
    renderer_.drawButton(UIButton(x, y, w, h, "Settings", kActionMenuSettings));
    y += h + gap;
    renderer_.drawButton(UIButton(x, y, w, h, "Quit", kActionMenuQuit));
}

// =========================================================================
// New game setup (map size / opponent count)
// =========================================================================

std::vector<UIButton> Engine::buildNewGameSetupButtons() const {
    std::vector<UIButton> buttons;
    constexpr float x = 40.0f;
    constexpr float arrowW = 30.0f;
    constexpr float w = 240.0f;
    constexpr float h = 28.0f;
    float y = 50.0f;

    buttons.emplace_back(x, y, arrowW, h, "-", kActionSetupMapSizeDown, setupMapSizeIndex_ > 0);
    buttons.emplace_back(x + w - arrowW, y, arrowW, h, "+", kActionSetupMapSizeUp,
                          setupMapSizeIndex_ < kMapSizeCount - 1);
    y += h + 20.0f;
    buttons.emplace_back(x, y, arrowW, h, "-", kActionSetupOpponentsDown, setupOpponentCount_ > 1);
    buttons.emplace_back(x + w - arrowW, y, arrowW, h, "+", kActionSetupOpponentsUp,
                          setupOpponentCount_ < kMaxOpponentsPerSize[setupMapSizeIndex_]);
    y += h + 26.0f;
    buttons.emplace_back(x, y, w, h + 4.0f, "Start Game", kActionSetupStart);
    y += h + 4.0f + 10.0f;
    buttons.emplace_back(x, y, w, h, "Back", kActionSetupBack);
    return buttons;
}

void Engine::handleNewGameSetupInput(const FrameInput& in) {
    if (in.pressed(KEY_B) || in.pressed(KEY_START)) {
        state_ = EngineState::MainMenu;
        return;
    }
    if (in.gesture != TouchGesture::Tap) return;

    for (const UIButton& b : buildNewGameSetupButtons()) {
        if (!b.enabled || !b.contains(in.tapPos)) continue;
        switch (b.actionId) {
            case kActionSetupMapSizeDown:
                setupMapSizeIndex_ = std::max(0, setupMapSizeIndex_ - 1);
                setupOpponentCount_ = std::min(setupOpponentCount_, kMaxOpponentsPerSize[setupMapSizeIndex_]);
                break;
            case kActionSetupMapSizeUp:
                setupMapSizeIndex_ = std::min(kMapSizeCount - 1, setupMapSizeIndex_ + 1);
                setupOpponentCount_ = std::min(setupOpponentCount_, kMaxOpponentsPerSize[setupMapSizeIndex_]);
                break;
            case kActionSetupOpponentsDown:
                setupOpponentCount_ = std::max(1, setupOpponentCount_ - 1);
                break;
            case kActionSetupOpponentsUp:
                setupOpponentCount_ = std::min(kMaxOpponentsPerSize[setupMapSizeIndex_], setupOpponentCount_ + 1);
                break;
            case kActionSetupStart:
                settings_.lastMapSizeIndex = setupMapSizeIndex_;
                settings_.lastOpponentCount = setupOpponentCount_;
                settings_.save();
                startNewGame();
                break;
            case kActionSetupBack:
                state_ = EngineState::MainMenu;
                break;
            default:
                break;
        }
        return;
    }
}

void Engine::renderNewGameSetup() {
    renderer_.beginTopScreen();
    renderer_.clear(Color4(12, 18, 30));
    renderer_.drawText(110.0f, 90.0f, 0.55f, Color4(255, 235, 150), "New Game");
    renderer_.drawText(70.0f, 115.0f, 0.4f, Color4(200, 200, 210),
                        "Choose a map size and how many rival tribes to face.");

    renderer_.beginBottomScreen();
    renderer_.clear(Color4(16, 16, 24));

    const MapSizeOption& size = kMapSizes[setupMapSizeIndex_];
    renderer_.drawTextf(40.0f, 30.0f, 0.45f, Color4(255, 255, 255), "Map Size: %s (%dx%d)",
                         size.label, size.width, size.height);
    renderer_.drawTextf(40.0f, 82.0f, 0.45f, Color4(255, 255, 255), "Rival Tribes: %d",
                         setupOpponentCount_);

    for (const UIButton& b : buildNewGameSetupButtons()) {
        renderer_.drawButton(b);
    }
}

// =========================================================================
// Pause menu
// =========================================================================

void Engine::handlePauseMenuInput(const FrameInput& in) {
    if (in.pressed(KEY_START) || in.pressed(KEY_B)) {
        pendingPauseConfirmAction_ = -1;
        state_ = EngineState::Playing;
        return;
    }
    if (in.gesture != TouchGesture::Tap) return;

    const float x = 60.0f;
    const float w = 200.0f;
    float y = 18.0f;
    constexpr float h = 24.0f;
    constexpr float gap = 5.0f;

    const bool confirmingMainMenu = pendingPauseConfirmAction_ == kActionPauseMainMenu;
    const bool confirmingQuitApp = pendingPauseConfirmAction_ == kActionPauseQuitApp;

    UIButton resume(x, y, w, h, "Resume", kActionPauseResume); y += h + gap;
    UIButton save(x, y, w, h, "Save Game", kActionPauseSave); y += h + gap;
    UIButton load(x, y, w, h, "Load Game", kActionPauseLoad, anySlotHasSave());
    y += h + gap;
    UIButton settingsBtn(x, y, w, h, "Settings", kActionPauseSettings); y += h + gap;
    UIButton techBtn(x, y, w, h, "Technology", kActionPauseTechnology,
                      gameState_.player(kHumanPlayerId) != nullptr);
    y += h + gap;
    UIButton mainMenu(x, y, w, h, confirmingMainMenu ? "Confirm quit to menu?" : "Quit to Main Menu",
                       kActionPauseMainMenu);
    y += h + gap;
    UIButton quitApp(x, y, w, h, confirmingQuitApp ? "Confirm exit?" : "Exit Application",
                      kActionPauseQuitApp);

    const Vec2 p = in.tapPos;
    if (resume.contains(p)) {
        pendingPauseConfirmAction_ = -1;
        state_ = EngineState::Playing;
    } else if (save.contains(p)) {
        pendingPauseConfirmAction_ = -1;
        slotPanelMode_ = SlotPanelMode::Save;
        slotPanelReturnState_ = EngineState::PauseMenu;
        state_ = EngineState::SaveSlotPanel;
    } else if (load.contains(p) && load.enabled) {
        pendingPauseConfirmAction_ = -1;
        slotPanelMode_ = SlotPanelMode::Load;
        slotPanelReturnState_ = EngineState::PauseMenu;
        state_ = EngineState::SaveSlotPanel;
    } else if (settingsBtn.contains(p)) {
        pendingPauseConfirmAction_ = -1;
        settingsReturnState_ = EngineState::PauseMenu;
        state_ = EngineState::SettingsPanel;
    } else if (techBtn.contains(p) && techBtn.enabled) {
        pendingPauseConfirmAction_ = -1;
        techPanelReturnState_ = EngineState::Playing;
        state_ = EngineState::TechPanel;
    } else if (mainMenu.contains(p)) {
        if (confirmingMainMenu) {
            pendingPauseConfirmAction_ = -1;
            state_ = EngineState::MainMenu;
        } else {
            pendingPauseConfirmAction_ = kActionPauseMainMenu;
            pendingPauseConfirmTimer_ = 3.0f;
        }
    } else if (quitApp.contains(p)) {
        if (confirmingQuitApp) {
            running_ = false;
        } else {
            pendingPauseConfirmAction_ = kActionPauseQuitApp;
            pendingPauseConfirmTimer_ = 3.0f;
        }
    }
}

void Engine::renderPauseMenu() {
    renderer_.beginBottomScreen();
    renderer_.clear(Color4(14, 14, 20));
    renderer_.drawText(70.0f, 4.0f, 0.55f, Color4(255, 240, 200), "Paused");

    const float x = 60.0f;
    const float w = 200.0f;
    float y = 18.0f;
    constexpr float h = 24.0f;
    constexpr float gap = 5.0f;
    const bool hasSave = anySlotHasSave();
    const bool confirmingMainMenu = pendingPauseConfirmAction_ == kActionPauseMainMenu;
    const bool confirmingQuitApp = pendingPauseConfirmAction_ == kActionPauseQuitApp;

    renderer_.drawButton(UIButton(x, y, w, h, "Resume", kActionPauseResume)); y += h + gap;
    renderer_.drawButton(UIButton(x, y, w, h, "Save Game", kActionPauseSave)); y += h + gap;
    renderer_.drawButton(UIButton(x, y, w, h, "Load Game", kActionPauseLoad, hasSave)); y += h + gap;
    renderer_.drawButton(UIButton(x, y, w, h, "Settings", kActionPauseSettings)); y += h + gap;
    renderer_.drawButton(UIButton(x, y, w, h, "Technology", kActionPauseTechnology,
                                   gameState_.player(kHumanPlayerId) != nullptr));
    y += h + gap;
    renderer_.drawButton(UIButton(x, y, w, h, confirmingMainMenu ? "Confirm quit to menu?" : "Quit to Main Menu",
                                   kActionPauseMainMenu, true, confirmingMainMenu));
    y += h + gap;
    renderer_.drawButton(UIButton(x, y, w, h, confirmingQuitApp ? "Confirm exit?" : "Exit Application",
                                   kActionPauseQuitApp, true, confirmingQuitApp));
}

// =========================================================================
// City production panel
// =========================================================================

std::vector<UIButton> Engine::buildCityPanelButtons() const {
    std::vector<UIButton> buttons;
    const City* c = gameState_.city(activeCityId_);
    const Player* p = gameState_.player(kHumanPlayerId);
    if (!c || !p) return buttons;

    float y = 24.0f;
    constexpr float h = 19.0f;
    constexpr float gap = 2.0f;
    constexpr float x = 8.0f;
    constexpr float w = 220.0f;

    for (size_t i = 0; i < static_cast<size_t>(UnitType::Count); ++i) {
        const UnitType type = static_cast<UnitType>(i);
        const UnitStats& stats = statsFor(type);
        const bool unlocked = p->unitUnlocked(type);
        char label[40];
        std::snprintf(label, sizeof(label), "%s (%d star%s)", stats.name, stats.cost,
                      stats.cost == 1 ? "" : "s");
        const bool isCurrent = c->producing == type;
        buttons.emplace_back(x, y, w, h, label, kActionCityProduceBase + static_cast<int32_t>(i),
                              unlocked, isCurrent);
        y += h + gap;
    }

    buttons.emplace_back(x, y, w, h, "Research technology...", kActionCityOpenResearch);
    y += h + gap;
    buttons.emplace_back(x, y, w, h, "Close", kActionCityClose);
    return buttons;
}

void Engine::handleCityPanelInput(const FrameInput& in) {
    if (in.pressed(KEY_B) || in.pressed(KEY_START)) {
        state_ = EngineState::Playing;
        return;
    }
    if (in.gesture != TouchGesture::Tap) return;

    const std::vector<UIButton> buttons = buildCityPanelButtons();
    for (const UIButton& b : buttons) {
        if (!b.enabled || !b.contains(in.tapPos)) continue;
        if (b.actionId == kActionCityOpenResearch) {
            techPanelReturnState_ = EngineState::CityPanel;
            state_ = EngineState::TechPanel;
        } else if (b.actionId == kActionCityClose) {
            state_ = EngineState::Playing;
        } else if (b.actionId >= kActionCityProduceBase &&
                   b.actionId < kActionCityProduceBase + static_cast<int32_t>(UnitType::Count)) {
            const UnitType type = static_cast<UnitType>(b.actionId - kActionCityProduceBase);
            if (gameState_.queueProduction(activeCityId_, type)) {
                setStatus("Production set");
            }
        }
        return;
    }
}

void Engine::renderCityPanel() {
    renderer_.beginBottomScreen();
    renderer_.clear(Color4(16, 16, 24));

    const City* c = gameState_.city(activeCityId_);
    if (!c) {
        state_ = EngineState::Playing;
        return;
    }

    renderer_.drawTextf(8.0f, 4.0f, 0.5f, Color4(255, 240, 200), "%s (Lv %d, Pop %d)",
                         c->name, c->level, c->population);

    const std::vector<UIButton> buttons = buildCityPanelButtons();
    for (const UIButton& b : buttons) {
        renderer_.drawButton(b);
    }
}

// =========================================================================
// Technology research panel
// =========================================================================

std::vector<UIButton> Engine::buildTechPanelButtons() const {
    std::vector<UIButton> buttons;
    const Player* p = gameState_.player(kHumanPlayerId);
    if (!p) return buttons;

    float y = 24.0f;
    constexpr float h = 16.0f;
    constexpr float gap = 2.0f;
    constexpr float x = 8.0f;
    constexpr float w = 220.0f;

    auto addTechButton = [&](TechId tech) {
        const TechDef& def = techDef(tech);
        const bool already = p->hasTech(tech);
        const bool affordable = p->canResearch(tech);
        char label[40];
        if (already) {
            std::snprintf(label, sizeof(label), "%s (researched)", def.name);
        } else {
            std::snprintf(label, sizeof(label), "%s (%d stars)", def.name, def.cost);
        }
        buttons.emplace_back(x, y, w, h, label, kActionTechBase + static_cast<int32_t>(tech),
                              affordable && !already, already);
        y += h + gap;
    };

    buttons.push_back(UIButton::header(x, y, w, "Tier 1"));
    y += 12.0f;
    for (size_t i = 0; i < static_cast<size_t>(TechId::Count); ++i) {
        const TechId tech = static_cast<TechId>(i);
        if (techDef(tech).prereq == kNoPrereq) addTechButton(tech);
    }

    y += 4.0f;
    buttons.push_back(UIButton::header(x, y, w, "Tier 2 (requires a Tier 1 tech)"));
    y += 12.0f;
    for (size_t i = 0; i < static_cast<size_t>(TechId::Count); ++i) {
        const TechId tech = static_cast<TechId>(i);
        if (techDef(tech).prereq != kNoPrereq) addTechButton(tech);
    }

    y += 4.0f;
    buttons.emplace_back(x, y, w, h, "Back", kActionTechBack);
    return buttons;
}

void Engine::handleTechPanelInput(const FrameInput& in) {
    if (in.pressed(KEY_B) || in.pressed(KEY_START)) {
        state_ = techPanelReturnState_;
        return;
    }
    if (in.gesture != TouchGesture::Tap) return;

    const std::vector<UIButton> buttons = buildTechPanelButtons();
    for (const UIButton& b : buttons) {
        if (!b.contains(in.tapPos)) continue;
        if (b.actionId == kActionTechBack) {
            state_ = techPanelReturnState_;
        } else if (b.enabled && b.actionId >= kActionTechBase &&
                   b.actionId < kActionTechBase + static_cast<int32_t>(TechId::Count)) {
            const TechId tech = static_cast<TechId>(b.actionId - kActionTechBase);
            if (gameState_.researchTech(kHumanPlayerId, tech)) {
                setStatus("Researched!");
            }
        }
        return;
    }
}

void Engine::renderTechPanel() {
    renderer_.beginBottomScreen();
    renderer_.clear(Color4(16, 16, 24));
    renderer_.drawText(8.0f, 4.0f, 0.5f, Color4(255, 240, 200), "Technology");

    const std::vector<UIButton> buttons = buildTechPanelButtons();
    for (const UIButton& b : buttons) {
        renderer_.drawButton(b);
    }
}

// =========================================================================
// Settings panel
// =========================================================================

std::vector<UIButton> Engine::buildSettingsButtons() const {
    std::vector<UIButton> buttons;
    constexpr float x = 20.0f;
    constexpr float w = 60.0f;
    constexpr float h = 24.0f;
    float y = 40.0f;

    buttons.emplace_back(x, y, w, h, "Music -", kActionSettingsMusicDown);
    buttons.emplace_back(x + w + 90.0f, y, w, h, "Music +", kActionSettingsMusicUp);
    y += h + 30.0f;
    buttons.emplace_back(x, y, w, h, "SFX -", kActionSettingsSfxDown);
    buttons.emplace_back(x + w + 90.0f, y, w, h, "SFX +", kActionSettingsSfxUp);
    y += h + 30.0f;
    buttons.emplace_back(x, y, 220.0f, h, "Toggle grid coordinates", kActionSettingsToggleGrid,
                          true, showDebugOverlay_);
    y += h + 30.0f;
    buttons.emplace_back(x, y, 220.0f, h, "Back", kActionSettingsBack);
    return buttons;
}

void Engine::handleSettingsPanelInput(const FrameInput& in) {
    if (in.pressed(KEY_B) || in.pressed(KEY_START)) {
        state_ = settingsReturnState_;
        return;
    }
    if (in.gesture != TouchGesture::Tap) return;

    const std::vector<UIButton> buttons = buildSettingsButtons();
    for (const UIButton& b : buttons) {
        if (!b.contains(in.tapPos)) continue;
        switch (b.actionId) {
            case kActionSettingsMusicDown:
                settings_.musicVolume = clampf(settings_.musicVolume - 0.1f, 0.0f, 1.0f);
                audio_.setMasterMusicVolume(settings_.musicVolume);
                break;
            case kActionSettingsMusicUp:
                settings_.musicVolume = clampf(settings_.musicVolume + 0.1f, 0.0f, 1.0f);
                audio_.setMasterMusicVolume(settings_.musicVolume);
                break;
            case kActionSettingsSfxDown:
                settings_.sfxVolume = clampf(settings_.sfxVolume - 0.1f, 0.0f, 1.0f);
                audio_.setMasterSfxVolume(settings_.sfxVolume);
                break;
            case kActionSettingsSfxUp:
                settings_.sfxVolume = clampf(settings_.sfxVolume + 0.1f, 0.0f, 1.0f);
                audio_.setMasterSfxVolume(settings_.sfxVolume);
                break;
            case kActionSettingsToggleGrid:
                showDebugOverlay_ = !showDebugOverlay_;
                settings_.showGridCoordinates = showDebugOverlay_;
                break;
            case kActionSettingsBack:
                settings_.save();
                state_ = settingsReturnState_;
                break;
            default:
                break;
        }
        return;
    }
}

void Engine::renderSettingsPanel() {
    renderer_.beginBottomScreen();
    renderer_.clear(Color4(16, 16, 24));
    renderer_.drawText(20.0f, 8.0f, 0.5f, Color4(255, 240, 200), "Settings");

    renderer_.drawTextf(20.0f, 28.0f, 0.4f, Color4(200, 200, 210), "Music volume: %.0f%%",
                         settings_.musicVolume * 100.0f);
    renderer_.drawTextf(20.0f, 92.0f, 0.4f, Color4(200, 200, 210), "SFX volume: %.0f%%",
                         settings_.sfxVolume * 100.0f);

    const std::vector<UIButton> buttons = buildSettingsButtons();
    for (const UIButton& b : buttons) {
        renderer_.drawButton(b);
    }
}

// =========================================================================
// Save slot picker
// =========================================================================

std::vector<UIButton> Engine::buildSaveSlotButtons() const {
    std::vector<UIButton> buttons;
    constexpr float x = 40.0f;
    constexpr float w = 240.0f;
    constexpr float h = 34.0f;
    constexpr float gap = 10.0f;
    float y = 30.0f;

    for (int32_t slot = 0; slot < SaveManager::kSlotCount; ++slot) {
        char path[64];
        SaveManager::getSlotPath(slot, path, sizeof(path));
        const SaveManager::SlotInfo info = SaveManager::peekSlot(path);

        char label[40];
        if (info.exists) {
            std::snprintf(label, sizeof(label), "Slot %d -- Turn %d", slot + 1, info.turnNumber);
        } else {
            std::snprintf(label, sizeof(label), "Slot %d -- Empty", slot + 1);
        }

        // Saving is always allowed (overwrites); loading requires the
        // slot to actually contain a game.
        const bool enabled = (slotPanelMode_ == SlotPanelMode::Save) || info.exists;
        buttons.emplace_back(x, y, w, h, label, kActionSlotBase + slot, enabled);
        y += h + gap;
    }

    buttons.emplace_back(x, y, w, h, "Back", kActionSlotBack);
    return buttons;
}

void Engine::handleSaveSlotPanelInput(const FrameInput& in) {
    if (in.pressed(KEY_B) || in.pressed(KEY_START)) {
        state_ = slotPanelReturnState_;
        return;
    }
    if (in.gesture != TouchGesture::Tap) return;

    const std::vector<UIButton> buttons = buildSaveSlotButtons();
    for (const UIButton& b : buttons) {
        if (!b.contains(in.tapPos)) continue;
        if (b.actionId == kActionSlotBack) {
            state_ = slotPanelReturnState_;
        } else if (b.enabled && b.actionId >= kActionSlotBase &&
                   b.actionId < kActionSlotBase + SaveManager::kSlotCount) {
            const int32_t slot = b.actionId - kActionSlotBase;
            if (slotPanelMode_ == SlotPanelMode::Save) {
                doSave(slot);
                state_ = EngineState::Playing;
            } else {
                doLoad(slot); // doLoad transitions to Playing on success and sets its own status
            }
        }
        return;
    }
}

void Engine::renderSaveSlotPanel() {
    renderer_.beginBottomScreen();
    renderer_.clear(Color4(16, 16, 24));
    renderer_.drawText(40.0f, 6.0f, 0.5f, Color4(255, 240, 200),
                        slotPanelMode_ == SlotPanelMode::Save ? "Save to which slot?" : "Load which slot?");

    const std::vector<UIButton> buttons = buildSaveSlotButtons();
    for (const UIButton& b : buttons) {
        renderer_.drawButton(b);
    }
}

} // namespace poly

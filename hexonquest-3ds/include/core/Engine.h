#pragma once

#include <3ds.h>
#include "core/Types.h"
#include "core/HexCoord.h"
#include "core/Settings.h"
#include "game/GameState.h"
#include "game/TurnManager.h"
#include "render/Renderer.h"
#include "render/Camera.h"
#include "input/InputManager.h"
#include "audio/AudioManager.h"
#include "fx/ParticleSystem.h"
#include "fx/AnimationSystem.h"
#include "ui/UIWidgets.h"
#include "save/SaveManager.h"
#include <vector>

namespace poly {

enum class EngineState {
    MainMenu,
    NewGameSetup,
    Playing,
    CityPanel,
    TechPanel,
    PauseMenu,
    SettingsPanel,
    SaveSlotPanel
};

enum class SlotPanelMode { Save, Load };

// -----------------------------------------------------------------------
// Engine is the top-level owner of every subsystem and the main-loop
// driver: world/units/players (GameState), turn order (TurnManager),
// presentation (Renderer/Camera), input, and the menu state machine
// (main menu / pause / city & tech panels / settings). Constructed once
// in main().
// -----------------------------------------------------------------------
class Engine {
public:
    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool init();
    void shutdown();
    int run();

private:
    void handleInput(float dt);
    void update(float dt);
    void render();

    // --- Playing state ---------------------------------------------
    void handlePlayingInput(const FrameInput& in, float dt);
    void renderTopScreen();
    void renderBottomScreen();
    void renderHUD();

    void selectOrActOnCursor();
    void tryFoundCity();
    void endHumanTurn();
    void doSave(int32_t slot);
    void doLoad(int32_t slot);
    void startNewGame();
    void cycleToNextReadyUnit(bool forward);
    std::vector<UIButton> buildQuickActionButtons() const;

    // --- Menu / panel states -----------------------------------------
    void handleMainMenuInput(const FrameInput& in);
    void renderMainMenu();

    void handleNewGameSetupInput(const FrameInput& in);
    void renderNewGameSetup();
    std::vector<UIButton> buildNewGameSetupButtons() const;

    void handlePauseMenuInput(const FrameInput& in);
    void renderPauseMenu();

    void handleCityPanelInput(const FrameInput& in);
    void renderCityPanel();
    std::vector<UIButton> buildCityPanelButtons() const;

    void handleTechPanelInput(const FrameInput& in);
    void renderTechPanel();
    std::vector<UIButton> buildTechPanelButtons() const;

    void handleSettingsPanelInput(const FrameInput& in);
    void renderSettingsPanel();
    std::vector<UIButton> buildSettingsButtons() const;

    void handleSaveSlotPanelInput(const FrameInput& in);
    void renderSaveSlotPanel();
    std::vector<UIButton> buildSaveSlotButtons() const;

    Vec2 hexWorldCenter(const HexCoord& c) const;
    bool tileVisibleToHuman(const HexCoord& c) const;

    Renderer renderer_;
    GameState gameState_;
    TurnManager turnManager_;
    Camera2D camera_;
    InputManager input_;
    AudioManager audio_;
    ParticleSystem particles_;
    AnimationSystem anims_;
    Settings settings_;

    SoundHandle sfxMove_ = kInvalidSoundHandle;
    SoundHandle sfxAttack_ = kInvalidSoundHandle;
    SoundHandle sfxFound_ = kInvalidSoundHandle;
    SoundHandle musicTrack_ = kInvalidSoundHandle;
    bool romfsMounted_ = false;

    static constexpr PlayerId kHumanPlayerId = 0;

    EngineState state_ = EngineState::MainMenu;
    EngineState settingsReturnState_ = EngineState::MainMenu;
    EngineState slotPanelReturnState_ = EngineState::MainMenu;
    EngineState techPanelReturnState_ = EngineState::CityPanel;
    SlotPanelMode slotPanelMode_ = SlotPanelMode::Load;
    CityId activeCityId_ = kInvalidCityId;

    int32_t setupMapSizeIndex_ = 1;   // index into kMapSizes
    int32_t setupOpponentCount_ = 1;  // 1..5 rival tribes

    PlayerId lastBannerPlayerId_ = kInvalidPlayerId;
    float turnBannerTimer_ = 0.0f;

    HexCoord cursor_{0, 0};
    UnitId selectedUnitId_ = kInvalidUnitId;
    UnitId pendingDisbandUnitId_ = kInvalidUnitId;
    float pendingDisbandTimer_ = 0.0f;

    int32_t pendingPauseConfirmAction_ = -1;
    float pendingPauseConfirmTimer_ = 0.0f;
    bool running_ = false;
    bool showDebugOverlay_ = false;

    bool gameOver_ = false;
    PlayerId winnerId_ = kInvalidPlayerId;
    bool wonByScore_ = false;

    char statusMessage_[64] = "";
    float statusMessageTimer_ = 0.0f;

    int32_t tutorialIndex_ = -1; // -1 == not showing a tutorial tooltip
    void startTutorialIfNeeded();
    void renderTutorialOverlay();

    u64 lastTickNs_ = 0;
    float smoothedFrameTimeMs_ = 16.6f;

    void setStatus(const char* msg, float durationSeconds = 2.0f);
};

} // namespace poly

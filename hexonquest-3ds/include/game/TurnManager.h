#pragma once

#include <cstdint>
#include <functional>
#include "game/GameState.h"

namespace poly {

// -----------------------------------------------------------------------
// Drives the turn order. Does not itself contain AI logic; when the
// active player is AI-controlled it invokes the supplied callback
// (wired to AIController::takeTurn by the Engine) once, then
// immediately advances to the next player.
// -----------------------------------------------------------------------
class TurnManager {
public:
    using AITurnCallback = std::function<void(GameState&, PlayerId)>;

    void start(GameState& state, const AITurnCallback& aiCallback) {
        state_ = &state;
        aiCallback_ = aiCallback;
        turnNumber_ = 1;
        currentPlayerIndex_ = 0;
        beginCurrentPlayerTurn();
    }

    PlayerId currentPlayerId() const {
        if (!state_ || state_->players().empty()) return kInvalidPlayerId;
        return state_->players()[static_cast<size_t>(currentPlayerIndex_)].id;
    }

    bool isCurrentPlayerHuman() const {
        if (!state_) return false;
        const Player* p = state_->player(currentPlayerId());
        return p && !p->isAI;
    }

    int32_t turnNumber() const { return turnNumber_; }
    int32_t currentPlayerIndex() const { return currentPlayerIndex_; }

    // Called by the Engine once per frame; if the active player is AI,
    // runs its turn synchronously and advances. Human turns wait for
    // an explicit endTurn() call from input.
    void update() {
        if (!state_) return;
        if (!isCurrentPlayerHuman()) {
            if (aiCallback_) aiCallback_(*state_, currentPlayerId());
            endTurn();
        }
    }

    // Reinstates turn state after a load, without re-running
    // beginPlayerTurn's economy/production side effects (those were
    // already applied and persisted before the save happened).
    void restore(GameState& state, const AITurnCallback& aiCallback,
                 int32_t turnNumber, int32_t currentPlayerIndex) {
        state_ = &state;
        aiCallback_ = aiCallback;
        turnNumber_ = turnNumber;
        currentPlayerIndex_ = currentPlayerIndex;
    }

    void endTurn() {
        if (!state_ || state_->players().empty()) return;
        const int32_t playerCount = static_cast<int32_t>(state_->players().size());
        currentPlayerIndex_ = (currentPlayerIndex_ + 1) % playerCount;
        if (currentPlayerIndex_ == 0) {
            ++turnNumber_;
        }
        beginCurrentPlayerTurn();
    }

private:
    void beginCurrentPlayerTurn() {
        if (!state_) return;
        state_->beginPlayerTurn(currentPlayerId());
    }

    GameState* state_ = nullptr;
    AITurnCallback aiCallback_;
    int32_t currentPlayerIndex_ = 0;
    int32_t turnNumber_ = 1;
};

} // namespace poly

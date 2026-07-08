#pragma once

#include <vector>
#include <functional>
#include "core/HexCoord.h"
#include "world/HexGrid.h"
#include "game/Unit.h"

namespace poly {

struct ReachableTile {
    HexCoord coord;
    float costSoFar;
    HexCoord cameFrom;
    bool hasParent;
};

// -----------------------------------------------------------------------
// Pathfinder is stateless (free functions bundled in a namespace-like
// struct for organization); callers own all storage so nothing here
// allocates beyond the returned std::vector, which is sized once and
// bounded by the small maps used on 3DS (<= a few hundred tiles).
// -----------------------------------------------------------------------
class Pathfinder {
public:
    // A predicate deciding whether `mover` may enter `tile` at all
    // (terrain + occupancy rules). Cost is looked up separately.
    using EnterPredicate = std::function<bool(const Tile& tile)>;

    // Dijkstra flood-fill of every tile reachable from `start` within
    // `movementBudget` movement points (inclusive), respecting terrain
    // move cost and the given entry predicate. Does not include tiles
    // occupied by other units unless `canEnter` says otherwise.
    static std::vector<ReachableTile> computeReachable(
        const HexGrid& grid, const HexCoord& start, float movementBudget,
        const EnterPredicate& canEnter);

    // A* shortest path from start to goal. Returns an empty vector if no
    // path exists within `maxSearchNodes` (safety cap for the 3DS's
    // limited heap). Path includes both start and goal.
    static std::vector<HexCoord> findPath(
        const HexGrid& grid, const HexCoord& start, const HexCoord& goal,
        const EnterPredicate& canEnter, int32_t maxSearchNodes = 512);
};

} // namespace poly

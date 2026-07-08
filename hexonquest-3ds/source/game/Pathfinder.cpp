#include "game/Pathfinder.h"
#include <unordered_map>
#include <algorithm>
#include <limits>

namespace poly {

namespace {

// Simple binary min-heap over (cost, HexCoord) pairs. Implemented by hand
// rather than std::priority_queue<std::pair<...>> to avoid pulling in
// <queue>'s heavier template instantiation footprint on a constrained
// toolchain, and to keep the comparator trivial.
struct HeapEntry {
    float cost;
    HexCoord coord;
};

class MinHeap {
public:
    void push(const HeapEntry& e) {
        data_.push_back(e);
        std::push_heap(data_.begin(), data_.end(), Greater{});
    }
    HeapEntry pop() {
        std::pop_heap(data_.begin(), data_.end(), Greater{});
        HeapEntry e = data_.back();
        data_.pop_back();
        return e;
    }
    bool empty() const { return data_.empty(); }
    size_t size() const { return data_.size(); }

private:
    struct Greater {
        bool operator()(const HeapEntry& a, const HeapEntry& b) const { return a.cost > b.cost; }
    };
    std::vector<HeapEntry> data_;
};

} // namespace

std::vector<ReachableTile> Pathfinder::computeReachable(
    const HexGrid& grid, const HexCoord& start, float movementBudget,
    const EnterPredicate& canEnter) {

    std::vector<ReachableTile> result;
    std::unordered_map<HexCoord, float, HexCoord::Hash> bestCost;
    std::unordered_map<HexCoord, HexCoord, HexCoord::Hash> cameFrom;

    MinHeap open;
    open.push({0.0f, start});
    bestCost[start] = 0.0f;

    std::array<HexCoord, 6> neighbors;

    while (!open.empty()) {
        const HeapEntry cur = open.pop();
        // Stale entry check: a cheaper path may have been found since
        // this entry was pushed.
        const auto it = bestCost.find(cur.coord);
        if (it != bestCost.end() && cur.cost > it->second + 0.0001f) continue;

        const int32_t neighborCount = grid.getNeighbors(cur.coord, neighbors);
        for (int32_t i = 0; i < neighborCount; ++i) {
            const HexCoord& n = neighbors[i];
            const Tile* tile = grid.tileAt(n);
            if (!tile) continue;
            if (!canEnter(*tile)) continue;

            const float stepCost = tile->baseMoveCost();
            const float newCost = cur.cost + stepCost;
            if (newCost > movementBudget + 0.0001f) continue;

            const auto existing = bestCost.find(n);
            if (existing == bestCost.end() || newCost < existing->second - 0.0001f) {
                bestCost[n] = newCost;
                cameFrom[n] = cur.coord;
                open.push({newCost, n});
            }
        }
    }

    result.reserve(bestCost.size());
    for (const auto& [coord, cost] : bestCost) {
        ReachableTile rt;
        rt.coord = coord;
        rt.costSoFar = cost;
        const auto parentIt = cameFrom.find(coord);
        rt.hasParent = parentIt != cameFrom.end();
        rt.cameFrom = rt.hasParent ? parentIt->second : coord;
        result.push_back(rt);
    }
    return result;
}

std::vector<HexCoord> Pathfinder::findPath(
    const HexGrid& grid, const HexCoord& start, const HexCoord& goal,
    const EnterPredicate& canEnter, int32_t maxSearchNodes) {

    std::vector<HexCoord> path;
    if (start == goal) {
        path.push_back(start);
        return path;
    }

    std::unordered_map<HexCoord, float, HexCoord::Hash> gScore;
    std::unordered_map<HexCoord, HexCoord, HexCoord::Hash> cameFrom;

    MinHeap open;
    open.push({0.0f, start});
    gScore[start] = 0.0f;

    int32_t expanded = 0;
    std::array<HexCoord, 6> neighbors;
    bool found = false;

    while (!open.empty() && expanded < maxSearchNodes) {
        const HeapEntry cur = open.pop();
        ++expanded;

        if (cur.coord == goal) {
            found = true;
            break;
        }

        const auto curG = gScore.find(cur.coord);
        const float curGVal = (curG != gScore.end()) ? curG->second : 0.0f;

        const int32_t neighborCount = grid.getNeighbors(cur.coord, neighbors);
        for (int32_t i = 0; i < neighborCount; ++i) {
            const HexCoord& n = neighbors[i];
            const Tile* tile = grid.tileAt(n);
            if (!tile) continue;
            // Goal tile is always allowed to be *evaluated* even if it
            // would normally be blocked (e.g. occupied by an enemy,
            // which the caller interprets as "attack" rather than
            // "move"); the caller's predicate is authoritative though,
            // so we simply respect it as given.
            if (!canEnter(*tile)) continue;

            const float tentativeG = curGVal + tile->baseMoveCost();
            const auto existing = gScore.find(n);
            if (existing == gScore.end() || tentativeG < existing->second - 0.0001f) {
                gScore[n] = tentativeG;
                cameFrom[n] = cur.coord;
                const float h = static_cast<float>(n.distanceTo(goal));
                open.push({tentativeG + h, n});
            }
        }
    }

    if (!found) return path; // empty: no path within search budget

    HexCoord walker = goal;
    path.push_back(walker);
    while (walker != start) {
        const auto it = cameFrom.find(walker);
        if (it == cameFrom.end()) {
            path.clear();
            return path; // disconnected reconstruction, shouldn't happen
        }
        walker = it->second;
        path.push_back(walker);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace poly

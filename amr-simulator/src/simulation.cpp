#include "simulation.h"

#include <algorithm>
#include <cmath>
#include <climits>

// =========================================================
// DESIGN OVERVIEW
//
// This file implements the missing simulation layer for the
// AMR warehouse simulator. The behavioral model is:
//
//   SERVER  -- talks only to NODES (task discovery) and
//              STATIONS (deployment). Never touches AMRs
//              directly.
//
//   NODE    -- on request from the server, discovers eligible
//              AMRs within node_range (closest first) and
//              assigns the subtask to the first eligible one.
//              A node that is already locally busy (servicing
//              another request, or mediating a collision) does
//              not respond in time, which is what drives the
//              server's node retry/timeout logic.
//
//   AMR     -- autonomous: decides eligibility (battery safety
//              margin), executes its assigned subtask via
//              atomic MOVE/WORK actions using a rolling-horizon
//              path plan, avoids collisions locally via a
//              cell-reservation scheme (equivalent to the
//              aura/ACCEPT-ACK-NAK protocol described in the
//              spec, but resolved deterministically in a single
//              tick since the simulation is not actually
//              multi-threaded), and manages its own battery/
//              charging.
//
// Everything is advanced deterministically once per
// simulate(dt) call: no real threads, sockets or sleeps are
// used. Communication delay is modeled with simulated timers
// (node "busyUntil" / server "phaseTimer") rather than actual
// waiting.
// =========================================================

namespace
{
    // -----------------------------------------------------
    // Tunable constants
    // -----------------------------------------------------

    constexpr float AMR_SPEED = 1.6f; // cells per second

    constexpr int BATTERY_DRAIN_PER_MOVE = 1;
    constexpr int BATTERY_DRAIN_PER_WORK = 2;

    constexpr float CHARGE_ALPHA = 0.1f; // 100 * alpha = 10 %/sec

    constexpr int LOW_BATTERY_IDLE_RETURN_THRESHOLD = 25;

    // Congestion (reroutes/waiting) can make the ACTUAL number of
    // cells travelled meaningfully higher than the ideal Manhattan
    // estimate. The eligibility check therefore budgets a 50% slack
    // on top of the ideal move count, plus a small flat margin, so
    // that an accepted AMR is never left stranded mid-subtask by a
    // busy warehouse. Battery reaching 0 would otherwise permanently
    // strand that subtask instance (WORK/MOVE are non-preemptive and
    // the same AMR must finish what it started), so this is treated
    // as a hard safety budget rather than a minor tuning constant.
    constexpr float BATTERY_MOVE_SLACK_FACTOR = 1.5f;
    constexpr int BATTERY_FLAT_SAFETY_MARGIN = 10;

    constexpr int PATH_HORIZON = 6;

    constexpr int NODE_RETRY_LIMIT = 3;

    constexpr float SERVER_NO_AMR_RETRY_SECONDS = 30.0f;
    constexpr float DEADLOCK_THRESHOLD_SECONDS = 8.0f;

    // findAlternateStep only ever returns a LOSSLESS reroute (equal
    // progress to the blocked move, never a detour to recover from
    // later), so it is safe and free to attempt immediately - no
    // grace period needed before trying it. Only when no lossless
    // reroute exists does the AMR fall back to waiting.

    constexpr float TRANSMIT_PULSE_DURATION = 0.4f;

    // Communication timeout = base margin + warehouse-size
    // dependent signal travel time.
    constexpr float COMM_BASE_MARGIN = 0.3f;
    constexpr float COMM_TIME_PER_CELL = 0.02f;

    constexpr float PI = 3.14159265358979323846f;

    // -----------------------------------------------------
    // Pure helpers (no access to Simulation's private state)
    // -----------------------------------------------------

    // Rolling-horizon straight (Manhattan) path preview from
    // (fx,fy) toward (tx,ty), at most `horizon` cells long.
    // This is only a PLAN - actual movement still happens one
    // cell at a time and is re-evaluated for conflicts every
    // tick, so dynamic obstacles never need to be baked in
    // here.
    std::vector<Position> computeHorizonPath(
        int fx, int fy,
        int tx, int ty,
        int horizon
    )
    {
        std::vector<Position> path;

        int cx = fx;
        int cy = fy;
        int dx = tx - cx;
        int dy = ty - cy;

        while ((cx != tx || cy != ty) &&
               static_cast<int>(path.size()) < horizon)
        {
            if (std::abs(dx) >= std::abs(dy) && dx != 0)
            {
                cx += (dx > 0) ? 1 : -1;
            }
            else if (dy != 0)
            {
                cy += (dy > 0) ? 1 : -1;
            }
            else
            {
                break;
            }

            path.push_back(Position{ cx, cy });

            dx = tx - cx;
            dy = ty - cy;
        }

        return path;
    }

    // PNG front is assumed to point upward (heading 0), with
    // rotation increasing clockwise (matches sf::Sprite's
    // rotation convention used by the renderer).
    float headingFromDelta(int dx, int dy)
    {
        if (dx == 0 && dy == 0)
        {
            return 0.0f;
        }

        float angle =
            std::atan2(
                static_cast<float>(dx),
                -static_cast<float>(dy)
            ) * (180.0f / PI);

        if (angle < 0.0f)
        {
            angle += 360.0f;
        }

        return angle;
    }
}


// =====================================================
// INIT
// =====================================================

void Simulation::init(State& state)
{
    this->state = &state;

    simTime = 0.0f;
    stuck = false;

    amrRuntimes.assign(state.amrs.size(), AMRRuntime{});
    nodeRuntimes.assign(state.nodes.size(), NodeRuntime{});

    nextAmrId = 1;
    for (const auto& amr : state.amrs)
    {
        nextAmrId = std::max(nextAmrId, amr.id + 1);
    }

    taskExecs.clear();
    taskExecs.reserve(state.tasks.size());

    for (const auto& task : state.tasks)
    {
        TaskExecution texec;
        texec.task = &task;
        texec.instances.assign(
            static_cast<std::size_t>(std::max(0, task.count)),
            SubtaskInstance{}
        );
        taskExecs.push_back(std::move(texec));
    }

    server = ServerState{};

    restingOccupancy.clear();
    inFlightClaims.clear();
    activeConflicts.clear();

    state.dashboard.amrs_deployed =
        static_cast<int>(state.amrs.size());

    state.dashboard.current_task =
        state.tasks.empty() ? -1 : state.tasks.front().id;
}


// =====================================================
// TOP-LEVEL TICK
// =====================================================

void Simulation::simulate(float dt)
{
    if (state == nullptr || dt <= 0.0f)
    {
        return;
    }

    simTime += dt;

    rebuildOccupancyMaps();

    updateServer(dt);

    updateNodeTransmitDecay();

    updateAMRs(dt);

    updateDashboard();

    cleanupStaleConflicts();

    recomputeStuck();
}

bool Simulation::isOver() const
{
    if (state == nullptr)
    {
        return false;
    }

    return stuck || allTasksComplete();
}


// =====================================================
// OCCUPANCY / COLLISION BOOKKEEPING
// =====================================================

void Simulation::rebuildOccupancyMaps()
{
    restingOccupancy.clear();
    inFlightClaims.clear();

    for (std::size_t i = 0; i < state->amrs.size(); ++i)
    {
        const AMR& amr = state->amrs[i];
        const AMRRuntime& rt = amrRuntimes[i];

        if (rt.moving)
        {
            inFlightClaims[encodeCell(rt.moveTo.x, rt.moveTo.y)] =
                static_cast<int>(i);
        }
        else
        {
            const int cx = static_cast<int>(std::lround(amr.x));
            const int cy = static_cast<int>(std::lround(amr.y));

            restingOccupancy[encodeCell(cx, cy)] =
                static_cast<int>(i);
        }
    }
}

long long Simulation::encodeCell(int x, int y) const
{
    return static_cast<long long>(y) * 1000000LL +
           static_cast<long long>(x);
}

Simulation::ClaimResult Simulation::tryClaimCell(
    int amrIndex,
    Position current,
    Position desired,
    Position& outCell
)
{
    const long long key = encodeCell(desired.x, desired.y);

    const auto restIt = restingOccupancy.find(key);
    const bool blockedByRest =
        restIt != restingOccupancy.end() &&
        restIt->second != amrIndex;

    const auto flightIt = inFlightClaims.find(key);
    const bool blockedByFlight =
        flightIt != inFlightClaims.end() &&
        flightIt->second != amrIndex;

    if (!blockedByRest && !blockedByFlight)
    {
        inFlightClaims[key] = amrIndex;
        outCell = desired;
        return ClaimResult::Claimed;
    }

    const int otherIndex =
        blockedByRest ? restIt->second : flightIt->second;

    // A resting AMR that is NOT mid-WORK (and not charging) has no
    // urgent, non-preemptible reason to be exactly where it is: an
    // idle AMR isn't going anywhere on its own, and one that is
    // itself at rest deciding its own next move (Executing in a
    // Move* phase, or ReturningToCharge) can just as safely be
    // nudged one cell aside first - it will simply replan its own
    // route from the new spot next tick. This is what prevents a
    // cluster of several active AMRs converging on the same hot
    // cell (e.g. several all headed for the same box) from
    // gridlocking each other forever: whoever needs the cell first
    // (by AMR processing order) can always make room. WORK and
    // charging are the only truly non-preemptible states, so a
    // blocker in either is never evicted - callers simply wait,
    // bounded by that action's duration.
    if (blockedByRest)
    {
        const AMRMode otherMode = amrRuntimes[otherIndex].mode;
        const ExecPhase otherPhase = amrRuntimes[otherIndex].phase;

        const bool otherIsMidWork =
            otherMode == AMRMode::Executing &&
            (otherPhase == ExecPhase::WorkAtA ||
             otherPhase == ExecPhase::WorkAtB);

        const bool evictable =
            !otherIsMidWork && otherMode != AMRMode::Charging;

        if (evictable)
        {
            registerConflict(amrIndex, otherIndex); // stats only

            Position evictTo{};
            if (findFreeAdjacent(otherIndex, current, evictTo))
            {
                relocateRestingAMR(otherIndex, evictTo);

                inFlightClaims[key] = amrIndex;
                outCell = desired;
                return ClaimResult::Claimed;
            }

            // The blocker itself is fully boxed in (very rare) -
            // nothing safe to do this tick but wait.
            return ClaimResult::Blocked;
        }
    }

    // Track/collision-count this conflict, but NEVER let it force a
    // claim into a cell someone else is still physically occupying
    // (whether mid-WORK or itself stuck deciding its own next move).
    // Safety (no two AMRs ever share a cell) outranks liveness here;
    // a true, permanently unresolvable mutual deadlock essentially
    // cannot occur on this open grid since a lossless reroute is
    // available whenever the goal isn't purely along one axis, so
    // the only cost of not forcing through is a very rare, brief
    // stall rather than any risk of an actual physical collision.
    registerConflict(amrIndex, otherIndex);

    Position alt{};

    if (findAlternateStep(
            amrIndex,
            current,
            desired,
            amrRuntimes[amrIndex].legDestX,
            amrRuntimes[amrIndex].legDestY,
            alt))
    {
        const int callerId = state->amrs[amrIndex].id;
        const int otherId = state->amrs[otherIndex].id;

        const auto conflictKey =
            (callerId < otherId)
                ? std::make_pair(callerId, otherId)
                : std::make_pair(otherId, callerId);

        ConflictRecord& rec = activeConflicts[conflictKey];

        if (!rec.rerouteCounted)
        {
            rec.rerouteCounted = true;
            state->dashboard.reroutes++;
        }

        inFlightClaims[encodeCell(alt.x, alt.y)] = amrIndex;
        outCell = alt;
        return ClaimResult::Claimed;
    }

    return ClaimResult::Blocked;
}

bool Simulation::findAlternateStep(
    int amrIndex,
    Position current,
    Position blocked,
    int destX,
    int destY,
    Position& outAlt
) const
{
    const int w = state->warehouse.width;
    const int h = state->warehouse.height;

    struct Candidate
    {
        Position pos;
        int dist;
    };

    std::vector<Candidate> candidates;

    static constexpr int dxs[4] = { 0, 0, -1, 1 };
    static constexpr int dys[4] = { -1, 1, 0, 0 };

    // A reroute must never be a net detour: on an open grid, if the
    // blocked move would have reduced distance-to-goal by 1 (which
    // any sane planned move does), there is usually a PERPENDICULAR
    // neighbor that reduces it by exactly the same amount (e.g. if
    // blocked going right, going up/down makes equal progress when
    // the goal isn't purely horizontal). Only candidates that make
    // genuine, undiminished progress are accepted - anything else
    // just becomes extra battery-draining travel that has to be
    // recovered from later, so the caller should WAIT instead.
    const int currentDist = manhattan(current.x, current.y, destX, destY);

    for (int k = 0; k < 4; ++k)
    {
        Position p{ current.x + dxs[k], current.y + dys[k] };

        if (p.x == blocked.x && p.y == blocked.y)
        {
            continue;
        }

        if (p.x < 0 || p.x >= w || p.y < 0 || p.y >= h)
        {
            continue;
        }

        const int d = manhattan(p.x, p.y, destX, destY);
        if (d >= currentDist)
        {
            // Would not make progress - not a genuine reroute.
            continue;
        }

        const long long key = encodeCell(p.x, p.y);

        const auto restIt = restingOccupancy.find(key);
        if (restIt != restingOccupancy.end() &&
            restIt->second != amrIndex)
        {
            continue;
        }

        const auto flightIt = inFlightClaims.find(key);
        if (flightIt != inFlightClaims.end() &&
            flightIt->second != amrIndex)
        {
            continue;
        }

        candidates.push_back({ p, d });
    }

    if (candidates.empty())
    {
        return false;
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& a, const Candidate& b)
        {
            return a.dist < b.dist;
        }
    );

    outAlt = candidates.front().pos;
    return true;
}

bool Simulation::findFreeAdjacent(
    int amrIndex,
    Position exclude,
    Position& outCell
) const
{
    // Idle AMRs can cluster (e.g. several parked right next to a
    // busy box), so the immediate 4-neighbor ring around the one
    // being evicted may itself be fully occupied. BFS outward until
    // the first genuinely free cell is found - this always succeeds
    // as long as the warehouse isn't saturated with AMRs, and finds
    // the closest such cell (so the shuffle stays local/cheap).
    const int w = state->warehouse.width;
    const int h = state->warehouse.height;

    const AMR& amr = state->amrs[amrIndex];
    const int startX = static_cast<int>(std::lround(amr.x));
    const int startY = static_cast<int>(std::lround(amr.y));

    static constexpr int dxs[4] = { 0, 0, -1, 1 };
    static constexpr int dys[4] = { -1, 1, 0, 0 };

    std::vector<Position> queue;
    std::vector<char> visited(
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0
    );

    const auto mark = [&](int x, int y) -> char&
    {
        return visited[static_cast<std::size_t>(y) * w + x];
    };

    queue.push_back({ startX, startY });
    mark(startX, startY) = 1;

    std::size_t head = 0;
    while (head < queue.size())
    {
        const Position p = queue[head++];

        for (int k = 0; k < 4; ++k)
        {
            const Position n{ p.x + dxs[k], p.y + dys[k] };

            if (n.x < 0 || n.x >= w || n.y < 0 || n.y >= h)
            {
                continue;
            }
            if (mark(n.x, n.y) != 0)
            {
                continue;
            }
            mark(n.x, n.y) = 1;

            if (n.x == exclude.x && n.y == exclude.y)
            {
                continue; // reserved for the AMR we're making way for
            }

            const long long key = encodeCell(n.x, n.y);
            if (restingOccupancy.count(key) != 0 ||
                inFlightClaims.count(key) != 0)
            {
                queue.push_back(n); // occupied, but keep expanding through it
                continue;
            }

            outCell = n;
            return true;
        }
    }

    return false;
}

void Simulation::relocateRestingAMR(int amrIndex, Position to)
{
    AMR& amr = state->amrs[amrIndex];

    const long long oldKey = encodeCell(
        static_cast<int>(std::lround(amr.x)),
        static_cast<int>(std::lround(amr.y))
    );
    restingOccupancy.erase(oldKey);

    amr.x = static_cast<float>(to.x);
    amr.y = static_cast<float>(to.y);

    restingOccupancy[encodeCell(to.x, to.y)] = amrIndex;
}

void Simulation::registerConflict(int amrIndex, int otherIndex)
{
    const int callerId = state->amrs[amrIndex].id;
    const int otherId = state->amrs[otherIndex].id;

    const auto key =
        (callerId < otherId)
            ? std::make_pair(callerId, otherId)
            : std::make_pair(otherId, callerId);

    ConflictRecord& rec = activeConflicts[key];

    if (rec.since < 0.0f)
    {
        rec.since = simTime;
        state->dashboard.collisions++;
    }

    rec.touchedThisTick = true;

    if (!rec.deadlockCounted &&
        (simTime - rec.since) >= DEADLOCK_THRESHOLD_SECONDS)
    {
        rec.deadlockCounted = true;
        state->dashboard.deadlocks++;
    }
}

void Simulation::cleanupStaleConflicts()
{
    for (auto it = activeConflicts.begin();
         it != activeConflicts.end(); )
    {
        if (!it->second.touchedThisTick)
        {
            it = activeConflicts.erase(it);
        }
        else
        {
            it->second.touchedThisTick = false;
            ++it;
        }
    }
}


// =====================================================
// GEOMETRY / TASK-SEMANTICS HELPERS
// =====================================================

int Simulation::manhattan(int x1, int y1, int x2, int y2) const
{
    return std::abs(x1 - x2) + std::abs(y1 - y2);
}

const Box* Simulation::findBox(int id) const
{
    for (const auto& box : state->boxes)
    {
        if (box.id == id)
        {
            return &box;
        }
    }
    return nullptr;
}

int Simulation::nearestStation(int x, int y) const
{
    int best = -1;
    int bestDist = INT_MAX;

    for (std::size_t i = 0; i < state->stations.size(); ++i)
    {
        const int d =
            manhattan(x, y, state->stations[i].x, state->stations[i].y);

        if (d < bestDist)
        {
            bestDist = d;
            best = static_cast<int>(i);
        }
    }

    return best;
}

Position Simulation::firstLegTarget(const Task& task) const
{
    if (task.subtask == Subtask::Goto)
    {
        return Position{ task.a, task.b };
    }

    const Box* box = findBox(task.a);
    if (box != nullptr)
    {
        return Position{ box->x, box->y };
    }

    // Defensive fallback for a malformed reference; keeps the
    // simulation from crashing on bad JSON data.
    return Position{ 0, 0 };
}

Position Simulation::secondLegTarget(const Task& task) const
{
    const Box* box = findBox(task.b);
    if (box != nullptr)
    {
        return Position{ box->x, box->y };
    }

    return Position{ 0, 0 };
}

int Simulation::workDurationFor(int boxId) const
{
    const Box* box = findBox(boxId);
    return box != nullptr ? box->duration : 0;
}

bool Simulation::eligible(int amrIndex, const Task& task) const
{
    const AMR& amr = state->amrs[amrIndex];
    const AMRRuntime& rt = amrRuntimes[amrIndex];

    if (rt.mode != AMRMode::Idle)
    {
        return false;
    }

    if (amr.battery <= 0)
    {
        return false;
    }

    const Position pos{
        static_cast<int>(std::lround(amr.x)),
        static_cast<int>(std::lround(amr.y))
    };

    int moves = 0;
    int works = 0;
    Position finalPos = pos;

    if (task.subtask == Subtask::Goto)
    {
        const Position target{ task.a, task.b };
        moves += manhattan(pos.x, pos.y, target.x, target.y);
        finalPos = target;
    }
    else
    {
        const Box* boxA = findBox(task.a);
        if (boxA == nullptr)
        {
            return false;
        }

        moves += manhattan(pos.x, pos.y, boxA->x, boxA->y);
        works += 1;
        finalPos = Position{ boxA->x, boxA->y };

        if (task.subtask == Subtask::TakeAndPut)
        {
            const Box* boxB = findBox(task.b);
            if (boxB == nullptr)
            {
                return false;
            }

            moves += manhattan(boxA->x, boxA->y, boxB->x, boxB->y);
            works += 1;
            finalPos = Position{ boxB->x, boxB->y };
        }
    }

    int returnMoves = 0;
    const int stIdx = nearestStation(finalPos.x, finalPos.y);

    if (stIdx >= 0)
    {
        const Station& st = state->stations[stIdx];
        returnMoves = manhattan(finalPos.x, finalPos.y, st.x, st.y);
    }

    const int moveBudget =
        static_cast<int>(
            std::ceil(
                static_cast<float>(moves + returnMoves) *
                BATTERY_MOVE_SLACK_FACTOR
            )
        );

    const int totalCost =
        moveBudget * BATTERY_DRAIN_PER_MOVE +
        works * BATTERY_DRAIN_PER_WORK +
        BATTERY_FLAT_SAFETY_MARGIN;

    return amr.battery >= totalCost;
}


// =====================================================
// SERVER
// =====================================================

bool Simulation::advanceToNextPendingInstance()
{
    while (server.taskIndex < static_cast<int>(taskExecs.size()))
    {
        TaskExecution& texec = taskExecs[server.taskIndex];

        while (server.instanceIndex <
               static_cast<int>(texec.instances.size()))
        {
            if (!texec.instances[server.instanceIndex].accepted)
            {
                state->dashboard.current_task = texec.task->id;
                return true;
            }

            server.instanceIndex++;
        }

        server.taskIndex++;
        server.instanceIndex = 0;
    }

    state->dashboard.current_task = -1;
    return false;
}

float Simulation::commTimeout() const
{
    const float perimeter =
        static_cast<float>(
            state->warehouse.width + state->warehouse.height
        );

    return COMM_BASE_MARGIN + perimeter * COMM_TIME_PER_CELL;
}

void Simulation::beginNodeSearch()
{
    const Task& task = *taskExecs[server.taskIndex].task;
    const Position ref = firstLegTarget(task);

    server.nodeOrder.clear();
    for (std::size_t i = 0; i < state->nodes.size(); ++i)
    {
        server.nodeOrder.push_back(static_cast<int>(i));
    }

    std::sort(
        server.nodeOrder.begin(),
        server.nodeOrder.end(),
        [&](int a, int b)
        {
            const int da =
                manhattan(state->nodes[a].x, state->nodes[a].y,
                          ref.x, ref.y);
            const int db =
                manhattan(state->nodes[b].x, state->nodes[b].y,
                          ref.x, ref.y);

            if (da != db)
            {
                return da < db;
            }

            return state->nodes[a].id < state->nodes[b].id;
        }
    );

    server.nodeOrderPos = 0;
    server.nodeAttempt = 0;
    server.phase = ServerPhase::Requesting;
}

void Simulation::beginDeployAttempt()
{
    const Task& task = *taskExecs[server.taskIndex].task;
    const Position ref = firstLegTarget(task);

    server.stationOrder.clear();
    for (std::size_t i = 0; i < state->stations.size(); ++i)
    {
        server.stationOrder.push_back(static_cast<int>(i));
    }

    std::sort(
        server.stationOrder.begin(),
        server.stationOrder.end(),
        [&](int a, int b)
        {
            const int da =
                manhattan(state->stations[a].x, state->stations[a].y,
                          ref.x, ref.y);
            const int db =
                manhattan(state->stations[b].x, state->stations[b].y,
                          ref.x, ref.y);

            if (da != db)
            {
                return da < db;
            }

            return state->stations[a].id < state->stations[b].id;
        }
    );

    server.stationOrderPos = 0;
    server.phase = ServerPhase::TryDeploy;
}

bool Simulation::tryAssignViaNode(
    int nodeIndex,
    int taskIndex,
    int instanceIndex
)
{
    const Node& node = state->nodes[nodeIndex];
    const Task& task = *taskExecs[taskIndex].task;

    // BFS-equivalent discovery: on a uniform-cost grid, BFS
    // layers from the node correspond exactly to Manhattan
    // distance, so gathering AMRs within node_range ordered by
    // increasing distance (closest first, ties by AMR id)
    // reproduces the intended "closer AMRs discovered first"
    // behavior without needing an explicit queue/visited walk.
    struct Candidate
    {
        int amrIndex;
        int dist;
        int id;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(state->amrs.size());

    for (std::size_t i = 0; i < state->amrs.size(); ++i)
    {
        const AMR& amr = state->amrs[i];

        const int ax = static_cast<int>(std::lround(amr.x));
        const int ay = static_cast<int>(std::lround(amr.y));

        const int d = manhattan(node.x, node.y, ax, ay);

        if (d <= state->node_range)
        {
            candidates.push_back(
                { static_cast<int>(i), d, amr.id }
            );
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& a, const Candidate& b)
        {
            if (a.dist != b.dist)
            {
                return a.dist < b.dist;
            }
            return a.id < b.id;
        }
    );

    for (const auto& candidate : candidates)
    {
        if (!eligible(candidate.amrIndex, task))
        {
            continue;
        }

        AMR& amr = state->amrs[candidate.amrIndex];
        AMRRuntime& rt = amrRuntimes[candidate.amrIndex];

        rt.mode = AMRMode::Executing;
        rt.taskIndex = taskIndex;
        rt.instanceIndex = instanceIndex;
        rt.phase = ExecPhase::MoveToA;
        rt.workTimer = 0.0f;
        rt.moving = false;
        rt.moveProgress = 0.0f;

        amr.idle = false;
        amr.transmitting = true;
        rt.transmitPulseTimer = TRANSMIT_PULSE_DURATION;

        taskExecs[taskIndex]
            .instances[instanceIndex]
            .assignedAMRIndex = candidate.amrIndex;

        return true;
    }

    return false;
}

void Simulation::deployAMR(int stationIndex)
{
    Station& st = state->stations[stationIndex];

    if (st.count <= 0)
    {
        return;
    }

    st.count--;

    AMR newAmr;
    newAmr.id = nextAmrId++;
    newAmr.x = static_cast<float>(st.x);
    newAmr.y = static_cast<float>(st.y);
    newAmr.idle = true;
    newAmr.heading = 0.0f;
    newAmr.intended_path.clear();
    newAmr.transmitting = true; // brief deployment ack pulse
    newAmr.battery = 100;

    state->amrs.push_back(newAmr);

    AMRRuntime rt;
    rt.transmitPulseTimer = TRANSMIT_PULSE_DURATION;
    amrRuntimes.push_back(rt);
}

void Simulation::updateServer(float dt)
{
    if (state->tasks.empty())
    {
        server.phase = ServerPhase::Idle;
        state->dashboard.current_task = -1;
        return;
    }

    switch (server.phase)
    {
        case ServerPhase::Idle:
        {
            if (advanceToNextPendingInstance())
            {
                beginNodeSearch();
            }
            break;
        }

        case ServerPhase::Requesting:
        {
            if (server.nodeOrderPos >= server.nodeOrder.size())
            {
                beginDeployAttempt();
                break;
            }

            const int nodeIdx = server.nodeOrder[server.nodeOrderPos];

            if (nodeRuntimes[nodeIdx].busyUntil > simTime)
            {
                // Node is currently occupied with other local
                // traffic (e.g. mediating a collision) - the
                // server's request effectively goes unanswered.
                server.nodeAttempt++;
                server.waitKind = NodeWaitKind::Retry;
                server.phase = ServerPhase::WaitingNodeResponse;
                server.phaseTimer = commTimeout();
            }
            else
            {
                state->nodes[nodeIdx].transmitting = true;
                nodeRuntimes[nodeIdx].busyUntil =
                    simTime + commTimeout();

                server.waitKind = NodeWaitKind::Resolve;
                server.phase = ServerPhase::WaitingNodeResponse;
                server.phaseTimer = commTimeout();
            }
            break;
        }

        case ServerPhase::WaitingNodeResponse:
        {
            server.phaseTimer -= dt;

            if (server.phaseTimer > 0.0f)
            {
                break;
            }

            if (server.waitKind == NodeWaitKind::Retry)
            {
                if (server.nodeAttempt >= NODE_RETRY_LIMIT)
                {
                    server.nodeOrderPos++;
                    server.nodeAttempt = 0;
                }
                server.phase = ServerPhase::Requesting;
            }
            else
            {
                const int nodeIdx =
                    server.nodeOrder[server.nodeOrderPos];

                const bool found = tryAssignViaNode(
                    nodeIdx,
                    server.taskIndex,
                    server.instanceIndex
                );

                if (found)
                {
                    taskExecs[server.taskIndex]
                        .instances[server.instanceIndex]
                        .accepted = true;

                    server.phase = ServerPhase::Idle;
                }
                else
                {
                    server.nodeOrderPos++;
                    server.nodeAttempt = 0;
                    server.phase = ServerPhase::Requesting;
                }
            }
            break;
        }

        case ServerPhase::TryDeploy:
        {
            if (server.stationOrderPos >= server.stationOrder.size())
            {
                server.phase = ServerPhase::WaitingRetry;
                server.phaseTimer = SERVER_NO_AMR_RETRY_SECONDS;
                break;
            }

            const int stIdx =
                server.stationOrder[server.stationOrderPos];

            if (state->stations[stIdx].count > 0)
            {
                deployAMR(stIdx);
                beginNodeSearch();
            }
            else
            {
                server.stationOrderPos++;
            }
            break;
        }

        case ServerPhase::WaitingRetry:
        {
            server.phaseTimer -= dt;

            if (server.phaseTimer <= 0.0f)
            {
                server.phase = ServerPhase::Idle;
            }
            break;
        }
    }
}


// =====================================================
// NODES
// =====================================================

void Simulation::updateNodeTransmitDecay()
{
    for (std::size_t i = 0; i < state->nodes.size(); ++i)
    {
        state->nodes[i].transmitting =
            nodeRuntimes[i].busyUntil > simTime;
    }
}


// =====================================================
// AMRS
// =====================================================

void Simulation::updateAMRs(float dt)
{
    const std::size_t count = state->amrs.size();

    for (std::size_t i = 0; i < count; ++i)
    {
        updateSingleAMR(static_cast<int>(i), dt);
    }
}

void Simulation::updateSingleAMR(int amrIndex, float dt)
{
    AMR& amr = state->amrs[amrIndex];
    AMRRuntime& rt = amrRuntimes[amrIndex];

    if (amr.transmitting)
    {
        rt.transmitPulseTimer -= dt;
        if (rt.transmitPulseTimer <= 0.0f)
        {
            amr.transmitting = false;
        }
    }

    if (amr.battery <= 0 && rt.mode != AMRMode::Dead)
    {
        rt.mode = AMRMode::Dead;
        amr.idle = false;
        amr.intended_path.clear();
    }

    applyChargingIfAtStation(amrIndex, dt);

    switch (rt.mode)
    {
        case AMRMode::Idle:
        {
            const int cx = static_cast<int>(std::lround(amr.x));
            const int cy = static_cast<int>(std::lround(amr.y));

            bool atStation = false;
            for (const auto& st : state->stations)
            {
                if (st.x == cx && st.y == cy)
                {
                    atStation = true;
                    break;
                }
            }

            if (!atStation &&
                amr.battery <= LOW_BATTERY_IDLE_RETURN_THRESHOLD)
            {
                beginReturnToCharge(amrIndex);
            }
            break;
        }

        case AMRMode::Executing:
            advanceExecution(amrIndex, dt);
            break;

        case AMRMode::ReturningToCharge:
            advanceReturnMovement(amrIndex, dt);
            break;

        case AMRMode::Charging:
            if (amr.battery >= 100)
            {
                rt.mode = AMRMode::Idle;
            }
            break;

        case AMRMode::Dead:
            break;
    }

    amr.idle = (rt.mode == AMRMode::Idle);
}

void Simulation::applyChargingIfAtStation(int amrIndex, float dt)
{
    AMR& amr = state->amrs[amrIndex];
    AMRRuntime& rt = amrRuntimes[amrIndex];

    if (rt.moving || amr.battery >= 100)
    {
        return;
    }

    const int cx = static_cast<int>(std::lround(amr.x));
    const int cy = static_cast<int>(std::lround(amr.y));

    bool atStation = false;
    for (const auto& st : state->stations)
    {
        if (st.x == cx && st.y == cy)
        {
            atStation = true;
            break;
        }
    }

    if (!atStation)
    {
        return;
    }

    rt.chargeAccumulator += 100.0f * CHARGE_ALPHA * dt;

    const int gain = static_cast<int>(rt.chargeAccumulator);
    if (gain > 0)
    {
        amr.battery = std::min(100, amr.battery + gain);
        rt.chargeAccumulator -= static_cast<float>(gain);
    }

    if (amr.battery >= 100)
    {
        amr.battery = 100;
        rt.chargeAccumulator = 0.0f;
    }
}

bool Simulation::stepTowards(int amrIndex, float dt, int tx, int ty)
{
    AMR& amr = state->amrs[amrIndex];
    AMRRuntime& rt = amrRuntimes[amrIndex];

    if (!rt.moving)
    {
        const int curX = static_cast<int>(std::lround(amr.x));
        const int curY = static_cast<int>(std::lround(amr.y));

        if (curX == tx && curY == ty)
        {
            amr.intended_path.clear();
            return true;
        }

        const std::vector<Position> preview =
            computeHorizonPath(curX, curY, tx, ty, PATH_HORIZON);

        amr.intended_path = preview;

        if (preview.empty())
        {
            return true;
        }

        Position claimed{};
        const ClaimResult result = tryClaimCell(
            amrIndex,
            Position{ curX, curY },
            preview.front(),
            claimed
        );

        if (result == ClaimResult::Blocked)
        {
            return false;
        }

        rt.moving = true;
        rt.moveFrom = Position{ curX, curY };
        rt.moveTo = claimed;
        rt.moveProgress = 0.0f;
        rt.legDestX = tx;
        rt.legDestY = ty;
    }

    rt.moveProgress += AMR_SPEED * dt;

    if (rt.moveProgress >= 1.0f)
    {
        amr.x = static_cast<float>(rt.moveTo.x);
        amr.y = static_cast<float>(rt.moveTo.y);

        rt.moving = false;
        rt.moveProgress = 0.0f;

        amr.heading = headingFromDelta(
            rt.moveTo.x - rt.moveFrom.x,
            rt.moveTo.y - rt.moveFrom.y
        );

        amr.battery =
            std::max(0, amr.battery - BATTERY_DRAIN_PER_MOVE);

        if (rt.moveTo.x == tx && rt.moveTo.y == ty)
        {
            amr.intended_path.clear();
            return true;
        }

        return false;
    }

    const float t = rt.moveProgress;
    amr.x = static_cast<float>(rt.moveFrom.x) +
        static_cast<float>(rt.moveTo.x - rt.moveFrom.x) * t;
    amr.y = static_cast<float>(rt.moveFrom.y) +
        static_cast<float>(rt.moveTo.y - rt.moveFrom.y) * t;

    return false;
}

void Simulation::advanceExecution(int amrIndex, float dt)
{
    AMR& amr = state->amrs[amrIndex];
    AMRRuntime& rt = amrRuntimes[amrIndex];

    TaskExecution& texec = taskExecs[rt.taskIndex];
    const Task& task = *texec.task;
    SubtaskInstance& inst = texec.instances[rt.instanceIndex];

    switch (rt.phase)
    {
        case ExecPhase::MoveToA:
        {
            const Position target = firstLegTarget(task);
            rt.legDestX = target.x;
            rt.legDestY = target.y;

            if (stepTowards(amrIndex, dt, target.x, target.y))
            {
                if (task.subtask == Subtask::Goto)
                {
                    rt.phase = ExecPhase::Done;
                }
                else
                {
                    rt.phase = ExecPhase::WorkAtA;
                    rt.workTimer =
                        static_cast<float>(workDurationFor(task.a));
                }
            }
            break;
        }

        case ExecPhase::WorkAtA:
        {
            amr.intended_path.clear();
            rt.workTimer -= dt;

            if (rt.workTimer <= 0.0f)
            {
                amr.battery = std::max(
                    0, amr.battery - BATTERY_DRAIN_PER_WORK
                );

                rt.phase =
                    (task.subtask == Subtask::Operate)
                        ? ExecPhase::Done
                        : ExecPhase::MoveToB;
            }
            break;
        }

        case ExecPhase::MoveToB:
        {
            const Position target = secondLegTarget(task);
            rt.legDestX = target.x;
            rt.legDestY = target.y;

            if (stepTowards(amrIndex, dt, target.x, target.y))
            {
                rt.phase = ExecPhase::WorkAtB;
                rt.workTimer =
                    static_cast<float>(workDurationFor(task.b));
            }
            break;
        }

        case ExecPhase::WorkAtB:
        {
            amr.intended_path.clear();
            rt.workTimer -= dt;

            if (rt.workTimer <= 0.0f)
            {
                amr.battery = std::max(
                    0, amr.battery - BATTERY_DRAIN_PER_WORK
                );
                rt.phase = ExecPhase::Done;
            }
            break;
        }

        case ExecPhase::Done:
        {
            // COMPLETED: relayed AMR -> node -> server.
            inst.completed = true;

            amr.transmitting = true;
            rt.transmitPulseTimer = TRANSMIT_PULSE_DURATION;

            rt.taskIndex = -1;
            rt.instanceIndex = -1;
            rt.phase = ExecPhase::None;

            amr.idle = true;

            if (amr.battery <= LOW_BATTERY_IDLE_RETURN_THRESHOLD)
            {
                beginReturnToCharge(amrIndex);
            }
            else
            {
                rt.mode = AMRMode::Idle;
            }
            break;
        }

        case ExecPhase::None:
        default:
            break;
    }
}

void Simulation::beginReturnToCharge(int amrIndex)
{
    AMR& amr = state->amrs[amrIndex];
    AMRRuntime& rt = amrRuntimes[amrIndex];

    const int cx = static_cast<int>(std::lround(amr.x));
    const int cy = static_cast<int>(std::lround(amr.y));

    const int stIdx = nearestStation(cx, cy);

    if (stIdx < 0)
    {
        // No stations exist at all - nowhere to charge.
        rt.mode = AMRMode::Idle;
        return;
    }

    rt.targetStationId = state->stations[stIdx].id;
    rt.mode = AMRMode::ReturningToCharge;
    amr.idle = false;
}

void Simulation::advanceReturnMovement(int amrIndex, float dt)
{
    AMR& amr = state->amrs[amrIndex];
    AMRRuntime& rt = amrRuntimes[amrIndex];

    int stationIndex = -1;
    for (std::size_t i = 0; i < state->stations.size(); ++i)
    {
        if (state->stations[i].id == rt.targetStationId)
        {
            stationIndex = static_cast<int>(i);
            break;
        }
    }

    if (stationIndex < 0)
    {
        rt.mode = AMRMode::Idle;
        return;
    }

    const Station& st = state->stations[stationIndex];

    if (stepTowards(amrIndex, dt, st.x, st.y))
    {
        rt.mode = AMRMode::Charging;
        amr.intended_path.clear();
    }
}


// =====================================================
// DASHBOARD / COMPLETION
// =====================================================

void Simulation::updateDashboard()
{
    Dashboard& d = state->dashboard;

    d.amrs_deployed = static_cast<int>(state->amrs.size());

    int idle = 0;
    int active = 0;
    int transmitting = 0;
    long long batterySum = 0;

    for (std::size_t i = 0; i < state->amrs.size(); ++i)
    {
        const AMR& amr = state->amrs[i];
        const AMRRuntime& rt = amrRuntimes[i];

        if (rt.mode == AMRMode::Idle)
        {
            idle++;
        }
        else if (rt.mode != AMRMode::Dead)
        {
            active++;
        }

        if (amr.transmitting)
        {
            transmitting++;
        }

        batterySum += amr.battery;
    }

    d.amrs_idle = idle;
    d.amrs_active = active;
    d.amrs_transmitting = transmitting;

    d.average_battery =
        state->amrs.empty()
            ? 100
            : static_cast<int>(
                  batterySum / static_cast<long long>(state->amrs.size())
              );

    int completed = 0;
    for (const auto& texec : taskExecs)
    {
        for (const auto& inst : texec.instances)
        {
            if (inst.completed)
            {
                completed++;
            }
        }
    }
    d.tasks_completed = completed;

    d.elapsed_seconds = static_cast<int>(simTime);
}

bool Simulation::allTasksComplete() const
{
    if (state->tasks.empty())
    {
        return true;
    }

    for (const auto& texec : taskExecs)
    {
        for (const auto& inst : texec.instances)
        {
            if (!inst.completed)
            {
                return false;
            }
        }
    }

    return true;
}

bool Simulation::anyPossibleAMR() const
{
    for (std::size_t i = 0; i < state->amrs.size(); ++i)
    {
        if (state->amrs[i].battery > 0 &&
            amrRuntimes[i].mode != AMRMode::Dead)
        {
            return true;
        }
    }

    for (const auto& st : state->stations)
    {
        if (st.count > 0)
        {
            return true;
        }
    }

    return false;
}

void Simulation::recomputeStuck()
{
    stuck = !allTasksComplete() && !anyPossibleAMR();
}

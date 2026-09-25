#pragma once

#include <vector>
#include <unordered_map>
#include <map>
#include <utility>
#include <cstddef>

#include "structs.h"

// =========================================================
// INTERNAL SIMULATION TYPES
//
// These types model the runtime/logical state that the
// public State/AMR/Node/Station structs do NOT carry
// (structs.h is owned by the parser/editor/renderer contract
// and is not modified here). Everything the behavioral model
// needs beyond that contract lives in these internal types,
// which are private to Simulation.
// =========================================================

enum class ExecPhase
{
    None,
    MoveToA,
    WorkAtA,
    MoveToB,
    WorkAtB,
    Done
};

enum class AMRMode
{
    Idle,
    Executing,
    ReturningToCharge,
    Charging,
    Dead
};

// Per-AMR runtime bookkeeping, parallel to state->amrs
// (index-aligned; grown whenever a new AMR is deployed).
struct AMRRuntime
{
    AMRMode mode = AMRMode::Idle;

    // Current assignment (valid while mode == Executing)
    int taskIndex = -1;
    int instanceIndex = -1;
    ExecPhase phase = ExecPhase::None;
    float workTimer = 0.0f;

    // Movement (rolling-horizon, one atomic cell transition
    // in flight at a time)
    bool moving = false;
    Position moveFrom{ 0, 0 };
    Position moveTo{ 0, 0 };
    float moveProgress = 0.0f;

    // Destination of the CURRENT leg (used to steer the
    // rolling-horizon planner and evaluate reroutes)
    int legDestX = 0;
    int legDestY = 0;

    // Return-to-charge bookkeeping
    int targetStationId = -1;

    // Visual "transmitting" pulse decay
    float transmitPulseTimer = 0.0f;

    // Fractional battery accumulator so charging (scaled by
    // dt) doesn't get lost to integer rounding every frame.
    float chargeAccumulator = 0.0f;
};

// Per-node runtime bookkeeping (index-aligned with
// state->nodes; nodes are static, never created at runtime).
struct NodeRuntime
{
    // Node is considered "busy" (cannot accept a new server
    // request, and any AMR discovery it is mid-way through
    // has not resolved yet) until simulation time reaches
    // this value.
    float busyUntil = 0.0f;
};

// One repetition of a Task's subtask.
struct SubtaskInstance
{
    bool accepted = false;
    bool completed = false;
    int assignedAMRIndex = -1;
};

// Execution bookkeeping for one Task from the warehouse
// JSON (task.count repeated identical subtasks).
struct TaskExecution
{
    const Task* task = nullptr;
    std::vector<SubtaskInstance> instances;
};

enum class ServerPhase
{
    Idle,
    Requesting,
    WaitingNodeResponse,
    TryDeploy,
    WaitingRetry
};

enum class NodeWaitKind
{
    Retry,
    Resolve
};

// The logical central coordinator's state machine. The
// server never touches AMRs directly - it only ever talks
// to nodes (task discovery) and stations (deployment).
struct ServerState
{
    ServerPhase phase = ServerPhase::Idle;

    int taskIndex = 0;
    int instanceIndex = 0;

    std::vector<int> nodeOrder;
    std::size_t nodeOrderPos = 0;
    int nodeAttempt = 0;
    NodeWaitKind waitKind = NodeWaitKind::Resolve;

    std::vector<int> stationOrder;
    std::size_t stationOrderPos = 0;

    float phaseTimer = 0.0f;
};

// Bookkeeping for one active (A,B) conflict episode, used to
// deduplicate collision/reroute/deadlock statistics so a
// single ongoing conflict is only ever counted once.
struct ConflictRecord
{
    float since = -1.0f;
    bool deadlockCounted = false;
    bool rerouteCounted = false;
    bool touchedThisTick = false;
};

class Simulation
{
public:
    void init(State& state);

    void simulate(float dt);

    bool isOver() const;

private:
    // ---------------------------------------------------
    // Top-level tick stages
    // ---------------------------------------------------

    void rebuildOccupancyMaps();
    void updateNodeTransmitDecay();
    void updateServer(float dt);
    void updateAMRs(float dt);
    void updateDashboard();
    void cleanupStaleConflicts();
    void recomputeStuck();

    // ---------------------------------------------------
    // Server helpers
    // ---------------------------------------------------

    bool advanceToNextPendingInstance();
    void beginNodeSearch();
    void beginDeployAttempt();
    float commTimeout() const;
    bool tryAssignViaNode(int nodeIndex, int taskIndex, int instanceIndex);
    void deployAMR(int stationIndex);

    // ---------------------------------------------------
    // AMR behavior helpers
    // ---------------------------------------------------

    void updateSingleAMR(int amrIndex, float dt);
    void applyChargingIfAtStation(int amrIndex, float dt);
    void advanceExecution(int amrIndex, float dt);
    void advanceReturnMovement(int amrIndex, float dt);
    void beginReturnToCharge(int amrIndex);

    // Returns true once the AMR has arrived exactly at
    // (tx, ty). Handles rolling-horizon planning, atomic
    // per-cell movement, and collision avoidance.
    bool stepTowards(int amrIndex, float dt, int tx, int ty);

    // ---------------------------------------------------
    // Eligibility / task semantics
    // ---------------------------------------------------

    bool eligible(int amrIndex, const Task& task) const;
    Position firstLegTarget(const Task& task) const;
    Position secondLegTarget(const Task& task) const;
    int workDurationFor(int boxId) const;
    const Box* findBox(int id) const;
    int nearestStation(int x, int y) const;
    int manhattan(int x1, int y1, int x2, int y2) const;

    // ---------------------------------------------------
    // Collision avoidance
    // ---------------------------------------------------

    long long encodeCell(int x, int y) const;

    enum class ClaimResult { Claimed, Blocked };

    ClaimResult tryClaimCell(
        int amrIndex,
        Position current,
        Position desired,
        Position& outCell
    );

    bool findAlternateStep(
        int amrIndex,
        Position current,
        Position blocked,
        int destX,
        int destY,
        Position& outAlt
    ) const;

    // An idle AMR never moves on its own, so if it happens to be
    // resting exactly on a cell another AMR genuinely needs (e.g. a
    // box another AMR must reach), it can block that cell forever.
    // These two helpers find it a free neighboring cell and shuffle
    // it there so the working AMR can proceed - safely, since the
    // idle AMR is relocated before the cell is handed over, never
    // causing an overlap.
    bool findFreeAdjacent(
        int amrIndex,
        Position exclude,
        Position& outCell
    ) const;

    void relocateRestingAMR(int amrIndex, Position to);

    // Registers/updates a conflict episode between two AMRs for
    // dashboard statistics (collisions avoided / deadlocks). Never
    // authorizes bypassing occupancy safety - see tryClaimCell.
    void registerConflict(int amrIndex, int otherIndex);

    bool allTasksComplete() const;
    bool anyPossibleAMR() const;

private:
    State* state = nullptr;

    float simTime = 0.0f;
    bool stuck = false;

    int nextAmrId = 1;

    std::vector<AMRRuntime> amrRuntimes;
    std::vector<NodeRuntime> nodeRuntimes;
    std::vector<TaskExecution> taskExecs;

    ServerState server;

    std::unordered_map<long long, int> restingOccupancy;
    std::unordered_map<long long, int> inFlightClaims;

    std::map<std::pair<int, int>, ConflictRecord> activeConflicts;
};

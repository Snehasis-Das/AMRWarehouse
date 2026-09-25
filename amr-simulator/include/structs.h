#pragma once

#include <vector>
#include <string>

enum class Subtask {
    TakeAndPut,
    Operate,
    Goto
};

enum class Type {
    Empty,
    Box,
    Station,
    AMR
};

struct Position {
    int x;
    int y;
};

struct Task {
    int id;
    int count;
    Subtask subtask;
    int a;
    int b;
};

struct Warehouse {
    int width;
    int height;
};

struct Box {
    int id;
    int duration;
    int x;
    int y;
};

struct Station {
    int id;
    int count;
    int x;
    int y;
};

struct Node {
    int id;
    int x;
    int y;
    bool transmitting;
};

struct AMR {
    int id;

    // Continuous simulation position
    float x;
    float y;

    bool idle;

    // Current facing direction in degrees
    float heading;

    // Logical path the AMR intends to follow
    std::vector<Position> intended_path;

    // True while this AMR is transmitting a message
    bool transmitting;

    // Battery percentage
    int battery;
};

struct Object {
    int id;
    Type type;
};

struct Dashboard
{
    // Tasks
    int current_task;
    int tasks_completed;
    int tasks_total;

    // Fleet
    int amrs_deployed;
    int amrs_active;
    int amrs_idle;
    int amrs_transmitting;
    int average_battery;

    // Coordination
    int collisions;
    int deadlocks;
    int reroutes;

    // Simulation
    int elapsed_seconds;
};

struct State {
    int node_range;

    Warehouse warehouse;

    std::vector<Station> stations;
    std::vector<Node> nodes;
    std::vector<Box> boxes;
    std::vector<AMR> amrs;
    std::vector<Task> tasks;

    std::vector<std::vector<Object>> grid;

    Dashboard dashboard;
};
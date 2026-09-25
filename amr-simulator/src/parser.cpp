#include "parser.h"

#include <fstream>

bool parse(const std::string& filepath, State& state)
{
    std::ifstream file(filepath);

    if (!file.is_open())
    {
        std::cerr << "Failed to open JSON file: "
                  << filepath << '\n';

        return false;
    }

    try
    {
        json root;
        file >> root;

        // -------------------------------------------------
        // Clear previous state
        // -------------------------------------------------

        state.stations.clear();
        state.nodes.clear();
        state.boxes.clear();
        state.amrs.clear();
        state.tasks.clear();
        state.grid.clear();

        // -------------------------------------------------
        // Dashboard
        // -------------------------------------------------

        state.dashboard.current_task = -1;
        state.dashboard.tasks_completed = 0;
        state.dashboard.tasks_total = 0;

        state.dashboard.amrs_deployed = 0;
        state.dashboard.amrs_active = 0;
        state.dashboard.amrs_idle = 0;
        state.dashboard.amrs_transmitting = 0;
        state.dashboard.average_battery = 100;

        state.dashboard.collisions = 0;
        state.dashboard.deadlocks = 0;
        state.dashboard.reroutes = 0;

        state.dashboard.elapsed_seconds = 0;

        // -------------------------------------------------
        // Warehouse
        // -------------------------------------------------

        state.warehouse.width =
            root.at("width").get<int>();

        state.warehouse.height =
            root.at("height").get<int>();

        // -------------------------------------------------
        // Node range
        // -------------------------------------------------

        state.node_range =
            root.at("minimum_node_range").get<int>();

        // -------------------------------------------------
        // Initialize grid
        //
        // grid[y][x]
        // -------------------------------------------------

        state.grid.resize(
            state.warehouse.height,
            std::vector<Object>(
                state.warehouse.width,
                Object{ -1, Type::Empty }
            )
        );

        // -------------------------------------------------
        // Objects
        // -------------------------------------------------

        for (const auto& object : root.at("objects"))
        {
            const std::string type =
                object.at("type").get<std::string>();

            // ---------------------------------------------
            // Box
            // ---------------------------------------------

            if (type == "Box")
            {
                Box box;

                box.id =
                    object.at("id").get<int>();

                box.duration =
                    object.at("task_timing").get<int>();

                box.x =
                    object.at("x").get<int>();

                box.y =
                    object.at("y").get<int>();

                state.boxes.push_back(box);

                // Occupy grid cell
                state.grid[box.y][box.x] =
                    Object{ box.id, Type::Box };
            }

            // ---------------------------------------------
            // Station
            // ---------------------------------------------

            else if (type == "Station")
            {
                Station station;

                station.id =
                    object.at("id").get<int>();

                station.count =
                    object.at("amr_count").get<int>();

                station.x =
                    object.at("x").get<int>();

                station.y =
                    object.at("y").get<int>();

                state.stations.push_back(station);

                // Occupy grid cell
                state.grid[station.y][station.x] =
                    Object{ station.id, Type::Station };
            }

            // ---------------------------------------------
            // AMR
            // ---------------------------------------------

            else if (type == "AMR")
            {
                const int x =
                    object.at("x").get<int>();

                const int y =
                    object.at("y").get<int>();

                AMR amr;

                amr.id =
                    object.at("id").get<int>();

                // Initial simulation position
                amr.x = static_cast<float>(x);
                amr.y = static_cast<float>(y);

                // AMRs start idle
                amr.idle = true;

                // Initial facing direction
                amr.heading = 0.0f;

                // No intended path yet
                amr.intended_path.clear();

                // No communication initially
                amr.transmitting = false;

                amr.battery = 100;

                state.amrs.push_back(amr);

                // Occupy initial grid cell
                state.grid[y][x] =
                    Object{ amr.id, Type::AMR };
            }

            // ---------------------------------------------
            // Node
            //
            // Nodes do NOT occupy grid cells.
            // ---------------------------------------------

            else if (type == "Node")
            {
                Node node;

                node.id =
                    object.at("id").get<int>();

                node.x =
                    object.at("x").get<int>();

                node.y =
                    object.at("y").get<int>();

                // Nodes are not transmitting initially
                node.transmitting = false;

                state.nodes.push_back(node);
            }

            // ---------------------------------------------
            // Unknown object
            // ---------------------------------------------

            else
            {
                std::cerr << "Unknown object type: "
                          << type << '\n';

                return false;
            }
        }

        // -------------------------------------------------
        // Tasks
        // -------------------------------------------------

        if (root.contains("tasks"))
        {
            for (const auto& taskJson : root.at("tasks"))
            {
                Task task;

                task.id =
                    taskJson.at("id").get<int>();

                task.count =
                    taskJson.at("count").get<int>();

                const std::string subtask =
                    taskJson.at("subtask").get<std::string>();

                if (subtask == "TakeAndPut")
                {
                    task.subtask =
                        Subtask::TakeAndPut;
                }
                else if (subtask == "Operate")
                {
                    task.subtask =
                        Subtask::Operate;
                }
                else if (subtask == "Goto")
                {
                    task.subtask =
                        Subtask::Goto;
                }
                else
                {
                    std::cerr << "Unknown subtask type: "
                              << subtask << '\n';

                    return false;
                }

                task.a =
                    taskJson.at("a").get<int>();

                task.b =
                    taskJson.at("b").get<int>();

                state.tasks.push_back(task);
            }
        }

        state.dashboard.tasks_total = 0;

        for (const auto& task : state.tasks)
        {
            state.dashboard.tasks_total += task.count;
        }

        return true;
    }
    catch (const json::exception& e)
    {
        std::cerr << "JSON parsing error: "
                  << e.what() << '\n';

        return false;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error while parsing: "
                  << e.what() << '\n';

        return false;
    }
}
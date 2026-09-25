#pragma once

#include <string>

#include <SFML/Graphics.hpp>

#include "structs.h"
#include "simulation.h"
#include "renderer.h"

class Application
{
public:
    explicit Application(const std::string& jsonPath);

    bool init();
    void run();

private:
    void processEvents();
    void update(float dt);
    void draw();

private:
    std::string jsonPath;

    State state;

    sf::RenderWindow window;

    Simulation simulation;
    Renderer renderer;

    bool running;
    bool simulationOver;
};
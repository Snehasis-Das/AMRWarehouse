#include "application.h"
#include "parser.h"

#include <algorithm>
#include <iostream>

Application::Application(const std::string& jsonPath)
    : jsonPath(jsonPath),
      running(true),
      simulationOver(false)
{
}

bool Application::init()
{
    // -------------------------------------------------
    // Parse JSON
    // -------------------------------------------------

    if (!parse(jsonPath, state))
    {
        std::cerr << "Failed to initialize simulator from JSON.\n";
        return false;
    }

    // -------------------------------------------------
    // Calculate window size from warehouse dimensions
    // -------------------------------------------------

    const sf::Vector2u desktopSize =
        sf::VideoMode::getDesktopMode().size;

    constexpr unsigned int MIN_DASHBOARD_WIDTH = 420;
    // Leave some space around the simulator window.
    constexpr unsigned int WINDOW_MARGIN = 80;

    const unsigned int availableWidth =
        desktopSize.x > WINDOW_MARGIN
            ? desktopSize.x - WINDOW_MARGIN
            : desktopSize.x;

    const unsigned int availableHeight =
        desktopSize.y > WINDOW_MARGIN
            ? desktopSize.y - WINDOW_MARGIN
            : desktopSize.y;

    if (availableWidth <= MIN_DASHBOARD_WIDTH)
    {
        std::cerr << "Not enough screen width for dashboard.\n";
        return false;
    }

    const unsigned int warehouseAvailableWidth =
        availableWidth - MIN_DASHBOARD_WIDTH;

    const unsigned int cellSize =
        std::min(
            warehouseAvailableWidth /
                static_cast<unsigned int>(
                    state.warehouse.width
                ),

            availableHeight /
                static_cast<unsigned int>(
                    state.warehouse.height
                )
        );
    
    if (cellSize == 0)
    {
        std::cerr << "Warehouse is too large for the available screen.\n";
        return false;
    }
    
    const unsigned int warehouseWidth =
        static_cast<unsigned int>(
            state.warehouse.width
        ) * cellSize;

    const unsigned int warehouseHeight =
        static_cast<unsigned int>(
            state.warehouse.height
        ) * cellSize;
    
    const unsigned int dashboardWidth =
        availableWidth - warehouseWidth;

    const unsigned int windowWidth =
        warehouseWidth + dashboardWidth;

    const unsigned int windowHeight =
        warehouseHeight;

    // -------------------------------------------------
    // Create window
    // -------------------------------------------------

    window.create(
        sf::VideoMode({
            windowWidth,
            windowHeight
        }),
        "AMR Simulator",
        sf::Style::Titlebar |
        sf::Style::Close
    );

    if (!window.isOpen())
    {
        std::cerr << "Failed to create SFML window.\n";
        return false;
    }

    // -------------------------------------------------
    // Initialize simulation and renderer
    // -------------------------------------------------

    simulation.init(state);

    renderer.init(window, state);

    renderer.start();

    return true;
}

void Application::run()
{
    sf::Clock deltaClock;
    sf::Clock frameClock;

    constexpr float targetFrameTime = 1.0f / 60.0f;

    while (window.isOpen())
    {
        frameClock.restart();

        const float dt =
            deltaClock.restart().asSeconds();

        processEvents();

        if (running && !simulationOver)
        {
            update(dt);
        }

        draw();

        const float frameTime =
            frameClock.getElapsedTime().asSeconds();

        if (frameTime < targetFrameTime)
        {
            sf::sleep(
                sf::seconds(targetFrameTime - frameTime)
            );
        }
    }
}

void Application::processEvents()
{
    while (const auto event = window.pollEvent())
    {
        if (event->is<sf::Event::Closed>())
        {
            window.close();
        }

        if (const auto* key =
                event->getIf<sf::Event::KeyPressed>())
        {
            // ---------------------------------------------
            // Escape
            // ---------------------------------------------

            if (key->code == sf::Keyboard::Key::Escape)
            {
                window.close();
            }

            // ---------------------------------------------
            // V - Run / Pause
            // ---------------------------------------------

            if (key->code == sf::Keyboard::Key::V)
            {
                if (simulationOver)
                {
                    continue;
                }

                running = !running;

                if (running)
                {
                    renderer.start();
                }
                else
                {
                    renderer.stop();
                }
            }

            // ---------------------------------------------
            // Arrow keys - Dashboard scroll
            // ---------------------------------------------

            if (key->code == sf::Keyboard::Key::Up)
            {
                renderer.scrollDashboard(-40.0f);
            }

            if (key->code == sf::Keyboard::Key::Down)
            {
                renderer.scrollDashboard(40.0f);
            }
            
        }
    }
}

void Application::update(float dt)
{
    simulation.simulate(dt);

    if (simulation.isOver())
    {
        simulationOver = true;
        running = false;

        renderer.stop();
        renderer.over();
    }
}

void Application::draw()
{
    renderer.render();

    if (simulationOver)
    {
        renderer.over();
    }

    window.display();
}
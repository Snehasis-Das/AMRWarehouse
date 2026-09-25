#pragma once

#include <SFML/Graphics.hpp>

#include "structs.h"

class Renderer
{
public:
    Renderer() = default;

    bool init(
        sf::RenderWindow& window,
        State& state
    );

    void start();
    void stop();

    void render();
    void over();

    void scrollDashboard(float delta);

private:
    void drawBackground();
    void drawGrid();

    void drawBoxes();
    void drawStations();
    void drawAMRs();
    void drawNodes();

    void drawAMRPath(const AMR& amr);
    void drawAMRAura(const AMR& amr);
    void drawCommunicationAura(float x, float y);

    void drawAMRDirection(const AMR& amr);

    void drawLabels();
    void drawSimulationOver();

    sf::Vector2f cellCenter(float x, float y) const;

    void drawTextureCentered(
        sf::Texture& texture,
        float x,
        float y,
        float scale
    );

    void drawDashboard();

private:
    sf::RenderWindow* window = nullptr;
    State* state = nullptr;

    sf::Texture amrTexture;
    sf::Texture boxTexture;
    sf::Texture stationTexture;
    sf::Texture nodeTexture;

    sf::Font font;

    float cellWidth = 0.0f;
    float cellHeight = 0.0f;

    float animationTime = 0.0f;
    sf::Clock animationClock;

    bool rendering = true;
    bool simulationOver = false;

    float dashboardX;
    float dashboardWidth;

    float dashboardScrollOffset = 0.0f;
    float dashboardContentHeight = 0.0f;
};
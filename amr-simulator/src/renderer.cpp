#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <filesystem>

namespace
{
    std::filesystem::path resourcePath(
        const std::string& filename
    )
    {
        return std::filesystem::current_path()
            / "resources"
            / "images"
            / filename;
    }

    // =================================================
    // VISUAL CONFIGURATION
    // =================================================

    // Object size as fraction of one grid cell.
    constexpr float AMR_SIZE = 0.65f;
    constexpr float BOX_SIZE = 0.70f;
    constexpr float STATION_SIZE = 0.80f;
    constexpr float NODE_SIZE = 0.65f;

    // -------------------------------------------------
    // AMR aura
    //
    // Radius is measured in grid cells.
    // -------------------------------------------------

    constexpr float AMR_AURA_RADIUS = 2.0f;

    // -------------------------------------------------
    // Animated waves
    // -------------------------------------------------

    constexpr int WAVE_COUNT = 3;
    constexpr float WAVE_PERIOD = 2.0f;

    // -------------------------------------------------
    // Line widths
    // -------------------------------------------------

    constexpr float GRID_LINE_THICKNESS = 1.0f;
    constexpr float PATH_THICKNESS = 4.0f;
    constexpr float PATH_POINT_RADIUS = 5.0f;

    constexpr float AMR_AURA_OUTLINE_THICKNESS = 2.0f;
    constexpr float COMMUNICATION_WAVE_THICKNESS = 3.0f;

    // -------------------------------------------------
    // Colors
    // -------------------------------------------------

    constexpr sf::Color BACKGROUND_COLOR(
        18, 22, 30
    );

    constexpr sf::Color GRID_COLOR(
        55, 64, 78
    );

    constexpr sf::Color PATH_COLOR(
        245, 65, 65
    );

    constexpr sf::Color AMR_AURA_FILL(
        40, 130, 255, 18
    );

    constexpr sf::Color AMR_AURA_OUTLINE(
        50, 155, 255, 85
    );

    constexpr sf::Color AMR_WAVE_COLOR(
        50, 160, 255
    );

    constexpr sf::Color COMMUNICATION_WAVE_COLOR(
        50, 235, 120
    );

    constexpr sf::Color AMR_LABEL_COLOR(
        235, 240, 250
    );

    constexpr sf::Color NODE_LABEL_COLOR(
        130, 255, 180
    );

    // -------------------------------------------------
    // Object information badges
    // -------------------------------------------------

    constexpr float BADGE_MARGIN = 1.0f;
    constexpr float BADGE_PADDING_X = 5.0f;
    constexpr float BADGE_PADDING_Y = 3.0f;
    constexpr float BADGE_MIN_WIDTH = 50.0f;
    constexpr float BADGE_HEIGHT = 36.0f;

    constexpr unsigned int BADGE_ID_FONT_SIZE = 13;
    constexpr unsigned int BADGE_STATUS_FONT_SIZE = 11;

    constexpr sf::Color BADGE_BACKGROUND(
        10, 14, 22, 180
    );

    constexpr sf::Color BADGE_OUTLINE(
        120, 130, 145, 120
    );

    constexpr float BADGE_OUTLINE_THICKNESS = 1.0f;

    constexpr sf::Color BADGE_ID_COLOR(
        245, 248, 252
    );

    constexpr sf::Color BADGE_STATUS_COLOR(
        190, 205, 220
    );

    // =================================================
    // Helpers
    // =================================================

    float clamp01(float value)
    {
        return std::max(0.0f, std::min(1.0f, value));
    }
}


// =====================================================
// INITIALIZATION
// =====================================================

bool Renderer::init(
    sf::RenderWindow& window,
    State& state
)
{
    this->window = &window;
    this->state = &state;

    const auto amrPath = 
        resourcePath("amr.png");

    const auto boxPath =
        resourcePath("box.png");

    const auto stationPath =
        resourcePath("station.png");

    const auto nodePath =
        resourcePath("node.png");

    if (!amrTexture.loadFromFile(amrPath))
    {
        std::cerr << "Failed to load: "
                  << amrPath.string() << '\n';
        return false;
    }

    if (!boxTexture.loadFromFile(boxPath))
    {
        std::cerr << "Failed to load: "
                  << boxPath.string() << '\n';
        return false;
    }

    if (!stationTexture.loadFromFile(stationPath))
    {
        std::cerr << "Failed to load: "
                  << stationPath.string() << '\n';
        return false;
    }

    if (!nodeTexture.loadFromFile(nodePath))
    {
        std::cerr << "Failed to load: "
                  << nodePath.string() << '\n';
        return false;
    }

    // -------------------------------------------------
    // Font
    // -------------------------------------------------

    if (!font.openFromFile(
            "resources/fonts/GoogleSans.ttf"))
    {
        std::cerr << "Failed to load "
                     "resources/fonts/GoogleSans.ttf\n";
        return false;
    }

    // -------------------------------------------------
    // Texture filtering
    // -------------------------------------------------

    amrTexture.setSmooth(true);
    boxTexture.setSmooth(true);
    stationTexture.setSmooth(true);
    nodeTexture.setSmooth(true);

    // -------------------------------------------------
    // Cell dimensions
    // -------------------------------------------------

    const sf::Vector2u windowSize =
    window.getSize();

    // The warehouse occupies the full window height.
    // Therefore the cell size is determined from height.
    // This keeps every warehouse cell square.
    cellHeight =
        static_cast<float>(windowSize.y) /
        static_cast<float>(state.warehouse.height);

    cellWidth = cellHeight;

    // -------------------------------------------------
    // Dashboard area
    // -------------------------------------------------

    dashboardX =
        static_cast<float>(state.warehouse.width) *
        cellWidth;

    dashboardWidth =
        static_cast<float>(windowSize.x) -
        dashboardX;

    animationTime = 0.0f;

    animationClock.restart();

    rendering = true;
    simulationOver = false;

    return true;
}


// =====================================================
// START / STOP
// =====================================================

void Renderer::start()
{
    rendering = true;

    // Prevent the paused duration from being included
    // in the next animation step.
    animationClock.restart();
}

void Renderer::stop()
{
    rendering = false;

    // Keep the animation frozen at its current state.
    animationClock.restart();
}


// =====================================================
// RENDER
// =====================================================

void Renderer::render()
{
    if (window == nullptr || state == nullptr)
    {
        return;
    }

    // -------------------------------------------------
    // Animation
    // -------------------------------------------------

    if (rendering)
    {
        animationTime +=
            animationClock.restart().asSeconds();
    }
    else
    {
        animationClock.restart();
    }

    // -------------------------------------------------
    // Begin frame
    // -------------------------------------------------

    window->clear(BACKGROUND_COLOR);

    // -------------------------------------------------
    // Layer 1: floor
    // -------------------------------------------------

    drawBackground();

    // -------------------------------------------------
    // Layer 2: grid
    // -------------------------------------------------

    drawGrid();

    // -------------------------------------------------
    // Layer 3: intended AMR paths
    // -------------------------------------------------

    for (const auto& amr : state->amrs)
    {
        drawAMRPath(amr);
    }

    // -------------------------------------------------
    // Layer 4: floor objects
    // -------------------------------------------------

    drawBoxes();
    drawStations();

    // -------------------------------------------------
    // Layer 5: AMR aura
    // -------------------------------------------------

    for (const auto& amr : state->amrs)
    {
        drawAMRAura(amr);
    }

    // -------------------------------------------------
    // Layer 6: AMRs
    // -------------------------------------------------

    drawAMRs();

    // -------------------------------------------------
    // Layer 7: ceiling objects
    //
    // Nodes are intentionally rendered after all
    // floor objects because they can share a position.
    // -------------------------------------------------

    drawNodes();

    // -------------------------------------------------
    // Layer 8: labels and dashboard
    // -------------------------------------------------

    drawLabels();

    drawDashboard();

    // -------------------------------------------------
    // Layer 9: simulation-over screen
    // -------------------------------------------------

    if (simulationOver)
    {
        drawSimulationOver();
    }
}

// =====================================================
// DASHBOARD
// =====================================================

void Renderer::drawDashboard()
{
    constexpr float HEADER_HEIGHT = 76.0f;
    constexpr float FOOTER_HEIGHT = 42.0f;

    constexpr float PANEL_PADDING = 18.0f;
    constexpr float SECTION_GAP = 18.0f;

    const float windowWidth =
        static_cast<float>(window->getSize().x);

    const float windowHeight =
        static_cast<float>(window->getSize().y);

    const float bodyTop =
        HEADER_HEIGHT;

    const float bodyHeight =
        windowHeight -
        HEADER_HEIGHT -
        FOOTER_HEIGHT;

    const float padding =
        PANEL_PADDING;

    /*
     * ---------------------------------------------------------
     * DASHBOARD BODY VIEW
     * ---------------------------------------------------------
     *
     * The dashboard body uses LOCAL coordinates.
     *
     * x = 0              -> left edge of dashboard
     * y = 0              -> top of dashboard body
     *
     * The sf::View handles the scrolling.
     */

    sf::View dashboardView;

    dashboardView.setSize({
        dashboardWidth,
        bodyHeight
    });

    dashboardView.setCenter({
        dashboardWidth / 2.0f,
        bodyHeight / 2.0f +
            dashboardScrollOffset
    });

    dashboardView.setViewport({
        {
            dashboardX / windowWidth,
            bodyTop / windowHeight
        },
        {
            dashboardWidth / windowWidth,
            bodyHeight / windowHeight
        }
    });

    const sf::View previousView =
        window->getView();

    window->setView(dashboardView);

    /*
     * ---------------------------------------------------------
     * DASHBOARD BODY BACKGROUND
     * ---------------------------------------------------------
     */

    sf::RectangleShape bodyBackground;

    bodyBackground.setPosition({
        0.0f,
        0.0f
    });

    bodyBackground.setSize({
        dashboardWidth,
        dashboardContentHeight > bodyHeight
            ? dashboardContentHeight
            : bodyHeight
    });

    bodyBackground.setFillColor(
        sf::Color(16, 20, 28)
    );

    window->draw(bodyBackground);

    /*
     * ---------------------------------------------------------
     * CONTENT
     * ---------------------------------------------------------
     */

    float y = 18.0f;

    const float contentX =
        padding;

    /*
     * ---------------------------------------------------------
     * STATUS
     * ---------------------------------------------------------
     */

    sf::RectangleShape statusBox;

    statusBox.setPosition({
        contentX,
        y
    });

    statusBox.setSize({
        dashboardWidth -
            2.0f * padding,
        48.0f
    });

    statusBox.setFillColor(
        rendering
            ? sf::Color(18, 45, 30)
            : sf::Color(45, 32, 20)
    );

    statusBox.setOutlineColor(
        rendering
            ? sf::Color(55, 180, 100)
            : sf::Color(210, 145, 55)
    );

    statusBox.setOutlineThickness(
        1.0f
    );

    window->draw(statusBox);

    sf::Text statusText(
        font,
        rendering ? "SIMULATION rendering"
                : "SIMULATION PAUSED",
        15
    );

    statusText.setFillColor(
        rendering
            ? sf::Color(100, 230, 145)
            : sf::Color(235, 175, 80)
    );

    statusText.setPosition({
        contentX + 14.0f,
        y + 14.0f
    });

    window->draw(statusText);

    y += 48.0f + SECTION_GAP;

    /*
     * ---------------------------------------------------------
     * FLEET
     * ---------------------------------------------------------
     */

    sf::Text fleetTitle(
        font,
        "FLEET",
        14
    );

    fleetTitle.setFillColor(
        sf::Color(180, 190, 205)
    );

    fleetTitle.setPosition({
        contentX,
        y
    });

    window->draw(fleetTitle);

    y += 26.0f;

    const float cardGap = 8.0f;

    const float cardWidth =
        (
            dashboardWidth -
            2.0f * padding -
            3.0f * cardGap
        ) / 4.0f;

    const float cardHeight = 72.0f;

    /*
     * DEPLOYED
     */

    sf::RectangleShape deployedCard;

    deployedCard.setPosition({
        contentX,
        y
    });

    deployedCard.setSize({
        cardWidth,
        cardHeight
    });

    deployedCard.setFillColor(
        sf::Color(25, 30, 40)
    );

    deployedCard.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    deployedCard.setOutlineThickness(
        1.0f
    );

    window->draw(deployedCard);

    sf::Text deployedLabel(
        font,
        "DEPLOYED",
        10
    );

    deployedLabel.setFillColor(
        sf::Color(155, 165, 180)
    );

    deployedLabel.setPosition({
        contentX + 8.0f,
        y + 9.0f
    });

    window->draw(deployedLabel);

    sf::Text deployedValue(
        font,
        std::to_string(
            state->dashboard.amrs_deployed
        ),
        22
    );

    deployedValue.setFillColor(
        sf::Color(240, 245, 250)
    );

    deployedValue.setPosition({
        contentX + 8.0f,
        y + 30.0f
    });

    window->draw(deployedValue);

    /*
     * ACTIVE
     */

    const float activeX =
        contentX +
        cardWidth +
        cardGap;

    sf::RectangleShape activeCard;

    activeCard.setPosition({
        activeX,
        y
    });

    activeCard.setSize({
        cardWidth,
        cardHeight
    });

    activeCard.setFillColor(
        sf::Color(25, 30, 40)
    );

    activeCard.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    activeCard.setOutlineThickness(
        1.0f
    );

    window->draw(activeCard);

    sf::Text activeLabel(
        font,
        "ACTIVE",
        10
    );

    activeLabel.setFillColor(
        sf::Color(155, 165, 180)
    );

    activeLabel.setPosition({
        activeX + 8.0f,
        y + 9.0f
    });

    window->draw(activeLabel);

    sf::Text activeValue(
        font,
        std::to_string(
            state->dashboard.amrs_active
        ),
        22
    );

    activeValue.setFillColor(
        sf::Color(100, 210, 255)
    );

    activeValue.setPosition({
        activeX + 8.0f,
        y + 30.0f
    });

    window->draw(activeValue);

    /*
     * IDLE
     */

    const float idleX =
        contentX +
        2.0f * (cardWidth + cardGap);

    sf::RectangleShape idleCard;

    idleCard.setPosition({
        idleX,
        y
    });

    idleCard.setSize({
        cardWidth,
        cardHeight
    });

    idleCard.setFillColor(
        sf::Color(25, 30, 40)
    );

    idleCard.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    idleCard.setOutlineThickness(
        1.0f
    );

    window->draw(idleCard);

    sf::Text idleLabel(
        font,
        "IDLE",
        10
    );

    idleLabel.setFillColor(
        sf::Color(155, 165, 180)
    );

    idleLabel.setPosition({
        idleX + 8.0f,
        y + 9.0f
    });

    window->draw(idleLabel);

    sf::Text idleValue(
        font,
        std::to_string(
            state->dashboard.amrs_idle
        ),
        22
    );

    idleValue.setFillColor(
        sf::Color(220, 220, 225)
    );

    idleValue.setPosition({
        idleX + 8.0f,
        y + 30.0f
    });

    window->draw(idleValue);

    /*
     * TRANSMITTING
     */

    const float transmittingX =
        contentX +
        3.0f * (cardWidth + cardGap);

    sf::RectangleShape transmittingCard;

    transmittingCard.setPosition({
        transmittingX,
        y
    });

    transmittingCard.setSize({
        cardWidth,
        cardHeight
    });

    transmittingCard.setFillColor(
        sf::Color(25, 30, 40)
    );

    transmittingCard.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    transmittingCard.setOutlineThickness(
        1.0f
    );

    window->draw(transmittingCard);

    sf::Text transmittingLabel(
        font,
        "TX",
        10
    );

    transmittingLabel.setFillColor(
        sf::Color(155, 165, 180)
    );

    transmittingLabel.setPosition({
        transmittingX + 8.0f,
        y + 9.0f
    });

    window->draw(transmittingLabel);

    sf::Text transmittingValue(
        font,
        std::to_string(
            state->dashboard.amrs_transmitting
        ),
        22
    );

    transmittingValue.setFillColor(
        sf::Color(120, 220, 150)
    );

    transmittingValue.setPosition({
        transmittingX + 8.0f,
        y + 30.0f
    });

    window->draw(transmittingValue);

    y += cardHeight + SECTION_GAP;

    /*
     * ---------------------------------------------------------
     * TASK PROGRESS
     * ---------------------------------------------------------
     */

    sf::Text taskTitle(
        font,
        "TASK PROGRESS",
        14
    );

    taskTitle.setFillColor(
        sf::Color(180, 190, 205)
    );

    taskTitle.setPosition({
        contentX,
        y
    });

    window->draw(taskTitle);

    y += 27.0f;

    const float taskBoxHeight = 90.0f;

    sf::RectangleShape taskBox;

    taskBox.setPosition({
        contentX,
        y
    });

    taskBox.setSize({
        dashboardWidth -
            2.0f * padding,
        taskBoxHeight
    });

    taskBox.setFillColor(
        sf::Color(25, 30, 40)
    );

    taskBox.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    taskBox.setOutlineThickness(
        1.0f
    );

    window->draw(taskBox);

    /*
     * COMPLETED / TOTAL
     */

    sf::Text taskText(
        font,
        std::to_string(
            state->dashboard.tasks_completed
        ) +
        " / " +
        std::to_string(
            state->dashboard.tasks_total
        ),
        24
    );

    taskText.setFillColor(
        sf::Color(240, 245, 250)
    );

    taskText.setPosition({
        contentX + 12.0f,
        y + 12.0f
    });

    window->draw(taskText);

    sf::Text taskLabel(
        font,
        "TASKS COMPLETED",
        10
    );

    taskLabel.setFillColor(
        sf::Color(155, 165, 180)
    );

    taskLabel.setPosition({
        contentX + 12.0f,
        y + 48.0f
    });

    window->draw(taskLabel);

    /*
     * PROGRESS BAR
     */

    const float progressBarX =
        contentX + 125.0f;

    const float progressBarY =
        y + 20.0f;

    const float progressBarWidth =
        dashboardWidth -
        2.0f * padding -
        145.0f;

    const float progressBarHeight =
        18.0f;

    sf::RectangleShape progressBackground;

    progressBackground.setPosition({
        progressBarX,
        progressBarY
    });

    progressBackground.setSize({
        progressBarWidth,
        progressBarHeight
    });

    progressBackground.setFillColor(
        sf::Color(40, 45, 55)
    );

    window->draw(progressBackground);

    float progress = 0.0f;

    if (state->dashboard.tasks_total > 0)
    {
        progress =
            static_cast<float>(
                state->dashboard.tasks_completed
            ) /
            static_cast<float>(
                state->dashboard.tasks_total
            );
    }

    progress =
        std::max(
            0.0f,
            std::min(progress, 1.0f)
        );

    sf::RectangleShape progressFill;

    progressFill.setPosition({
        progressBarX,
        progressBarY
    });

    progressFill.setSize({
        progressBarWidth * progress,
        progressBarHeight
    });

    progressFill.setFillColor(
        sf::Color(70, 190, 110)
    );

    window->draw(progressFill);

    y += taskBoxHeight + SECTION_GAP;

    /*
     * ---------------------------------------------------------
     * CURRENT TASK
     * ---------------------------------------------------------
     */

    sf::Text currentTaskTitle(
        font,
        "CURRENT TASK",
        14
    );

    currentTaskTitle.setFillColor(
        sf::Color(180, 190, 205)
    );

    currentTaskTitle.setPosition({
        contentX,
        y
    });

    window->draw(currentTaskTitle);

    y += 27.0f;

    sf::RectangleShape currentTaskBox;

    currentTaskBox.setPosition({
        contentX,
        y
    });

    currentTaskBox.setSize({
        dashboardWidth -
            2.0f * padding,
        58.0f
    });

    currentTaskBox.setFillColor(
        sf::Color(25, 30, 40)
    );

    currentTaskBox.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    currentTaskBox.setOutlineThickness(
        1.0f
    );

    window->draw(currentTaskBox);

    std::string currentTaskString;

    if (state->dashboard.current_task < 0)
    {
        currentTaskString =
            "No active task";
    }
    else
    {
        currentTaskString =
            "Task #" +
            std::to_string(
                state->dashboard.current_task
            );
    }

    sf::Text currentTaskText(
        font,
        currentTaskString,
        16
    );

    currentTaskText.setFillColor(
        sf::Color(235, 240, 245)
    );

    currentTaskText.setPosition({
        contentX + 12.0f,
        y + 18.0f
    });

    window->draw(currentTaskText);

    y += 58.0f + SECTION_GAP;

    /*
     * ---------------------------------------------------------
     * BATTERY
     * ---------------------------------------------------------
     */

    sf::Text batteryTitle(
        font,
        "BATTERY",
        14
    );

    batteryTitle.setFillColor(
        sf::Color(180, 190, 205)
    );

    batteryTitle.setPosition({
        contentX,
        y
    });

    window->draw(batteryTitle);

    y += 27.0f;

    sf::RectangleShape batteryBox;

    batteryBox.setPosition({
        contentX,
        y
    });

    batteryBox.setSize({
        dashboardWidth -
            2.0f * padding,
        72.0f
    });

    batteryBox.setFillColor(
        sf::Color(25, 30, 40)
    );

    batteryBox.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    batteryBox.setOutlineThickness(
        1.0f
    );

    window->draw(batteryBox);

    sf::Text batteryText(
        font,
        std::to_string(
            state->dashboard.average_battery
        ) + "%",
        22
    );

    batteryText.setFillColor(
        sf::Color(240, 245, 250)
    );

    batteryText.setPosition({
        contentX + 12.0f,
        y + 11.0f
    });

    window->draw(batteryText);

    const float batteryBarX =
        contentX + 95.0f;

    const float batteryBarY =
        y + 22.0f;

    const float batteryBarWidth =
        dashboardWidth -
        2.0f * padding -
        115.0f;

    const float batteryBarHeight =
        18.0f;

    sf::RectangleShape batteryBackground;

    batteryBackground.setPosition({
        batteryBarX,
        batteryBarY
    });

    batteryBackground.setSize({
        batteryBarWidth,
        batteryBarHeight
    });

    batteryBackground.setFillColor(
        sf::Color(40, 45, 55)
    );

    window->draw(batteryBackground);

    float battery =
        static_cast<float>(
            state->dashboard.average_battery
        ) / 100.0f;

    battery =
        std::max(
            0.0f,
            std::min(battery, 1.0f)
        );

    sf::RectangleShape batteryFill;

    batteryFill.setPosition({
        batteryBarX,
        batteryBarY
    });

    batteryFill.setSize({
        batteryBarWidth * battery,
        batteryBarHeight
    });

    batteryFill.setFillColor(
        sf::Color(80, 190, 110)
    );

    window->draw(batteryFill);

    y += 72.0f + SECTION_GAP;

    /*
     * ---------------------------------------------------------
     * SYSTEM EVENTS
     * ---------------------------------------------------------
     */

    sf::Text eventsTitle(
        font,
        "SYSTEM EVENTS",
        14
    );

    eventsTitle.setFillColor(
        sf::Color(180, 190, 205)
    );

    eventsTitle.setPosition({
        contentX,
        y
    });

    window->draw(eventsTitle);

    y += 27.0f;

    const float eventCardWidth =
        (
            dashboardWidth -
            2.0f * padding -
            2.0f * cardGap
        ) / 3.0f;

    const float eventCardHeight = 72.0f;

    /*
     * COLLISIONS
     */

    sf::RectangleShape collisionCard;

    collisionCard.setPosition({
        contentX,
        y
    });

    collisionCard.setSize({
        eventCardWidth,
        eventCardHeight
    });

    collisionCard.setFillColor(
        sf::Color(25, 30, 40)
    );

    collisionCard.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    collisionCard.setOutlineThickness(
        1.0f
    );

    window->draw(collisionCard);

    sf::Text collisionLabel(
        font,
        "COLLISIONS",
        10
    );

    collisionLabel.setFillColor(
        sf::Color(155, 165, 180)
    );

    collisionLabel.setPosition({
        contentX + 8.0f,
        y + 9.0f
    });

    window->draw(collisionLabel);

    sf::Text collisionValue(
        font,
        std::to_string(
            state->dashboard.collisions
        ),
        22
    );

    collisionValue.setFillColor(
        sf::Color(235, 100, 100)
    );

    collisionValue.setPosition({
        contentX + 8.0f,
        y + 30.0f
    });

    window->draw(collisionValue);

    /*
     * DEADLOCKS
     */

    const float deadlockX =
        contentX +
        eventCardWidth +
        cardGap;

    sf::RectangleShape deadlockCard;

    deadlockCard.setPosition({
        deadlockX,
        y
    });

    deadlockCard.setSize({
        eventCardWidth,
        eventCardHeight
    });

    deadlockCard.setFillColor(
        sf::Color(25, 30, 40)
    );

    deadlockCard.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    deadlockCard.setOutlineThickness(
        1.0f
    );

    window->draw(deadlockCard);

    sf::Text deadlockLabel(
        font,
        "DEADLOCKS",
        10
    );

    deadlockLabel.setFillColor(
        sf::Color(155, 165, 180)
    );

    deadlockLabel.setPosition({
        deadlockX + 8.0f,
        y + 9.0f
    });

    window->draw(deadlockLabel);

    sf::Text deadlockValue(
        font,
        std::to_string(
            state->dashboard.deadlocks
        ),
        22
    );

    deadlockValue.setFillColor(
        sf::Color(235, 165, 80)
    );

    deadlockValue.setPosition({
        deadlockX + 8.0f,
        y + 30.0f
    });

    window->draw(deadlockValue);

    /*
     * REROUTES
     */

    const float rerouteX =
        contentX +
        2.0f * (eventCardWidth + cardGap);

    sf::RectangleShape rerouteCard;

    rerouteCard.setPosition({
        rerouteX,
        y
    });

    rerouteCard.setSize({
        eventCardWidth,
        eventCardHeight
    });

    rerouteCard.setFillColor(
        sf::Color(25, 30, 40)
    );

    rerouteCard.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    rerouteCard.setOutlineThickness(
        1.0f
    );

    window->draw(rerouteCard);

    sf::Text rerouteLabel(
        font,
        "REROUTES",
        10
    );

    rerouteLabel.setFillColor(
        sf::Color(155, 165, 180)
    );

    rerouteLabel.setPosition({
        rerouteX + 8.0f,
        y + 9.0f
    });

    window->draw(rerouteLabel);

    sf::Text rerouteValue(
        font,
        std::to_string(
            state->dashboard.reroutes
        ),
        22
    );

    rerouteValue.setFillColor(
        sf::Color(100, 180, 240)
    );

    rerouteValue.setPosition({
        rerouteX + 8.0f,
        y + 30.0f
    });

    window->draw(rerouteValue);

    y += eventCardHeight + SECTION_GAP;

    /*
     * ---------------------------------------------------------
     * ELAPSED TIME
     * ---------------------------------------------------------
     */

    sf::Text timeTitle(
        font,
        "SIMULATION TIME",
        14
    );

    timeTitle.setFillColor(
        sf::Color(180, 190, 205)
    );

    timeTitle.setPosition({
        contentX,
        y
    });

    window->draw(timeTitle);

    y += 27.0f;

    sf::RectangleShape timeBox;

    timeBox.setPosition({
        contentX,
        y
    });

    timeBox.setSize({
        dashboardWidth -
            2.0f * padding,
        58.0f
    });

    timeBox.setFillColor(
        sf::Color(25, 30, 40)
    );

    timeBox.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    timeBox.setOutlineThickness(
        1.0f
    );

    window->draw(timeBox);

    const int totalSeconds =
        state->dashboard.elapsed_seconds;

    const int minutes =
        totalSeconds / 60;

    const int seconds =
        totalSeconds % 60;

    std::string timeString =
        std::to_string(minutes) +
        ":" +
        (seconds < 10 ? "0" : "") +
        std::to_string(seconds);

    sf::Text timeText(
        font,
        timeString,
        20
    );

    timeText.setFillColor(
        sf::Color(240, 245, 250)
    );

    timeText.setPosition({
        contentX + 12.0f,
        y + 16.0f
    });

    window->draw(timeText);

    y += 58.0f;

    /*
     * ---------------------------------------------------------
     * SAVE CONTENT HEIGHT
     * ---------------------------------------------------------
     */

    dashboardContentHeight =
        y + 20.0f;

    /*
     * ---------------------------------------------------------
     * RESTORE NORMAL WINDOW VIEW
     * ---------------------------------------------------------
     */

    window->setView(previousView);

    /*
     * ---------------------------------------------------------
     * FIXED FOOTER
     * ---------------------------------------------------------
     */

    sf::RectangleShape footer;

    footer.setPosition({
        dashboardX,
        windowHeight -
            FOOTER_HEIGHT
    });

    footer.setSize({
        dashboardWidth,
        FOOTER_HEIGHT
    });

    footer.setFillColor(
        sf::Color(11, 15, 22)
    );

    footer.setOutlineColor(
        sf::Color(55, 65, 80)
    );

    footer.setOutlineThickness(
        1.0f
    );

    window->draw(footer);

    sf::Text footerText(
        font,
        "V  Run/Pause     ↑ ↓  Scroll Dashboard     ESC  Exit",
        11
    );

    footerText.setFillColor(
        sf::Color(150, 165, 180)
    );

    footerText.setPosition({
        dashboardX + 12.0f,
        windowHeight -
            FOOTER_HEIGHT +
            14.0f
    });

    window->draw(footerText);

    /*
     * ---------------------------------------------------------
     * SCROLL INDICATOR
     * ---------------------------------------------------------
     */

    const float maxScroll =
        std::max(
            0.0f,
            dashboardContentHeight -
            bodyHeight
        );

    if (maxScroll > 0.0f)
    {
        const float trackHeight =
            bodyHeight - 20.0f;

        const float trackX =
            dashboardX +
            dashboardWidth -
            8.0f;

        const float trackY =
            bodyTop + 10.0f;

        sf::RectangleShape scrollTrack;

        scrollTrack.setPosition({
            trackX,
            trackY
        });

        scrollTrack.setSize({
            3.0f,
            trackHeight
        });

        scrollTrack.setFillColor(
            sf::Color(45, 50, 60)
        );

        window->draw(scrollTrack);

        const float thumbHeight =
            std::max(
                30.0f,
                trackHeight *
                    (
                        bodyHeight /
                        dashboardContentHeight
                    )
            );

        const float scrollRatio =
            dashboardScrollOffset /
            maxScroll;

        sf::RectangleShape scrollThumb;

        scrollThumb.setPosition({
            trackX,
            trackY +
                (
                    trackHeight -
                    thumbHeight
                ) *
                scrollRatio
        });

        scrollThumb.setSize({
            3.0f,
            thumbHeight
        });

        scrollThumb.setFillColor(
            sf::Color(120, 130, 145)
        );

        window->draw(scrollThumb);
    }
}

void Renderer::scrollDashboard(float delta)
{
    dashboardScrollOffset += delta;

    const float windowHeight =
        static_cast<float>(
            window->getSize().y
        );

    constexpr float HEADER_HEIGHT = 76.0f;
    constexpr float FOOTER_HEIGHT = 42.0f;

    const float bodyHeight =
        windowHeight -
        HEADER_HEIGHT -
        FOOTER_HEIGHT;

    const float maxScroll =
        std::max(
            0.0f,
            dashboardContentHeight - bodyHeight
        );

    dashboardScrollOffset =
        std::max(
            0.0f,
            std::min(
                dashboardScrollOffset,
                maxScroll
            )
        );
}

// =====================================================
// SIMULATION OVER
// =====================================================

void Renderer::over()
{
    simulationOver = true;
    rendering = false;

    animationClock.restart();
}


// =====================================================
// GRID COORDINATE → SCREEN COORDINATE
// =====================================================

sf::Vector2f Renderer::cellCenter(
    float x,
    float y
) const
{
    return {
        (x + 0.5f) * cellWidth,
        (y + 0.5f) * cellHeight
    };
}


// =====================================================
// BACKGROUND
// =====================================================

void Renderer::drawBackground()
{
    sf::RectangleShape background;

    background.setSize({
        cellWidth *
            static_cast<float>(
                state->warehouse.width),

        cellHeight *
            static_cast<float>(
                state->warehouse.height)
    });

    background.setPosition({
        0.0f,
        0.0f
    });

    background.setFillColor(
        BACKGROUND_COLOR
    );

    window->draw(background);
}


// =====================================================
// GRID
// =====================================================

void Renderer::drawGrid()
{
    const float width =
        cellWidth *
        static_cast<float>(
            state->warehouse.width);

    const float height =
        cellHeight *
        static_cast<float>(
            state->warehouse.height);

    // -------------------------------------------------
    // Vertical grid lines
    //
    // x = column
    // -------------------------------------------------

    for (int x = 0;
         x <= state->warehouse.width;
         ++x)
    {
        sf::RectangleShape line;

        line.setSize({
            GRID_LINE_THICKNESS,
            height
        });

        line.setPosition({
            static_cast<float>(x) * cellWidth,
            0.0f
        });

        line.setFillColor(
            GRID_COLOR
        );

        window->draw(line);
    }

    // -------------------------------------------------
    // Horizontal grid lines
    //
    // y = row
    // -------------------------------------------------

    for (int y = 0;
         y <= state->warehouse.height;
         ++y)
    {
        sf::RectangleShape line;

        line.setSize({
            width,
            GRID_LINE_THICKNESS
        });

        line.setPosition({
            0.0f,
            static_cast<float>(y) * cellHeight
        });

        line.setFillColor(
            GRID_COLOR
        );

        window->draw(line);
    }
}


// =====================================================
// DRAW TEXTURE INSIDE ONE CELL
// =====================================================

void Renderer::drawTextureCentered(
    sf::Texture& texture,
    float x,
    float y,
    float scale
)
{
    sf::Sprite sprite(texture);

    const sf::Vector2u textureSize =
        texture.getSize();

    sprite.setOrigin({
        static_cast<float>(
            textureSize.x) / 2.0f,

        static_cast<float>(
            textureSize.y) / 2.0f
    });

    sprite.setPosition(
        cellCenter(x, y)
    );

    const float maxDimension =
        static_cast<float>(
            std::max(
                textureSize.x,
                textureSize.y
            )
        );

    const float targetSize =
        std::min(cellWidth, cellHeight)
        * scale;

    const float finalScale =
        targetSize / maxDimension;

    sprite.setScale({
        finalScale,
        finalScale
    });

    window->draw(sprite);
}


// =====================================================
// BOXES
// =====================================================

void Renderer::drawBoxes()
{
    for (const auto& box : state->boxes)
    {
        drawTextureCentered(
            boxTexture,
            static_cast<float>(box.x),
            static_cast<float>(box.y),
            BOX_SIZE
        );
    }
}


// =====================================================
// STATIONS
// =====================================================

void Renderer::drawStations()
{
    for (const auto& station :
         state->stations)
    {
        drawTextureCentered(
            stationTexture,
            static_cast<float>(station.x),
            static_cast<float>(station.y),
            STATION_SIZE
        );
    }
}


// =====================================================
// AMRS
// =====================================================

void Renderer::drawAMRs()
{
    for (const auto& amr : state->amrs)
    {
        sf::Sprite sprite(amrTexture);

        const sf::Vector2u textureSize =
            amrTexture.getSize();

        sprite.setOrigin({
            static_cast<float>(
                textureSize.x) / 2.0f,

            static_cast<float>(
                textureSize.y) / 2.0f
        });

        // -------------------------------------------------
        // IMPORTANT:
        //
        // AMR x/y are FLOAT coordinates.
        // No integer conversion occurs.
        // -------------------------------------------------

        sprite.setPosition(
            cellCenter(
                amr.x,
                amr.y
            )
        );

        const float maxDimension =
            static_cast<float>(
                std::max(
                    textureSize.x,
                    textureSize.y
                )
            );

        const float targetSize =
            std::min(cellWidth, cellHeight)
            * AMR_SIZE;

        const float scale =
            targetSize / maxDimension;

        sprite.setScale({
            scale,
            scale
        });

        // PNG front is assumed to point upward.
        sprite.setRotation(
            sf::degrees(amr.heading)
        );

        window->draw(sprite);
    }
}


// =====================================================
// NODES
// =====================================================

void Renderer::drawNodes()
{
    for (const auto& node :
         state->nodes)
    {
        // -------------------------------------------------
        // Node communication waves
        //
        // Radius = node_range cells.
        //
        // There is deliberately NO window-boundary cap.
        // SFML clips whatever lies outside the window.
        // -------------------------------------------------

        if (node.transmitting)
        {
            drawCommunicationAura(
                static_cast<float>(node.x),
                static_cast<float>(node.y)
            );
        }

        // -------------------------------------------------
        // Node itself
        //
        // Node occupies exactly one grid cell.
        // It is drawn last because it is a ceiling object.
        // -------------------------------------------------

        drawTextureCentered(
            nodeTexture,
            static_cast<float>(node.x),
            static_cast<float>(node.y),
            NODE_SIZE
        );
    }
}


// =====================================================
// AMR INTENDED PATH
// =====================================================

void Renderer::drawAMRPath(
    const AMR& amr
)
{
    if (amr.intended_path.empty())
    {
        return;
    }

    std::vector<sf::Vertex> vertices;

    vertices.reserve(
        amr.intended_path.size() + 1
    );

    // -------------------------------------------------
    // Start at current continuous AMR position
    // -------------------------------------------------

    vertices.emplace_back(
        cellCenter(
            amr.x,
            amr.y
        ),
        PATH_COLOR
    );

    // -------------------------------------------------
    // Continue through logical path cells
    // -------------------------------------------------

    for (const Position& position :
         amr.intended_path)
    {
        vertices.emplace_back(
            cellCenter(
                static_cast<float>(
                    position.x),

                static_cast<float>(
                    position.y)
            ),

            PATH_COLOR
        );
    }

    // -------------------------------------------------
    // Path
    // -------------------------------------------------

    window->draw(
        vertices.data(),
        vertices.size(),
        sf::PrimitiveType::LineStrip
    );

    // -------------------------------------------------
    // Destination marker
    // -------------------------------------------------

    const Position& destination =
        amr.intended_path.back();

    sf::CircleShape marker(
        PATH_POINT_RADIUS
    );

    marker.setOrigin({
        PATH_POINT_RADIUS,
        PATH_POINT_RADIUS
    });

    marker.setPosition(
        cellCenter(
            static_cast<float>(
                destination.x),

            static_cast<float>(
                destination.y)
        )
    );

    marker.setFillColor(
        PATH_COLOR
    );

    window->draw(marker);
}


// =====================================================
// AMR BLUE AURA
// =====================================================

void Renderer::drawAMRAura(const AMR& amr)
{
    const sf::Vector2f center =
        cellCenter(amr.x, amr.y);

    const float cellSize =
        std::min(cellWidth, cellHeight);

    const float radius =
        AMR_AURA_RADIUS * cellSize;

    // -------------------------------------------------
    // Fixed blue aura
    // -------------------------------------------------

    sf::CircleShape aura(radius);

    aura.setOrigin({
        radius,
        radius
    });

    aura.setPosition(center);

    aura.setFillColor(AMR_AURA_FILL);

    aura.setOutlineThickness(
        AMR_AURA_OUTLINE_THICKNESS
    );

    aura.setOutlineColor(
        AMR_AURA_OUTLINE
    );

    window->draw(aura);

    // -------------------------------------------------
    // Animated blue waves
    // -------------------------------------------------

    const float phase =
        std::fmod(
            animationTime,
            WAVE_PERIOD
        ) / WAVE_PERIOD;

    for (int i = 0; i < WAVE_COUNT; ++i)
    {
        const float wavePhase =
            std::fmod(
                phase +
                static_cast<float>(i) /
                    static_cast<float>(WAVE_COUNT),
                1.0f
            );

        const float waveRadius =
            wavePhase * radius;

        const float alpha =
            180.0f * (1.0f - wavePhase);

        sf::CircleShape wave(waveRadius);

        wave.setOrigin({
            waveRadius,
            waveRadius
        });

        wave.setPosition(center);

        wave.setFillColor(
            sf::Color::Transparent
        );

        wave.setOutlineThickness(
            AMR_AURA_OUTLINE_THICKNESS
        );

        wave.setOutlineColor(
            sf::Color(
                AMR_WAVE_COLOR.r,
                AMR_WAVE_COLOR.g,
                AMR_WAVE_COLOR.b,
                static_cast<std::uint8_t>(alpha)
            )
        );

        window->draw(wave);
    }

    // -------------------------------------------------
    // Green communication waves
    // -------------------------------------------------

    if (amr.transmitting)
    {
        drawCommunicationAura(
            amr.x,
            amr.y
        );
    }
}


// =====================================================
// COMMUNICATION WAVES
// =====================================================

void Renderer::drawCommunicationAura(
    float x,
    float y
)
{
    const sf::Vector2f center =
        cellCenter(x, y);

    // -------------------------------------------------
    // Node range is expressed in grid cells.
    // -------------------------------------------------

    const float radius =
        static_cast<float>(
            state->node_range
        )
        *
        std::min(
            cellWidth,
            cellHeight
        );

    // -------------------------------------------------
    // Animated concentric waves
    // -------------------------------------------------

    const float phase =
        std::fmod(
            animationTime,
            WAVE_PERIOD
        )
        /
        WAVE_PERIOD;

    for (int i = 0;
         i < WAVE_COUNT;
         ++i)
    {
        const float wavePhase =
            std::fmod(
                phase +
                static_cast<float>(i) /
                    static_cast<float>(WAVE_COUNT),

                1.0f
            );

        const float currentRadius =
            wavePhase * radius;

        const float alpha =
            190.0f *
            (1.0f - wavePhase);

        sf::CircleShape wave(
            currentRadius
        );

        wave.setOrigin({
            currentRadius,
            currentRadius
        });

        wave.setPosition(center);

        wave.setFillColor(
            sf::Color::Transparent
        );

        wave.setOutlineThickness(
            COMMUNICATION_WAVE_THICKNESS
        );

        wave.setOutlineColor(
            sf::Color(
                COMMUNICATION_WAVE_COLOR.r,
                COMMUNICATION_WAVE_COLOR.g,
                COMMUNICATION_WAVE_COLOR.b,
                static_cast<std::uint8_t>(
                    clamp01(alpha / 255.0f) * 255.0f
                )
            )
        );

        window->draw(wave);
    }
}


// =====================================================
// LABELS
// =====================================================

void Renderer::drawLabels()
{
    // -------------------------------------------------
    // Object information badges
    //
    // Every badge is anchored to the top-right corner
    // of the object's grid cell. The badge is deliberately
    // small and translucent so it does not dominate the
    // warehouse view.
    // -------------------------------------------------

    auto drawBadge =
        [&](int id,
            const std::string& status,
            float cellX,
            float cellY)
    {
        sf::Text idText(
            font,
            "#" + std::to_string(id),
            BADGE_ID_FONT_SIZE
        );

        sf::Text statusText(
            font,
            status,
            BADGE_STATUS_FONT_SIZE
        );

        idText.setFillColor(BADGE_ID_COLOR);
        statusText.setFillColor(BADGE_STATUS_COLOR);

        const auto idBounds =
            idText.getLocalBounds();

        const auto statusBounds =
            statusText.getLocalBounds();

        const float textWidth =
            std::max(
                idBounds.size.x,
                statusBounds.size.x
            );

        const float badgeWidth =
            std::max(
                BADGE_MIN_WIDTH,
                textWidth + BADGE_PADDING_X * 2.0f
            );

        sf::RectangleShape badge;

        badge.setSize({
            badgeWidth,
            BADGE_HEIGHT
        });

        badge.setFillColor(BADGE_BACKGROUND);
        badge.setOutlineThickness(
            BADGE_OUTLINE_THICKNESS
        );
        badge.setOutlineColor(BADGE_OUTLINE);

        const float right =
            (std::floor(cellX) + 1.0f) * cellWidth;

        const float top =
            std::floor(cellY) * cellHeight;

        const float badgeX =
            right - badgeWidth - BADGE_MARGIN;

        const float badgeY =
            top + BADGE_MARGIN;

        badge.setPosition({
            badgeX,
            badgeY
        });

        window->draw(badge);

        idText.setPosition({
            badgeX + BADGE_PADDING_X,
            badgeY + BADGE_PADDING_Y
        });

        window->draw(idText);

        statusText.setPosition({
            badgeX + BADGE_PADDING_X,
            badgeY + BADGE_HEIGHT / 2.0f
        });

        window->draw(statusText);
    };

    // -------------------------------------------------
    // Boxes
    // -------------------------------------------------

    for (const auto& box : state->boxes)
    {
        drawBadge(
            box.id,
            std::to_string(box.duration) + "s",
            static_cast<float>(box.x),
            static_cast<float>(box.y)
        );
    }

    // -------------------------------------------------
    // Stations
    // -------------------------------------------------

    for (const auto& station : state->stations)
    {
        drawBadge(
            station.id,
            "x" + std::to_string(station.count),
            static_cast<float>(station.x),
            static_cast<float>(station.y)
        );
    }

    // -------------------------------------------------
    // Nodes
    // -------------------------------------------------

    for (const auto& node : state->nodes)
    {
        drawBadge(
            node.id,
            node.transmitting ? "TX" : "OFF",
            static_cast<float>(node.x),
            static_cast<float>(node.y)
        );
    }

    // -------------------------------------------------
    // AMRs
    // -------------------------------------------------

    for (const auto& amr : state->amrs)
    {
        drawBadge(
            amr.id,
            std::to_string(amr.battery) + "%",
            amr.x,
            amr.y
        );
    }
}


// =====================================================
// SIMULATION OVER
// =====================================================

void Renderer::drawSimulationOver()
{
    const sf::Vector2u windowSize =
        window->getSize();

    // -------------------------------------------------
    // Dark overlay
    // -------------------------------------------------

    sf::RectangleShape overlay;

    overlay.setSize({
        static_cast<float>(
            windowSize.x),

        static_cast<float>(
            windowSize.y)
    });

    overlay.setFillColor(
        sf::Color(5, 8, 15, 190)
    );

    window->draw(overlay);

    // -------------------------------------------------
    // Title
    // -------------------------------------------------

    sf::Text title(
        font,
        "SIMULATION COMPLETE",
        42
    );

    title.setFillColor(
        sf::Color(100, 230, 150)
    );

    const auto titleBounds =
        title.getLocalBounds();

    title.setOrigin({
        titleBounds.position.x +
            titleBounds.size.x / 2.0f,

        titleBounds.position.y +
            titleBounds.size.y / 2.0f
    });

    title.setPosition({
        static_cast<float>(
            windowSize.x) / 2.0f,

        static_cast<float>(
            windowSize.y) / 2.0f - 25.0f
    });

    window->draw(title);

    // -------------------------------------------------
    // Exit instruction
    // -------------------------------------------------

    sf::Text instruction(
        font,
        "Press ESC to close",
        20
    );

    instruction.setFillColor(
        sf::Color(210, 215, 225)
    );

    const auto instructionBounds =
        instruction.getLocalBounds();

    instruction.setOrigin({
        instructionBounds.position.x +
            instructionBounds.size.x / 2.0f,

        instructionBounds.position.y +
            instructionBounds.size.y / 2.0f
    });

    instruction.setPosition({
        static_cast<float>(
            windowSize.x) / 2.0f,

        static_cast<float>(
            windowSize.y) / 2.0f + 35.0f
    });

    window->draw(instruction);
}
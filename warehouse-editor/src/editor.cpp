#include "editor.h"
#include "taskwork.h"
#include "grid.h"
#include "infobox.h"
#include "palette.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QGridLayout>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QProcess>
#include <QCoreApplication>
#include <QFileInfo>


Editor::Editor(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("AMR Warehouse Editor");

    createToolbar();
    createWorkspace();

    m_grid->setWarehouseSize(m_warehouseWidth, m_warehouseHeight);
    updateLayout();
}

void Editor::createToolbar()
{
    m_toolbar = new QToolBar("Main Toolbar", this);
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    QAction* newAction = m_toolbar->addAction("New");
    QAction* saveAction = m_toolbar->addAction("Save");
    QAction* loadAction = m_toolbar->addAction("Load");
    QAction* simulateAction = m_toolbar->addAction("Simulate");

    connect(newAction, &QAction::triggered, this, &Editor::newWarehouse);
    connect(saveAction, &QAction::triggered, this, &Editor::saveWarehouse);
    connect(loadAction, &QAction::triggered, this, &Editor::loadWarehouse);
    connect(simulateAction, &QAction::triggered, this, &Editor::simulate);

    addToolBar(Qt::TopToolBarArea, m_toolbar);
}

void Editor::createWorkspace()
{
    m_grid = new Grid(this);

    m_debug = new QPlainTextEdit(this);
    m_debug->setReadOnly(true);
    m_debug->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_debug->setStyleSheet(
        "QPlainTextEdit {"
        " background: #161616; color: white; border: 0px;"
        " padding: 6px; font-family: Consolas, 'Courier New', monospace;"
        "}"
    );

    m_infobox = new InfoBox(this);

    auto* central = new QWidget(this);

    auto* leftPanel = new QWidget(central);

    auto* leftLayout = new QVBoxLayout(leftPanel);

    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    m_palette = new Palette(leftPanel);
    m_taskWork = new TaskWork(leftPanel);

    leftLayout->addWidget(
        m_palette,
        0,
        Qt::AlignTop
    );

    leftLayout->addWidget(
        m_taskWork,
        1
    );

    auto* layout = new QGridLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(0);
    layout->setVerticalSpacing(0);

    layout->addWidget(leftPanel, 0, 0, 2, 1);
    layout->addWidget(m_grid, 0, 1);
    layout->addWidget(m_debug, 1, 1);
    layout->addWidget(m_infobox, 0, 2, 2, 1);

    layout->setColumnStretch(0, 0);
    layout->setColumnStretch(1, 0);
    layout->setColumnStretch(2, 0);
    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 0);

    setCentralWidget(central);

    connect(m_palette, &Palette::objectSelected, m_grid, &Grid::setSelectedType);
    connect(m_grid, &Grid::objectSelected, m_infobox, &InfoBox::showObject);
    connect(m_grid, &Grid::debugMessage, this, &Editor::log);
    connect(m_taskWork, &TaskWork::debugMessage, this, &Editor::log);
}

void Editor::updateLayout()
{
    if (!centralWidget() || !m_grid || !m_palette || !m_taskWork || !m_infobox || !m_debug)
        return;

    constexpr int minPaletteWidth = 170;
    constexpr int preferredPaletteWidth = 220;
    constexpr int maxPaletteWidth = 280;
    constexpr int minInfoWidth = 220;
    constexpr int preferredInfoWidth = 280;
    constexpr int maxInfoWidth = 360;
    constexpr int minGridWidth = 400;
    constexpr int minGridHeight = 280;
    constexpr int maxGridWidth = 1000;
    constexpr int maxGridHeight = 700;
    constexpr int debugHeight = 150;

    const int availableWidth = centralWidget()->width();
    const int availableHeight = centralWidget()->height();

    int paletteWidth = qBound(minPaletteWidth, preferredPaletteWidth, maxPaletteWidth);
    int infoWidth = qBound(minInfoWidth, preferredInfoWidth, maxInfoWidth);

    const double ratio = static_cast<double>(m_warehouseWidth) /
                         static_cast<double>(m_warehouseHeight);

    int maxWidth = qMin(maxGridWidth, availableWidth - paletteWidth - infoWidth);
    int maxHeight = qMin(maxGridHeight, availableHeight - debugHeight);

    if (maxWidth < minGridWidth) {
        const int deficit = minGridWidth - maxWidth;
        const int reducePalette = qMin(deficit / 2, paletteWidth - minPaletteWidth);
        paletteWidth -= reducePalette;
        const int remainingDeficit = deficit - reducePalette;
        infoWidth -= qMin(remainingDeficit, infoWidth - minInfoWidth);
        maxWidth = qMin(maxGridWidth, availableWidth - paletteWidth - infoWidth);
    }

    int gridWidth = qMax(1, maxWidth);
    int gridHeight = static_cast<int>(gridWidth / ratio);

    if (gridHeight > maxHeight) {
        gridHeight = qMax(1, maxHeight);
        gridWidth = static_cast<int>(gridHeight * ratio);
    }

    if (gridWidth < minGridWidth && maxWidth >= minGridWidth) {
        gridWidth = minGridWidth;
        gridHeight = static_cast<int>(gridWidth / ratio);
    }

    if (gridHeight < minGridHeight && maxHeight >= minGridHeight) {
        gridHeight = minGridHeight;
        gridWidth = static_cast<int>(gridHeight * ratio);
    }

    gridWidth = qMin(gridWidth, maxWidth);
    gridHeight = qMin(gridHeight, maxHeight);

    m_palette->setFixedWidth(paletteWidth);
    const int paletteHeight = qMax(1, m_palette->heightForWidth(paletteWidth));
    m_palette->setFixedHeight(paletteHeight);
    m_taskWork->setFixedWidth(paletteWidth);
    m_infobox->setFixedWidth(infoWidth);
    m_grid->setFixedSize(gridWidth, gridHeight);
    m_debug->setFixedSize(gridWidth, debugHeight);
}

void Editor::newWarehouse()
{
    bool widthOk = false;
    bool heightOk = false;

    const int width = QInputDialog::getInt(
        this,
        "New Warehouse",
        "Width:",
        m_warehouseWidth,
        1,
        500,
        1,
        &widthOk
    );

    if (!widthOk)
        return;

    const int height = QInputDialog::getInt(
        this,
        "New Warehouse",
        "Height:",
        m_warehouseHeight,
        1,
        500,
        1,
        &heightOk
    );

    if (!heightOk)
        return;

    m_warehouseWidth = width;
    m_warehouseHeight = height;
    m_currentFile.clear();

    m_grid->setWarehouseSize(
        m_warehouseWidth,
        m_warehouseHeight
    );

    m_taskWork->clearTasks();

    updateLayout();
    log(QString("Created new warehouse: %1 x %2")
            .arg(width)
            .arg(height));
}

void Editor::saveWarehouse()
{
    const QString filepath = QFileDialog::getSaveFileName(
        this,
        "Save Warehouse",
        m_currentFile,
        "Warehouse JSON (*.json)"
    );

    if (filepath.isEmpty())
        return;

    saveWarehouseTo(filepath);
}

bool Editor::saveWarehouseTo(const QString& filepath)
{
    QJsonObject root;
    root["width"] = m_warehouseWidth;
    root["height"] = m_warehouseHeight;
    root["objects"] = m_grid->toJson();
    root["tasks"] = m_taskWork->toJson();

    QJsonObject fleet;
    fleet["amrs_outside_stations"] = m_grid->amrsOutsideStations();
    fleet["station_amrs"] = m_grid->stationAmrCount();
    fleet["total_amrs"] = m_grid->totalAmrCount();
    root["fleet"] = fleet;

    const int minimumRange = m_grid->minimumNodeRange();
    root["minimum_node_range"] = minimumRange;

    if (minimumRange < 0)
        log("WARNING: Could not cover all warehouse cells with the current nodes.");
    else
        log(QString("Minimum node range required to cover all cells: %1").arg(minimumRange));

    log(QString("AMRs outside stations: %1").arg(m_grid->amrsOutsideStations()));
    log(QString("AMRs assigned to stations: %1").arg(m_grid->stationAmrCount()));
    log(QString("Total AMRs available: %1").arg(m_grid->totalAmrCount()));

    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        log("ERROR: Could not open file for writing: " + filepath);
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    m_currentFile = filepath;
    log("Saved warehouse: " + filepath);
    return true;
}

void Editor::loadWarehouse()
{
    const QString filepath = QFileDialog::getOpenFileName(
        this,
        "Load Warehouse",
        QString(),
        "Warehouse JSON (*.json)"
    );

    if (filepath.isEmpty())
        return;

    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) {
        log("ERROR: Could not open file: " + filepath);
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        log("ERROR: Invalid JSON: " + error.errorString());
        return;
    }

    const QJsonObject root = document.object();

    if (!root.contains("width") || !root.contains("height")) {
        log("ERROR: JSON has no warehouse dimensions.");
        return;
    }

    const int width = root["width"].toInt();
    const int height = root["height"].toInt();

    if (width <= 0 || height <= 0) {
        log("ERROR: Invalid warehouse dimensions.");
        return;
    }

    m_warehouseWidth = width;
    m_warehouseHeight = height;

    m_grid->setWarehouseSize(width, height);
    m_grid->fromJson(root["objects"].toArray());

    if (root.contains("tasks") && root["tasks"].isArray())
        m_taskWork->fromJson(root["tasks"].toArray());
    else
        m_taskWork->clearTasks();

    m_currentFile = filepath;

    updateLayout();
    log("Loaded warehouse: " + filepath);
}

void Editor::simulate()
{
    auto launchSimulator = [this]()
    {
        if (m_currentFile.isEmpty())
        {
            return;
        }

        const QString simulatorPath =
            QCoreApplication::applicationDirPath()
            + "/AMRSimulator.exe";

        log("Simulation requested: " + m_currentFile);

        if (!QFileInfo::exists(simulatorPath))
        {
            log("ERROR: AMRSimulator.exe not found: " + simulatorPath);

            QMessageBox::critical(
                this,
                "Simulator Not Found",
                "AMRSimulator.exe could not be found next to the editor."
            );

            return;
        }

        if (!QProcess::startDetached(
                simulatorPath,
                { m_currentFile }))
        {
            log("ERROR: Failed to launch AMRSimulator.exe.");

            QMessageBox::critical(
                this,
                "Simulation Error",
                "Failed to launch the AMR simulator."
            );

            return;
        }

        log("Simulator launched successfully.");
    };

    if (!m_currentFile.isEmpty())
    {
        launchSimulator();
        return;
    }

    log("ERROR: No warehouse JSON is currently loaded.");

    QMessageBox dialog(
        QMessageBox::Warning,
        "No Warehouse Loaded",
        "There is no currently loaded warehouse.\n\n"
        "Save the current warehouse or choose an existing warehouse JSON to simulate.",
        QMessageBox::Cancel,
        this
    );

    QPushButton* saveButton =
        dialog.addButton(
            "Save Current Warehouse",
            QMessageBox::AcceptRole
        );

    QPushButton* chooseButton =
        dialog.addButton(
            "Choose Warehouse JSON",
            QMessageBox::ActionRole
        );

    dialog.exec();

    if (dialog.clickedButton() == saveButton)
    {
        saveWarehouse();

        if (!m_currentFile.isEmpty())
        {
            launchSimulator();
        }

        return;
    }

    if (dialog.clickedButton() == chooseButton)
    {
        loadWarehouse();

        if (!m_currentFile.isEmpty())
        {
            launchSimulator();
        }
    }
}

void Editor::log(const QString& message)
{
    if (m_debug)
        m_debug->appendPlainText(message);
}

void Editor::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateLayout();
}

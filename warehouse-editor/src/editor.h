#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QWidget>
#include <QString>

class Grid;
class Palette;
class InfoBox;
class QPlainTextEdit;
class QResizeEvent;

class Editor : public QMainWindow
{
    Q_OBJECT

public:
    explicit Editor(QWidget* parent = nullptr);
    ~Editor() override = default;

private:
    void createToolbar();
    void createWorkspace();
    void updateLayout();

    void newWarehouse();
    void saveWarehouse();
    void loadWarehouse();
    void simulate();

    bool saveWarehouseTo(const QString& filepath);

    void log(const QString& message);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QToolBar* m_toolbar = nullptr;

    Palette* m_palette = nullptr;
    Grid* m_grid = nullptr;
    QPlainTextEdit* m_debug = nullptr;
    InfoBox* m_infobox = nullptr;

    int m_warehouseWidth = 30;
    int m_warehouseHeight = 20;

    QString m_currentFile;
};

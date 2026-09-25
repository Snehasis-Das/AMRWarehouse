#pragma once

#include "task.h"

#include <QJsonArray>
#include <QList>
#include <QWidget>

class QScrollArea;
class QPushButton;
class QVBoxLayout;

class TaskWork : public QWidget
{
    Q_OBJECT

public:
    explicit TaskWork(QWidget* parent = nullptr);

    QJsonArray toJson() const;
    void fromJson(const QJsonArray& tasks);
    void clearTasks();

signals:
    void debugMessage(const QString& message);

private slots:
    void addTask();

private:
    void removeTask(int taskId);
    void rebuildList();

    QList<Task> m_tasks;
    int m_nextTaskId = 1;

    QScrollArea* m_scrollArea = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QPushButton* m_addButton = nullptr;
};
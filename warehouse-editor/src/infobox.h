#pragma once

#include <QWidget>

class QLabel;

class InfoBox : public QWidget
{
    Q_OBJECT

public:
    explicit InfoBox(QWidget* parent = nullptr);

public slots:
    void showObject(const QString& type, int id, int x, int y, const QString& metadata);

private:
    QLabel* m_image = nullptr;
    QLabel* m_type = nullptr;
    QLabel* m_id = nullptr;
    QLabel* m_position = nullptr;
    QLabel* m_metadata = nullptr;
};

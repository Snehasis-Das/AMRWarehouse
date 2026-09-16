#pragma once

#include <QWidget>

class Palette : public QWidget
{
    Q_OBJECT

public:
    explicit Palette(QWidget* parent = nullptr);

signals:
    void objectSelected(const QString& type);
};

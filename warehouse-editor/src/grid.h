#pragma once

#include <QJsonArray>
#include <QWidget>

class QDragEnterEvent;
class QDropEvent;
class QMouseEvent;
class QPaintEvent;

class Grid : public QWidget
{
    Q_OBJECT

public:
    explicit Grid(QWidget* parent = nullptr);

    void setWarehouseSize(int width, int height);
    void setSelectedType(const QString& type);

    QJsonArray toJson() const;
    void fromJson(const QJsonArray& objects);

    int minimumNodeRange() const;
    int amrsOutsideStations() const;
    int stationAmrCount() const;
    int totalAmrCount() const;

signals:
    void objectSelected(const QString& type, int x, int y, const QString& metadata);
    void debugMessage(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct Object {
        QString type;
        int x = 0;
        int y = 0;
        int taskTiming = 0;
        int amrCount = 0;
    };

    QString objectAt(int x, int y) const;
    Object* objectAtMutable(int x, int y);
    void addObject(const QString& type, int x, int y);
    void eraseObject(int x, int y);
    void configureObject(Object& object);

    QString imagePathForType(const QString& type) const;

private:
    int m_width = 10;
    int m_height = 10;
    QList<Object> m_objects;
    QString m_selectedType;
};

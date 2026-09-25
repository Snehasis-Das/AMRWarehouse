#include "grid.h"

#include <QDragEnterEvent>
#include <QInputDialog>
#include <QJsonObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QQueue>

Grid::Grid(QWidget* parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumSize(1, 1);
}

void Grid::setSelectedType(const QString& type)
{
    m_selectedType = type;
    emit debugMessage(QString("Selected tool: %1").arg(type));
}

void Grid::setWarehouseSize(int width, int height)
{
    m_width = qMax(1, width);
    m_height = qMax(1, height);
    m_objects.clear();
    m_nextBoxId = 1;
    m_nextStationId = 1;
    m_nextAmrId = 1;
    m_nextNodeId = 1;
    update();
}

QJsonArray Grid::toJson() const
{
    QJsonArray objects;

    for (const Object& object : m_objects) {
        QJsonObject value;
        value["id"] = object.id;
        value["type"] = object.type;
        value["x"] = object.x;
        value["y"] = object.y;

        if (object.type == "Box")
            value["task_timing"] = object.taskTiming;
        else if (object.type == "Station")
            value["amr_count"] = object.amrCount;

        objects.append(value);
    }

    return objects;
}

void Grid::fromJson(const QJsonArray& objects)
{
    m_objects.clear();

    m_nextBoxId = 1;
    m_nextStationId = 1;
    m_nextAmrId = 1;
    m_nextNodeId = 1;

    for (const QJsonValue& value : objects) {
        if (!value.isObject())
            continue;

        const QJsonObject object = value.toObject();
        const QString type = object["type"].toString();
        const int x = object["x"].toInt(-1);
        const int y = object["y"].toInt(-1);

        if (type.isEmpty() || x < 0 || y < 0 || x >= m_width || y >= m_height)
            continue;

        Object loaded;
        loaded.type = type;
        loaded.x = x;
        loaded.y = y;

        if (object.contains("id"))
            loaded.id = object["id"].toInt(0);
        else
            loaded.id = nextIdForType(type);

        if (loaded.id <= 0)
            loaded.id = nextIdForType(type);

        if (type == "Box")
            loaded.taskTiming = qMax(0, object["task_timing"].toInt(0));
        else if (type == "Station")
            loaded.amrCount = qMax(0, object["amr_count"].toInt(0));

        m_objects.append(loaded);
    }

    updateNextIds();

    update();
}

int Grid::nextIdForType(const QString& type)
{
    if (type == "Box")
        return m_nextBoxId++;

    if (type == "Station")
        return m_nextStationId++;

    if (type == "AMR")
        return m_nextAmrId++;

    if (type == "Node")
        return m_nextNodeId++;

    return 1;
}

void Grid::updateNextIds()
{
    m_nextBoxId = 1;
    m_nextStationId = 1;
    m_nextAmrId = 1;
    m_nextNodeId = 1;

    for (const Object& object : m_objects) {

        if (object.type == "Box") {
            m_nextBoxId =
                qMax(m_nextBoxId, object.id + 1);
        }
        else if (object.type == "Station") {
            m_nextStationId =
                qMax(m_nextStationId, object.id + 1);
        }
        else if (object.type == "AMR") {
            m_nextAmrId =
                qMax(m_nextAmrId, object.id + 1);
        }
        else if (object.type == "Node") {
            m_nextNodeId =
                qMax(m_nextNodeId, object.id + 1);
        }
    }
}

QString Grid::objectAt(int x, int y) const
{
    for (const Object& object : m_objects) {
        if (object.x == x && object.y == y)
            return object.type;
    }

    return {};
}

Grid::Object* Grid::objectAtMutable(int x, int y)
{
    for (Object& object : m_objects) {
        if (object.x == x && object.y == y)
            return &object;
    }

    return nullptr;
}

void Grid::addObject(const QString& type, int x, int y)
{
    if (x < 0 || y < 0 || x >= m_width || y >= m_height)
        return;

    for (Object& object : m_objects) {
        if (object.x == x && object.y == y) {
            object.type = type;
            object.id = nextIdForType(type);
            object.taskTiming = 0;
            object.amrCount = 0;
            update();
            return;
        }
    }

    Object object;
    object.type = type;
    object.id = nextIdForType(type);
    object.x = x;
    object.y = y;

    m_objects.append(object);
    update();
}

void Grid::eraseObject(int x, int y)
{
    for (int i = 0; i < m_objects.size(); ++i) {
        if (m_objects[i].x == x && m_objects[i].y == y) {
            const QString type = m_objects[i].type;
            m_objects.removeAt(i);
            emit debugMessage(QString("Erased %1 at (%2, %3)").arg(type).arg(x).arg(y));
            update();
            return;
        }
    }

    emit debugMessage(QString("Nothing to erase at (%1, %2)").arg(x).arg(y));
}

void Grid::configureObject(Object& object)
{
    if (object.type == "Box") {
        bool ok = false;

        const int timing = QInputDialog::getInt(
            this,
            "Box Task Timing",
            "Task timing (seconds):",
            object.taskTiming,
            0,
            86400,
            1,
            &ok
        );

        if (!ok)
            return;

        object.taskTiming = timing;

        emit debugMessage(
            QString("Box at (%1, %2) task timing set to %3 seconds")
                .arg(object.x)
                .arg(object.y)
                .arg(object.taskTiming)
        );
    }
    else if (object.type == "Station") {
        bool ok = false;

        const int count = QInputDialog::getInt(
            this,
            "Station AMR Count",
            "AMRs assigned to this station:",
            object.amrCount,
            0,
            10000,
            1,
            &ok
        );

        if (!ok)
            return;

        object.amrCount = count;

        emit debugMessage(
            QString("Station at (%1, %2) assigned %3 AMRs")
                .arg(object.x)
                .arg(object.y)
                .arg(object.amrCount)
        );
    }
}

QString Grid::imagePathForType(const QString& type) const
{
    if (type == "Node")
        return ":/images/node.svg";
    if (type == "Box")
        return ":/images/box.svg";
    if (type == "Station")
        return ":/images/station.svg";
    if (type == "AMR")
        return ":/images/amr.svg";

    return {};
}

void Grid::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.fillRect(rect(), QColor("#202020"));

    const double cellWidth = static_cast<double>(width()) / m_width;
    const double cellHeight = static_cast<double>(height()) / m_height;

    painter.setPen(QPen(QColor("#505050"), 1));

    for (int x = 0; x <= m_width; ++x) {
        const int px = qRound(x * cellWidth);
        painter.drawLine(px, 0, px, height());
    }

    for (int y = 0; y <= m_height; ++y) {
        const int py = qRound(y * cellHeight);
        painter.drawLine(0, py, width(), py);
    }

    for (const Object& object : m_objects) {
        const QRectF cell(
            object.x * cellWidth,
            object.y * cellHeight,
            cellWidth,
            cellHeight
        );

        const QString imagePath = imagePathForType(object.type);
        if (imagePath.isEmpty())
            continue;

        QPixmap pixmap(imagePath);
        if (pixmap.isNull())
            continue;

        const int padding = qMax(2, qMin(6, static_cast<int>(qMin(cellWidth, cellHeight) * 0.12)));
        const QRect target = cell.toRect().adjusted(
            padding,
            padding,
            -padding,
            -padding
        );

        painter.drawPixmap(
            target,
            pixmap.scaled(
                target.size(),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            )
        );
    }
}

void Grid::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasText())
        event->acceptProposedAction();
    else
        event->ignore();
}

void Grid::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasText()) {
        event->ignore();
        return;
    }

    const QString type = event->mimeData()->text();
    const QPoint position = event->position().toPoint();

    const int x = qBound(
        0,
        static_cast<int>(position.x() * m_width / static_cast<double>(width())),
        m_width - 1
    );

    const int y = qBound(
        0,
        static_cast<int>(position.y() * m_height / static_cast<double>(height())),
        m_height - 1
    );

    if (type == "Erase") {
        eraseObject(x, y);
        emit objectSelected("Empty", 0, x, y, "Cell is empty.");
        event->acceptProposedAction();
        return;
    }

    addObject(type, x, y);

    Object* object = objectAtMutable(x, y);
    if (object && (type == "Box" || type == "Station"))
        configureObject(*object);

    QString metadata;
    if (object) {
        if (object->type == "Box")
            metadata = QString("Task timing: %1 seconds").arg(object->taskTiming);
        else if (object->type == "Station")
            metadata = QString("AMRs assigned: %1").arg(object->amrCount);
        else if (object->type == "AMR")
            metadata = "AMR outside stations.";
        else if (object->type == "Node")
            metadata = "Network coordination node.";
    }

    emit objectSelected(type, object ? object->id : 0, x, y, metadata);
    emit debugMessage(QString("Placed %1 at (%2, %3)").arg(type).arg(x).arg(y));

    event->acceptProposedAction();
}

void Grid::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    const QPoint position = event->position().toPoint();

    const int x = qBound(
        0,
        static_cast<int>(position.x() * m_width / static_cast<double>(width())),
        m_width - 1
    );

    const int y = qBound(
        0,
        static_cast<int>(position.y() * m_height / static_cast<double>(height())),
        m_height - 1
    );

    if (m_selectedType == "Erase") {
        eraseObject(x, y);
        emit objectSelected("Empty", 0, x, y, "Cell is empty.");
        return;
    }

    Object* object = objectAtMutable(x, y);

    if (!object) {
        emit objectSelected("Empty", 0, x, y, "Cell is empty.");
        return;
    }

    configureObject(*object);

    QString metadata;
    if (object->type == "Box")
        metadata = QString("Task timing: %1 seconds").arg(object->taskTiming);
    else if (object->type == "Station")
        metadata = QString("AMRs assigned: %1").arg(object->amrCount);
    else if (object->type == "AMR")
        metadata = "AMR outside stations.";
    else if (object->type == "Node")
        metadata = "Network coordination node.";

    emit objectSelected(object->type, object->id, object->x, object->y, metadata);
    update();
}

int Grid::minimumNodeRange() const
{
    const int cellCount = m_width * m_height;
    if (cellCount <= 0)
        return -1;

    QVector<int> distance(cellCount, -1);
    QQueue<int> queue;

    for (const Object& object : m_objects) {
        if (object.type != "Node")
            continue;

        const int index = object.y * m_width + object.x;
        if (distance[index] == 0)
            continue;

        distance[index] = 0;
        queue.enqueue(index);
    }

    if (queue.isEmpty())
        return -1;

    int maximumDistance = 0;

    static const int dx[] = { 1, -1, 0, 0 };
    static const int dy[] = { 0, 0, 1, -1 };

    while (!queue.isEmpty()) {
        const int current = queue.dequeue();
        const int x = current % m_width;
        const int y = current / m_width;

        for (int direction = 0; direction < 4; ++direction) {
            const int nextX = x + dx[direction];
            const int nextY = y + dy[direction];

            if (nextX < 0 || nextX >= m_width ||
                nextY < 0 || nextY >= m_height)
                continue;

            const int next = nextY * m_width + nextX;

            if (distance[next] != -1)
                continue;

            distance[next] = distance[current] + 1;
            maximumDistance = qMax(maximumDistance, distance[next]);
            queue.enqueue(next);
        }
    }

    for (int value : distance) {
        if (value == -1)
            return -1;
    }

    return maximumDistance;
}

int Grid::amrsOutsideStations() const
{
    int count = 0;

    for (const Object& object : m_objects) {
        if (object.type == "AMR")
            ++count;
    }

    return count;
}

int Grid::stationAmrCount() const
{
    int count = 0;

    for (const Object& object : m_objects) {
        if (object.type == "Station")
            count += object.amrCount;
    }

    return count;
}

int Grid::totalAmrCount() const
{
    return amrsOutsideStations() + stationAmrCount();
}

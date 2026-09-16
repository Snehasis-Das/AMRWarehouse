#include "palette.h"

#include "flow_layout.h"

#include <QApplication>
#include <QDrag>
#include <QIcon>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>

namespace {

class PaletteButton : public QPushButton
{
public:
    PaletteButton(const QString& type, const QString& iconPath, QWidget* parent = nullptr)
        : QPushButton(parent), m_type(type)
    {
        setText(type);
        if (!iconPath.isEmpty()) {
            setIcon(QIcon(iconPath));
            setIconSize(QSize(24, 24));
        }
        setMinimumHeight(44);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::OpenHandCursor);
        setStyleSheet(
            "QPushButton {"
            " background: #292929; color: white;"
            " border: 1px solid #444; border-radius: 6px;"
            " padding: 5px 8px; text-align: left;"
            "}"
            "QPushButton:hover { background: #353535; }"
            "QPushButton:pressed { background: #1f1f1f; }"
        );
    }

protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!(event->buttons() & Qt::LeftButton)) {
            QPushButton::mouseMoveEvent(event);
            return;
        }

        if (m_type == "Erase") {
            QPushButton::mouseMoveEvent(event);
            return;
        }

        auto* mime = new QMimeData;
        mime->setText(m_type);

        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        if (!icon().isNull())
            drag->setPixmap(icon().pixmap(28, 28));
        drag->exec(Qt::CopyAction);
    }

private:
    QString m_type;
};

} 

Palette::Palette(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new FlowLayout(this, 8, 8, 8);

    auto* node = new PaletteButton("Node", ":/icons/node.svg", this);
    auto* box = new PaletteButton("Box", ":/icons/box.svg", this);
    auto* station = new PaletteButton("Station", ":/icons/station.svg", this);
    auto* amr = new PaletteButton("AMR", ":/icons/amr.svg", this);
    auto* erase = new PaletteButton("Erase", QString(), this);

    layout->addWidget(node);
    layout->addWidget(box);
    layout->addWidget(station);
    layout->addWidget(amr);
    layout->addWidget(erase);

    connect(node, &QPushButton::clicked, this, [this]() { emit objectSelected("Node"); });
    connect(box, &QPushButton::clicked, this, [this]() { emit objectSelected("Box"); });
    connect(station, &QPushButton::clicked, this, [this]() { emit objectSelected("Station"); });
    connect(amr, &QPushButton::clicked, this, [this]() { emit objectSelected("AMR"); });
    connect(erase, &QPushButton::clicked, this, [this]() { emit objectSelected("Erase"); });

    setStyleSheet("QWidget { background: #242424; }");
}

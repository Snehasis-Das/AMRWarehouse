#include "infobox.h"

#include "grid.h"

#include <QFormLayout>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

InfoBox::InfoBox(QWidget* parent)
    : QWidget(parent)
{
    m_image = new QLabel(this);
    m_image->setFixedSize(120, 120);
    m_image->setAlignment(Qt::AlignCenter);

    m_type = new QLabel("Object: None", this);
    m_id = new QLabel("ID: -", this);
    m_position = new QLabel("Position: -", this);
    m_metadata = new QLabel("Select or place an object on the grid.", this);
    m_metadata->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    layout->addWidget(m_image, 0, Qt::AlignHCenter);
    layout->addWidget(m_type);
    layout->addWidget(m_id);
    layout->addWidget(m_position);
    layout->addWidget(m_metadata);
    layout->addStretch();

    setStyleSheet(
        "QWidget { background: #242424; color: white; }"
        "QLabel { color: white; }"
    );
}

void InfoBox::showObject(const QString& type, int id, int x, int y, const QString& objectMetadata)
{
    m_type->setText("Object: " + type);

    if (type == "Empty")
        m_id->setText("ID: -");
    else
        m_id->setText(QString("ID: %1").arg(id));
    
    m_position->setText(QString("Position: (%1, %2)").arg(x).arg(y));

    QString imagePath;
    QString metadata;

    if (type == "Node") {
        imagePath = ":/images/node.svg";
        metadata = "Network coordination node\nMinimum range is calculated when the warehouse is saved.";
    } else if (type == "Box") {
        imagePath = ":/images/box.svg";
        metadata = "Warehouse box\nTask timing can be configured when the box is clicked.";
    } else if (type == "Station") {
        imagePath = ":/images/station.svg";
        metadata = "AMR station / deployment point\nAMR allocation can be configured when the station is clicked.";
    } else if (type == "AMR") {
        imagePath = ":/images/amr.svg";
        metadata = "Autonomous Mobile Robot\nThis AMR is outside a station and counts toward the total fleet.";
    } else {
        imagePath.clear();
        metadata = "Empty warehouse cell";
    }

    if (!imagePath.isEmpty()) {
        m_image->setPixmap(QPixmap(imagePath).scaled(
            m_image->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        ));
    } else {
        m_image->clear();
    }

    if (!objectMetadata.isEmpty())
        metadata += "\n\n" + objectMetadata;

    m_metadata->setText(metadata);
}

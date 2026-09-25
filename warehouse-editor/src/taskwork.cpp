#include "taskwork.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QString subtaskToString(Subtask subtask)
{
    switch (subtask) {
    case Subtask::TakeAndPut:
        return "TakeAndPut";

    case Subtask::Operate:
        return "Operate";

    case Subtask::Goto:
        return "Goto";
    }

    return "TakeAndPut";
}

bool subtaskFromString(const QString& value, Subtask& subtask)
{
    if (value == "TakeAndPut") {
        subtask = Subtask::TakeAndPut;
        return true;
    }

    if (value == "Operate") {
        subtask = Subtask::Operate;
        return true;
    }

    if (value == "Goto") {
        subtask = Subtask::Goto;
        return true;
    }

    return false;
}


/*
 * Dialog used only by TaskWork.
 *
 * The visible metadata fields change according to the selected
 * subtask type.
 */
class TaskDialog : public QDialog
{
public:
    explicit TaskDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Add Task");
        setModal(true);

        m_count = new QSpinBox(this);
        m_count->setRange(1, 1000000);
        m_count->setValue(1);

        m_subtask = new QComboBox(this);
        m_subtask->addItem("TakeAndPut");
        m_subtask->addItem("Operate");
        m_subtask->addItem("Goto");

        m_aLabel = new QLabel(this);
        m_bLabel = new QLabel(this);

        m_a = new QSpinBox(this);
        m_b = new QSpinBox(this);

        m_a->setRange(-2147483647, 2147483647);
        m_b->setRange(-2147483647, 2147483647);

        auto* form = new QFormLayout;
        form->setContentsMargins(14, 14, 14, 14);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(10);

        form->addRow("Count:", m_count);
        form->addRow("Subtask:", m_subtask);
        form->addRow(m_aLabel, m_a);
        form->addRow(m_bLabel, m_b);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Cancel | QDialogButtonBox::Ok,
            this
        );

        connect(
            buttons,
            &QDialogButtonBox::accepted,
            this,
            &QDialog::accept
        );

        connect(
            buttons,
            &QDialogButtonBox::rejected,
            this,
            &QDialog::reject
        );

        connect(
            m_subtask,
            &QComboBox::currentIndexChanged,
            this,
            [this](int) {
                updateMetadataFields();
            }
        );

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        layout->addLayout(form);
        layout->addWidget(buttons);

        /*
         * Same visual language as the existing editor:
         * dark background, #292929 controls, #444 borders,
         * 6px rounded corners.
         */
        setStyleSheet(
            "QDialog { background: #242424; color: white; }"

            "QLabel { color: white; }"

            "QSpinBox, QComboBox {"
            " background: #292929;"
            " color: white;"
            " border: 1px solid #444;"
            " border-radius: 6px;"
            " padding: 4px 6px;"
            " min-height: 28px;"
            "}"

            "QSpinBox:focus, QComboBox:focus {"
            " border: 1px solid #666;"
            "}"

            "QComboBox QAbstractItemView {"
            " background: #292929;"
            " color: white;"
            " border: 1px solid #444;"
            " selection-background-color: #353535;"
            "}"

            "QPushButton {"
            " background: #292929;"
            " color: white;"
            " border: 1px solid #444;"
            " border-radius: 6px;"
            " padding: 5px 12px;"
            " min-height: 30px;"
            "}"

            "QPushButton:hover {"
            " background: #353535;"
            "}"

            "QPushButton:pressed {"
            " background: #1f1f1f;"
            "}"
        );

        updateMetadataFields();
    }

    Task task(int id) const
    {
        Task result;

        result.id = id;
        result.count = m_count->value();

        result.subtask =
            static_cast<Subtask>(m_subtask->currentIndex());

        result.a = m_a->value();

        /*
         * Operate has no second metadata value.
         * The representation is explicitly b = -1.
         */
        result.b =
            result.subtask == Subtask::Operate
                ? -1
                : m_b->value();

        return result;
    }

private:
    void updateMetadataFields()
    {
        const Subtask subtask =
            static_cast<Subtask>(m_subtask->currentIndex());

        if (subtask == Subtask::TakeAndPut) {

            m_aLabel->setText("Take from Box ID:");
            m_bLabel->setText("Put into Box ID:");

            m_aLabel->show();
            m_a->show();

            m_bLabel->show();
            m_b->show();
        }
        else if (subtask == Subtask::Operate) {

            m_aLabel->setText("Target ID:");

            m_aLabel->show();
            m_a->show();

            /*
             * b is invisible because it is meaningless for Operate.
             */
            m_bLabel->hide();
            m_b->hide();
        }
        else {

            m_aLabel->setText("X:");
            m_bLabel->setText("Y:");

            m_aLabel->show();
            m_a->show();

            m_bLabel->show();
            m_b->show();
        }
    }

    QSpinBox* m_count = nullptr;
    QComboBox* m_subtask = nullptr;

    QLabel* m_aLabel = nullptr;
    QLabel* m_bLabel = nullptr;

    QSpinBox* m_a = nullptr;
    QSpinBox* m_b = nullptr;
};


QString taskMetadataText(const Task& task)
{
    switch (task.subtask) {

    case Subtask::TakeAndPut:
        return QString("Box %1  →  Box %2")
            .arg(task.a)
            .arg(task.b);

    case Subtask::Operate:
        return QString("Target ID: %1")
            .arg(task.a);

    case Subtask::Goto:
        return QString("Location: (%1, %2)")
            .arg(task.a)
            .arg(task.b);
    }

    return {};
}


QString taskSummaryText(const Task& task)
{
    return QString("%1  ×  %2")
        .arg(subtaskToString(task.subtask))
        .arg(task.count);
}

} // namespace


TaskWork::TaskWork(QWidget* parent)
    : QWidget(parent)
{
    auto* title = new QLabel("Tasks", this);

    title->setStyleSheet(
        "QLabel {"
        " color: white;"
        " font-weight: 600;"
        " padding: 0 2px;"
        "}"
    );


    /*
     * Scrollable task area.
     *
     * Only this portion scrolls when there are many tasks.
     * The Add Task button stays visible.
     */
    m_scrollArea = new QScrollArea(this);

    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_scrollArea->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff
    );

    m_scrollArea->setVerticalScrollBarPolicy(
        Qt::ScrollBarAsNeeded
    );

    m_scrollArea->setStyleSheet(
        "QScrollArea {"
        " background: #242424;"
        " border: 0px;"
        "}"

        "QScrollBar:vertical {"
        " background: #242424;"
        " width: 10px;"
        " margin: 0px;"
        "}"

        "QScrollBar::handle:vertical {"
        " background: #444;"
        " min-height: 24px;"
        " border-radius: 4px;"
        "}"

        "QScrollBar::handle:vertical:hover {"
        " background: #555;"
        "}"

        "QScrollBar::add-line:vertical,"
        "QScrollBar::sub-line:vertical {"
        " height: 0px;"
        "}"
    );


    auto* content = new QWidget;

    content->setStyleSheet(
        "QWidget {"
        " background: #242424;"
        "}"
    );

    m_listLayout = new QVBoxLayout(content);

    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(8);

    m_scrollArea->setWidget(content);


    /*
     * Add Task belongs to TaskWork, NOT Palette.
     */
    m_addButton = new QPushButton("+ Add Task", this);

    m_addButton->setMinimumHeight(44);

    m_addButton->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Fixed
    );

    m_addButton->setCursor(Qt::PointingHandCursor);

    m_addButton->setStyleSheet(
        "QPushButton {"
        " background: #292929;"
        " color: white;"
        " border: 1px solid #444;"
        " border-radius: 6px;"
        " padding: 5px 8px;"
        " text-align: left;"
        "}"

        "QPushButton:hover {"
        " background: #353535;"
        "}"

        "QPushButton:pressed {"
        " background: #1f1f1f;"
        "}"
    );


    auto* layout = new QVBoxLayout(this);

    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    layout->addWidget(title);
    layout->addWidget(m_scrollArea, 1);
    layout->addWidget(m_addButton);


    setStyleSheet(
        "QWidget {"
        " background: #242424;"
        " color: white;"
        "}"
    );


    connect(
        m_addButton,
        &QPushButton::clicked,
        this,
        &TaskWork::addTask
    );

    rebuildList();
}


QJsonArray TaskWork::toJson() const
{
    QJsonArray tasks;

    for (const Task& task : m_tasks) {

        QJsonObject value;

        value["id"] = task.id;
        value["count"] = task.count;
        value["subtask"] = subtaskToString(task.subtask);
        value["a"] = task.a;
        value["b"] = task.b;

        tasks.append(value);
    }

    return tasks;
}


void TaskWork::fromJson(const QJsonArray& tasks)
{
    m_tasks.clear();
    m_nextTaskId = 1;

    int maximumId = 0;

    for (const QJsonValue& value : tasks) {

        if (!value.isObject())
            continue;

        const QJsonObject object = value.toObject();

        const int id =
            object["id"].toInt(-1);

        const int count =
            object["count"].toInt(-1);

        const QString subtaskText =
            object["subtask"].toString();

        if (id < 1 || count < 1)
            continue;

        Subtask subtask;

        if (!subtaskFromString(subtaskText, subtask))
            continue;

        Task task;

        task.id = id;
        task.count = count;
        task.subtask = subtask;
        task.a = object["a"].toInt(0);

        /*
         * Operate always normalizes b to -1.
         */
        task.b =
            subtask == Subtask::Operate
                ? -1
                : object["b"].toInt(0);

        m_tasks.append(task);

        maximumId = qMax(maximumId, id);
    }

    /*
     * The next generated ID is always above the highest
     * existing ID in the loaded file.
     */
    m_nextTaskId = maximumId + 1;

    rebuildList();
}


void TaskWork::clearTasks()
{
    m_tasks.clear();
    m_nextTaskId = 1;

    rebuildList();
}


void TaskWork::addTask()
{
    TaskDialog dialog(this);

    if (dialog.exec() != QDialog::Accepted)
        return;

    /*
     * Count comes directly from user input.
     * There is no expansion/compression of subtasks.
     */
    const Task task =
        dialog.task(m_nextTaskId);

    m_tasks.append(task);
    ++m_nextTaskId;

    rebuildList();

    emit debugMessage(
        QString("Added Task %1: %2 x %3")
            .arg(task.id)
            .arg(subtaskToString(task.subtask))
            .arg(task.count)
    );
}


void TaskWork::removeTask(int taskId)
{
    for (int i = 0; i < m_tasks.size(); ++i) {

        if (m_tasks[i].id != taskId)
            continue;

        m_tasks.removeAt(i);

        rebuildList();

        emit debugMessage(
            QString("Removed Task %1").arg(taskId)
        );

        return;
    }
}


void TaskWork::rebuildList()
{
    /*
     * Completely rebuild only the TaskWork list.
     *
     * Nothing in Palette, Grid, Debug or InfoBox is touched.
     */
    while (QLayoutItem* item = m_listLayout->takeAt(0)) {

        if (QWidget* widget = item->widget())
            delete widget;

        delete item;
    }


    if (m_tasks.isEmpty()) {

        auto* empty = new QLabel("No tasks.");

        empty->setAlignment(Qt::AlignCenter);

        empty->setStyleSheet(
            "QLabel {"
            " color: #888;"
            " padding: 12px;"
            "}"
        );

        m_listLayout->addWidget(empty);
        m_listLayout->addStretch();

        return;
    }


    for (const Task& task : m_tasks) {

        auto* row = new QFrame;

        row->setStyleSheet(
            "QFrame {"
            " background: #292929;"
            " color: white;"
            " border: 1px solid #444;"
            " border-radius: 6px;"
            "}"

            "QLabel {"
            " border: 0px;"
            " color: white;"
            "}"

            "QPushButton {"
            " background: transparent;"
            " color: #bbbbbb;"
            " border: 0px;"
            " border-radius: 4px;"
            " font-size: 16px;"
            "}"

            "QPushButton:hover {"
            " background: #353535;"
            " color: white;"
            "}"

            "QPushButton:pressed {"
            " background: #1f1f1f;"
            "}"
        );


        auto* rowLayout = new QGridLayout(row);

        rowLayout->setContentsMargins(8, 6, 6, 8);
        rowLayout->setHorizontalSpacing(6);
        rowLayout->setVerticalSpacing(4);


        auto* title =
            new QLabel(
                QString("Task %1").arg(task.id),
                row
            );

        title->setStyleSheet(
            "QLabel {"
            " font-weight: 600;"
            "}"
        );


        auto* removeButton =
            new QPushButton("×", row);

        removeButton->setFixedSize(28, 28);
        removeButton->setCursor(Qt::PointingHandCursor);


        auto* summary =
            new QLabel(
                taskSummaryText(task),
                row
            );

        summary->setStyleSheet(
            "QLabel {"
            " color: #dddddd;"
            "}"
        );


        auto* metadata =
            new QLabel(
                taskMetadataText(task),
                row
            );

        metadata->setStyleSheet(
            "QLabel {"
            " color: #aaaaaa;"
            "}"
        );

        metadata->setWordWrap(true);


        rowLayout->addWidget(
            title,
            0,
            0
        );

        rowLayout->addWidget(
            removeButton,
            0,
            1,
            Qt::AlignRight | Qt::AlignTop
        );

        rowLayout->addWidget(
            summary,
            1,
            0,
            1,
            2
        );

        rowLayout->addWidget(
            metadata,
            2,
            0,
            1,
            2
        );

        rowLayout->setColumnStretch(0, 1);


        connect(
            removeButton,
            &QPushButton::clicked,
            this,
            [this, id = task.id]() {
                removeTask(id);
            }
        );


        m_listLayout->addWidget(row);
    }

    m_listLayout->addStretch();
}
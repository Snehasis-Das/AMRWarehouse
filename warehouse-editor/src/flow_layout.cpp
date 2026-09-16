#include "flow_layout.h"

#include <QStyle>
#include <QWidget>

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent),
      m_hSpacing(hSpacing),
      m_vSpacing(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    QLayoutItem* item;
    while ((item = takeAt(0)) != nullptr)
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return index >= 0 && index < m_items.size() ? m_items.at(index) : nullptr;
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= m_items.size())
        return nullptr;

    return m_items.takeAt(index);
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return Qt::Horizontal | Qt::Vertical;
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;

    for (const QLayoutItem* item : m_items)
        size = size.expandedTo(item->minimumSize());

    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());

    return size;
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    const QMargins margins = contentsMargins();
    const QRect effective = rect.adjusted(
        margins.left(), margins.top(),
        -margins.right(), -margins.bottom()
    );

    if (m_items.isEmpty())
        return margins.top() + margins.bottom();

    int y = effective.y();
    int rowHeight = 0;
    int rowWidth = 0;
    QList<QLayoutItem*> row;

    auto flushRow = [&]() {
        if (row.isEmpty())
            return;

        const int spacing = m_hSpacing;
        const int totalSpacing = spacing * (row.size() - 1);
        const int available = qMax(0, effective.width() - totalSpacing);
        const int buttonWidth = qMax(1, available / row.size());
        int x = effective.x();

        for (int i = 0; i < row.size(); ++i) {
            QLayoutItem* item = row.at(i);
            const int width = (i == row.size() - 1)
                ? effective.right() - x + 1
                : buttonWidth;

            if (!testOnly)
                item->setGeometry(QRect(x, y, width, rowHeight));

            x += width + spacing;
        }

        y += rowHeight + m_vSpacing;
        row.clear();
        rowHeight = 0;
        rowWidth = 0;
    };

    for (QLayoutItem* item : m_items) {
        const QSize hint = item->sizeHint();
        const int spacing = row.isEmpty() ? 0 : m_hSpacing;

        if (!row.isEmpty() &&
            rowWidth + spacing + hint.width() > effective.width()) {
            flushRow();
        }

        row.append(item);
        rowHeight = qMax(rowHeight, hint.height());
        rowWidth += spacing + hint.width();
    }

    flushRow();

    return y - effective.y() + margins.top() + margins.bottom();
}

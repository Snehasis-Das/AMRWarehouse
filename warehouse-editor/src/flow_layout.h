#pragma once

#include <QLayout>
#include <QStyle>

class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget* parent = nullptr,
                        int margin = 0,
                        int hSpacing = 8,
                        int vSpacing = 8);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;

    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

private:
    QList<QLayoutItem*> m_items;
    int m_hSpacing;
    int m_vSpacing;
};

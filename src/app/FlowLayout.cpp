#include "FlowLayout.h"

#include <QWidget>

#include <algorithm>

FlowLayout::FlowLayout(QWidget* parent, int spacing)
    : QLayout(parent)
{
    setSpacing(spacing);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0)) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items) {
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    arrange(rect, false);
}

int FlowLayout::arrange(const QRect& rect, bool testOnly) const
{
    const QMargins margins = contentsMargins();
    const QRect area = rect.marginsRemoved(margins);
    int x = area.x();
    int y = area.y();
    int rowHeight = 0;
    for (QLayoutItem* item : m_items) {
        if (item->widget() != nullptr && !item->widget()->isVisible() && !testOnly) {
            continue;
        }
        const QSize hint = item->sizeHint();
        if (x + hint.width() > area.right() + 1 && rowHeight > 0) {
            x = area.x();
            y += rowHeight + spacing();
            rowHeight = 0;
        }
        if (!testOnly) {
            item->setGeometry(QRect(QPoint(x, y), hint));
        }
        x += hint.width() + spacing();
        rowHeight = std::max(rowHeight, hint.height());
    }
    return y + rowHeight - rect.y() + margins.bottom();
}

/* Copyright 2013-2021 MiniSkins Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SourceComboBox.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>

namespace
{
    // 行内操作按钮宽度（像素），删除按钮固定在最右侧
    const int kActionWidth = 22;

    void drawEditGlyph(QPainter *painter, const QRect &rect, const QColor &color)
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->translate(rect.center());
        painter->rotate(-45.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        // 笔杆
        painter->drawRect(QRect(-6, -2, 8, 4));
        // 笔尖
        QPolygon tip;
        tip << QPoint(2, -2) << QPoint(6, 0) << QPoint(2, 2);
        painter->drawPolygon(tip);
        painter->restore();
    }

    void drawDeleteGlyph(QPainter *painter, const QRect &rect, const QColor &color)
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(color, 2.0));
        const QPoint center = rect.center();
        const int half = 4;
        painter->drawLine(center.x() - half, center.y() - half, center.x() + half, center.y() + half);
        painter->drawLine(center.x() - half, center.y() + half, center.x() + half, center.y() - half);
        painter->restore();
    }

    class SourceItemDelegate : public QStyledItemDelegate
    {
    public:
        explicit SourceItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
        {
            QStyleOptionViewItem opt = option;
            initStyleOption(&opt, index);

            const bool deletable = index.data(SourceComboBox::DeletableRole).toBool();
            const bool editable = index.data(SourceComboBox::EditableRole).toBool();
            const int reserved = (deletable ? kActionWidth : 0) + (editable ? kActionWidth : 0);

            if (reserved > 0 && !opt.text.isEmpty())
            {
                // 预留按钮区域，过长名称按右侧省略，避免压住按钮
                QFontMetrics metrics(opt.font);
                opt.text = metrics.elidedText(opt.text, Qt::ElideRight, opt.rect.width() - reserved - 8);
            }

            QStyledItemDelegate::paint(painter, opt, index);

            if (reserved <= 0)
            {
                return;
            }

            const bool selected = (option.state & QStyle::State_Selected) != 0;
            const QColor editColor = selected ? opt.palette.color(QPalette::HighlightedText)
                                              : opt.palette.color(QPalette::Text);
            const QColor deleteColor(200, 32, 32);

            QRect actionRect(option.rect.right() - reserved + 1, option.rect.top(), kActionWidth, option.rect.height());
            painter->save();
            if (editable)
            {
                drawEditGlyph(painter, actionRect, editColor);
                actionRect.translate(kActionWidth, 0);
            }
            if (deletable)
            {
                drawDeleteGlyph(painter, actionRect, deleteColor);
            }
            painter->restore();
        }
    };
}

SourceComboBox::SourceComboBox(QWidget *parent) : QComboBox(parent)
{
    // 行内操作按钮由本控件自行绘制与响应点击
    setItemDelegate(new SourceItemDelegate(this));
    view()->setMouseTracking(true);
    view()->viewport()->installEventFilter(this);
}

void SourceComboBox::setItemActions(int index, bool deletable, bool editable)
{
    setItemData(index, deletable, DeletableRole);
    setItemData(index, editable, EditableRole);
}

bool SourceComboBox::eventFilter(QObject *watched, QEvent *event)
{
    // 拦截弹出列表中的鼠标按下事件，命中行内按钮时不再改变当前选中项
    if (event->type() == QEvent::MouseButtonPress && watched == view()->viewport())
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            QModelIndex index = view()->indexAt(mouseEvent->pos());
            if (index.isValid())
            {
                const int right = view()->visualRect(index).right();
                const int x = mouseEvent->pos().x();
                if (index.data(DeletableRole).toBool() && x > right - kActionWidth)
                {
                    hidePopup();
                    emit deleteRequested(index.row());
                    return true;
                }
                if (index.data(EditableRole).toBool() && x > right - 2 * kActionWidth && x <= right - kActionWidth)
                {
                    hidePopup();
                    emit editRequested(index.row());
                    return true;
                }
            }
        }
    }
    return QComboBox::eventFilter(watched, event);
}

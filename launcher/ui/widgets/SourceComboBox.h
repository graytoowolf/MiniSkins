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

#pragma once

#include <QComboBox>

/*!
 * 下载源下拉框：自定义源条目右侧会绘制行内操作按钮（编辑与红色叉形删除），
 * 内置源条目不显示任何操作按钮。
 */
class SourceComboBox : public QComboBox
{
    Q_OBJECT

public:
    enum ItemRole
    {
        DeletableRole = Qt::UserRole + 1,
        EditableRole = Qt::UserRole + 2
    };

    explicit SourceComboBox(QWidget *parent = 0);

    // 为指定条目启用/禁用行内操作按钮
    void setItemActions(int index, bool deletable, bool editable);

signals:
    void deleteRequested(int index);
    void editRequested(int index);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

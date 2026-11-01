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

#include "JavaMatchSelectDialog.h"

#include <QDialogButtonBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

JavaMatchSelectDialog::JavaMatchSelectDialog(const QList<JavaInstallPtr> &candidates, QWidget *parent)
    : QDialog(parent), m_candidates(candidates)
{
    setWindowTitle(tr("Select a Java installation"));
    setModal(true);
    resize(760, 360);

    auto layout = new QVBoxLayout(this);

    auto label = new QLabel(tr("Multiple Java installations match this instance's requirements.\nPlease select one to use for launching:"), this);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels(QStringList() << tr("Install Path") << tr("Version") << tr("Vendor") << tr("Architecture"));
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setAlternatingRowColors(true);
    for (int i = 0; i < m_candidates.size(); ++i)
    {
        auto install = m_candidates.at(i);
        auto item = new QTreeWidgetItem(m_tree);
        item->setText(0, install->path);
        item->setText(1, install->id.toString());
        item->setText(2, install->vendor.isEmpty() ? QStringLiteral("-") : install->vendor);
        item->setText(3, install->arch.serialize());
        item->setData(0, Qt::UserRole, i);
    }
    m_tree->resizeColumnToContents(0);
    m_tree->resizeColumnToContents(1);
    m_tree->resizeColumnToContents(2);
    m_tree->resizeColumnToContents(3);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &JavaMatchSelectDialog::onSelectionChanged);
    layout->addWidget(m_tree);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void JavaMatchSelectDialog::onSelectionChanged()
{
    auto items = m_tree->selectedItems();
    if (items.isEmpty())
    {
        m_selected.reset();
        m_okButton->setEnabled(false);
        return;
    }
    int index = items.first()->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= m_candidates.size())
    {
        m_selected.reset();
        m_okButton->setEnabled(false);
        return;
    }
    m_selected = m_candidates.at(index);
    m_okButton->setEnabled(true);
}

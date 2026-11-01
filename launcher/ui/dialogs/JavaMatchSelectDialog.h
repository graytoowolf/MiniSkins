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

#include <QDialog>

#include "java/JavaInstall.h"

class QTreeWidget;
class QPushButton;

// Modal dialog shown when multiple Java installations match the instance's
// requirements. Shows install path, version, vendor and architecture of every
// candidate and lets the user pick one.
class JavaMatchSelectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit JavaMatchSelectDialog(const QList<JavaInstallPtr> &candidates, QWidget *parent = 0);

    JavaInstallPtr selectedJava() const { return m_selected; }

private slots:
    void onSelectionChanged();

private:
    QList<JavaInstallPtr> m_candidates;
    JavaInstallPtr m_selected;
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_okButton = nullptr;
};

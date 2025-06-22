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

#include "PasteEEPage.h"
#include "ui_PasteEEPage.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTabBar>

#include "settings/SettingsObject.h"
#include "tools/BaseProfiler.h"
#include "Application.h"

PasteEEPage::PasteEEPage(QWidget *parent) : QWidget(parent),
                                            ui(new Ui::PasteEEPage)
{
    ui->setupUi(this);
    ui->tabWidget->tabBar()->hide();
    connect(ui->customAPIkeyEdit, &QLineEdit::textEdited, this, &PasteEEPage::textEdited);
    connect(ui->logPlatformComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &PasteEEPage::platformChanged);
    loadSettings();
    platformChanged(ui->logPlatformComboBox->currentIndex());
}

PasteEEPage::~PasteEEPage()
{
    delete ui;
}
void PasteEEPage::platformChanged(int index)
{
    QString platform = ui->logPlatformComboBox->itemText(index);
    if (platform == "mclo.gs")
    {
        ui->groupBox_2->hide();
    }
    else if (platform == "paste.ee")
    {
        ui->groupBox_2->show();
    }
}

void PasteEEPage::loadSettings()
{
    auto s = APPLICATION->settings();
    QString keyToUse = s->get("PasteEEAPIKey").toString();
    if (keyToUse == "miniskins")
    {
        ui->multimcButton->setChecked(true);
    }
    else
    {
        ui->customButton->setChecked(true);
        ui->customAPIkeyEdit->setText(keyToUse);
    }

    QString currentPlatform = s->get("LogPlatform").toString();
    int index = ui->logPlatformComboBox->findText(currentPlatform);
    if (index != -1)
    {
        ui->logPlatformComboBox->setCurrentIndex(index);
    }
}

void PasteEEPage::applySettings()
{
    auto s = APPLICATION->settings();

    QString pasteKeyToUse;
    if (ui->customButton->isChecked())
        pasteKeyToUse = ui->customAPIkeyEdit->text();
    else
    {
        pasteKeyToUse = "miniskins";
    }
    QString currentPlatform = ui->logPlatformComboBox->currentText();

    s->set("PasteEEAPIKey", pasteKeyToUse);
    s->set("LogPlatform", currentPlatform);
}

bool PasteEEPage::apply()
{
    applySettings();
    return true;
}

void PasteEEPage::textEdited(const QString &text)
{
    ui->customButton->setChecked(true);
}

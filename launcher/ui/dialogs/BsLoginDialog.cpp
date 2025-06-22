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

#include "BsLoginDialog.h"
#include "ui_BsLoginDialog.h"
#include "Application.h"
#include "ui/dialogs/customyggdrasil.h"

#include "minecraft/auth/AccountTask.h"

#include <QtWidgets/QPushButton>
#include <DesktopServices.h>

BsLoginDialog::BsLoginDialog(QWidget *parent) : QDialog(parent), ui(new Ui::BsLoginDialog), isDialogOpen(false)
{
    ui->setupUi(this);
    ui->progressBar->setVisible(false);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    updateYggSources();

    connect(ui->yggurlcomboBox, &QComboBox::currentTextChanged, this, &BsLoginDialog::onComboBoxCurrentTextChanged);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void BsLoginDialog::onComboBoxCurrentTextChanged(const QString &text)
{
    if (ui->yggurlcomboBox->currentIndex() == ui->yggurlcomboBox->count() - 1 && !isDialogOpen)
    {
        isDialogOpen = true;
        customyggdrasil *customDialog = new customyggdrasil(this);
        customDialog->exec(); // 显示对话框（模态）
        updateYggSources();
        // 使用 QTimer 延迟设置索引
        int index = ui->yggurlcomboBox->count() - 2;
        ui->yggurlcomboBox->setCurrentIndex(index); // 设置索引
        isDialogOpen = false;
    }
}

void BsLoginDialog::updateYggSources()
{
    // 获取 YggSources
    const auto &yggSources = APPLICATION->getYggSources();

    // 清空之前的项目
    ui->yggurlcomboBox->clear();

    // 遍历 yggSources，动态添加到 comboBox 中
    QSet<QString> addedUrls;
    for (const YggSource &source : yggSources)
    {
        const QString &url = source.getUrl();
        if (!addedUrls.contains(url))
        {
            ui->yggurlcomboBox->addItem(source.getName(), url);
            addedUrls.insert(url);
        }
    }

    ui->yggurlcomboBox->addItem(tr("Custom Yggdrasil API"));
}

BsLoginDialog::~BsLoginDialog()
{
    delete ui;
}

// Stage 1: User interaction
void BsLoginDialog::accept()
{
    setUserInputsEnabled(false);
    ui->progressBar->setVisible(true);

    // Setup the login task and start it
    m_account = MinecraftAccount::createBlessings(
        ui->userTextBox->text(), ui->yggurlcomboBox->currentData().toString(), ui->yggurlcomboBox->currentText());
    m_loginTask = m_account->bslogin(ui->passTextBox->text());
    connect(m_loginTask.get(), &Task::failed, this, &BsLoginDialog::onTaskFailed);
    connect(m_loginTask.get(), &Task::succeeded, this, &BsLoginDialog::onTaskSucceeded);
    connect(m_loginTask.get(), &Task::status, this, &BsLoginDialog::onTaskStatus);
    connect(m_loginTask.get(), &Task::progress, this, &BsLoginDialog::onTaskProgress);
    m_loginTask->start();
}

void BsLoginDialog::setUserInputsEnabled(bool enable)
{
    ui->userTextBox->setEnabled(enable);
    ui->passTextBox->setEnabled(enable);
    ui->buttonBox->setEnabled(enable);
}

// Enable the OK button only when both textboxes contain something.
void BsLoginDialog::on_userTextBox_textEdited(const QString &newText)
{
    ui->buttonBox->button(QDialogButtonBox::Ok)
        ->setEnabled(!newText.isEmpty() && !ui->passTextBox->text().isEmpty());
}
void BsLoginDialog::on_passTextBox_textEdited(const QString &newText)
{
    ui->buttonBox->button(QDialogButtonBox::Ok)
        ->setEnabled(!newText.isEmpty() && !ui->userTextBox->text().isEmpty());
}

void BsLoginDialog::onTaskFailed(const QString &reason)
{
    // Set message
    auto lines = reason.split('\n');
    QString processed;
    for (auto line : lines)
    {
        if (line.size())
        {
            processed += "<font color='red'>" + line + "</font><br />";
        }
        else
        {
            processed += "<br />";
        }
    }
    ui->label->setText(processed);

    // Re-enable user-interaction
    setUserInputsEnabled(true);
    ui->progressBar->setVisible(false);
}

void BsLoginDialog::onTaskSucceeded()
{
    QDialog::accept();
}

void BsLoginDialog::onTaskStatus(const QString &status)
{
    ui->label->setText(status);
}

void BsLoginDialog::onTaskProgress(qint64 current, qint64 total)
{
    ui->progressBar->setMaximum(total);
    ui->progressBar->setValue(current);
}
// 辅助函数：标准化URL
QString BsLoginDialog::normalizeUrl(const QString &url)
{
    QString normalized = url.trimmed();
    if (normalized.endsWith('/'))
    {
        normalized.chop(1);
    }
    return normalized;
}
MinecraftAccountPtr BsLoginDialog::newAccount(QWidget *parent, QString msg, QString initialAccount /*= ""*/, QString yggUrl /*= ""*/, QString yggName /*= ""*/)
{
    BsLoginDialog dlg(parent);
    dlg.ui->label->setText(msg);
    dlg.ui->userTextBox->setText(initialAccount);

    if (!yggUrl.isEmpty())
    {
        QString normalizedYggUrl = normalizeUrl(yggUrl);

        int foundIndex = -1;
        for (int i = 0; i < dlg.ui->yggurlcomboBox->count(); ++i)
        {
            QString itemData = dlg.ui->yggurlcomboBox->itemData(i).toString();
            QString normalizedItemData = normalizeUrl(itemData);

            if (normalizedItemData == normalizedYggUrl)
            {
                foundIndex = i;
                break;
            }
        }

        if (foundIndex != -1)
        {
            dlg.ui->yggurlcomboBox->setCurrentIndex(foundIndex);
        }
        else
        {
            int customIndex = dlg.ui->yggurlcomboBox->count() - 1;
            QString displayName = yggName.isEmpty() ? yggUrl : yggName;
            dlg.ui->yggurlcomboBox->insertItem(customIndex, displayName, yggUrl);
            dlg.ui->yggurlcomboBox->setCurrentIndex(customIndex);
        }
    }

    if (dlg.exec() == QDialog::Accepted)
    {
        return dlg.m_account;
    }
    return 0;
}

void BsLoginDialog::on_regpushButton_clicked()
{
    QString customUrl = ui->yggurlcomboBox->currentData().toString();
    QUrl url = QUrl::fromUserInput(customUrl);
    url.setPath("/auth/register");
    DesktopServices::openUrl(url);
}

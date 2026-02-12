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

#include "LauncherPage.h"
#include "ui_LauncherPage.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QTextCharFormat>
#include <QTimer>

#include "updater/UpdateChecker.h"

#include "settings/SettingsObject.h"
#include <FileSystem.h>
#include "Application.h"
#include "BuildConfig.h"
#include "ui/themes/ITheme.h"

#include <QApplication>
#include <QProcess>

// FIXME: possibly move elsewhere
enum InstSortMode
{
    // Sort alphabetically by name.
    Sort_Name,
    // Sort by which instance was launched most recently.
    Sort_LastLaunch
};

LauncherPage::LauncherPage(QWidget *parent) : QWidget(parent), ui(new Ui::LauncherPage)
{
    ui->setupUi(this);
    auto origForeground = ui->fontPreview->palette().color(ui->fontPreview->foregroundRole());
    auto origBackground = ui->fontPreview->palette().color(ui->fontPreview->backgroundRole());
    m_colors.reset(new LogColorCache(origForeground, origBackground));

    ui->sortingModeGroup->setId(ui->sortByNameBtn, Sort_Name);
    ui->sortingModeGroup->setId(ui->sortLastLaunchedBtn, Sort_LastLaunch);

    defaultFormat = new QTextCharFormat(ui->fontPreview->currentCharFormat());

    m_languageModel = APPLICATION->translations();

    // 延迟加载设置，等待异步数据加载完成
    QTimer::singleShot(0, this, [this]() {
        this->sources = APPLICATION->getDownloadSources();
        this->loadSettings();
    });

    // Updater
    if (!BuildConfig.UPDATER_ENABLED)
    {
        if (ui->updateSettingsBox)
        {
            ui->updateSettingsBox->setHidden(true);
        }
    }

    connect(ui->fontSizeBox, SIGNAL(valueChanged(int)), SLOT(refreshFontPreview()));
    connect(ui->consoleFont, SIGNAL(currentFontChanged(QFont)), SLOT(refreshFontPreview()));

    // move mac data button
    QFile file(QDir::current().absolutePath() + "/dontmovemacdata");
    if (!file.exists())
    {
        if (ui->migrateDataFolderMacBtn)
        {
            ui->migrateDataFolderMacBtn->setVisible(false);
        }
    }
}

LauncherPage::~LauncherPage()
{
    delete ui;
    delete defaultFormat;
}

bool LauncherPage::apply()
{
    applySettings();
    return true;
}

void LauncherPage::on_instDirBrowseBtn_clicked()
{
    if (!ui->instDirTextBox)
    {
        return;
    }

    QString raw_dir = QFileDialog::getExistingDirectory(this, tr("Instance Folder"), ui->instDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!raw_dir.isEmpty() && QDir(raw_dir).exists())
    {
        QString cooked_dir = FS::NormalizePath(raw_dir);
        if (FS::checkProblemticPathJava(QDir(cooked_dir)))
        {
            QMessageBox warning;
            warning.setText(tr("You're trying to specify an instance folder which\'s path "
                               "contains at least one \'!\'. "
                               "Java is known to cause problems if that is the case, your "
                               "instances (probably) won't start!"));
            warning.setInformativeText(
                tr("Do you really want to use this path? "
                   "Selecting \"No\" will close this and not alter your instance path."));
            warning.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            int result = warning.exec();
            if (result == QMessageBox::Yes)
            {
                ui->instDirTextBox->setText(cooked_dir);
            }
        }
        else
        {
            ui->instDirTextBox->setText(cooked_dir);
        }
    }
}

void LauncherPage::on_iconsDirBrowseBtn_clicked()
{
    if (!ui->iconsDirTextBox)
    {
        return;
    }

    QString raw_dir = QFileDialog::getExistingDirectory(this, tr("Icons Folder"), ui->iconsDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!raw_dir.isEmpty() && QDir(raw_dir).exists())
    {
        QString cooked_dir = FS::NormalizePath(raw_dir);
        ui->iconsDirTextBox->setText(cooked_dir);
    }
}

void LauncherPage::on_modsDirBrowseBtn_clicked()
{
    if (!ui->modsDirTextBox)
    {
        return;
    }

    QString raw_dir = QFileDialog::getExistingDirectory(this, tr("Mods Folder"), ui->modsDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!raw_dir.isEmpty() && QDir(raw_dir).exists())
    {
        QString cooked_dir = FS::NormalizePath(raw_dir);
        ui->modsDirTextBox->setText(cooked_dir);
    }
}
void LauncherPage::on_migrateDataFolderMacBtn_clicked()
{
    QFile file(QDir::current().absolutePath() + "/dontmovemacdata");
    file.remove();
    QProcess::startDetached(qApp->arguments()[0]);
    qApp->quit();
}

void LauncherPage::applySettings()
{
    auto s = APPLICATION->settings();

    if (ui->resetNotificationsBtn->isChecked())
    {
        s->set("ShownNotifications", QString());
    }

    // Updates
    if (ui->autoUpdateCheckBox)
    {
        s->set("AutoUpdate", ui->autoUpdateCheckBox->isChecked());
    }
    auto original = s->get("IconTheme").toString();
    // FIXME: make generic
    if (ui->themeComboBox)
    {
        switch (ui->themeComboBox->currentIndex())
        {
        case 1:
            s->set("IconTheme", "pe_dark");
            break;
        case 2:
            s->set("IconTheme", "pe_light");
            break;
        case 3:
            s->set("IconTheme", "pe_blue");
            break;
        case 4:
            s->set("IconTheme", "pe_colored");
            break;
        case 5:
            s->set("IconTheme", "OSX");
            break;
        case 6:
            s->set("IconTheme", "iOS");
            break;
        case 7:
            s->set("IconTheme", "flat");
            break;
        case 8:
            s->set("IconTheme", "custom");
            break;
        case 0:
        default:
            s->set("IconTheme", "miniskins");
            break;
        }
    }
    if (ui->downloadcomboBox && ui->downloadcomboBox->currentIndex() < ui->downloadcomboBox->count())
    {
        const DownloadSource &secondSource = sources[ui->downloadcomboBox->currentIndex()];
        s->set("Downloadsource", secondSource.getType());
        s->set("Downloadsourceurl", secondSource.getUrl());
        s->set("Downloadsourceproxy", secondSource.isProxy());
    }

    if (ui->threadcomboBox)
    {
        switch (ui->threadcomboBox->currentIndex())
        {
        case 0:
            s->set("Threads", "4");
            break;
    case 1:
        s->set("Threads", "6");
        break;
    case 3:
        s->set("Threads", "16");
        break;
    case 4:
        s->set("Threads", "32");
        break;
    case 2:
    default:
        s->set("Threads", "8");
        break;
    }
    }

    if (ui->themeComboBoxColors)
    {
        auto originalAppTheme = s->get("ApplicationTheme").toString();
        auto newAppTheme = ui->themeComboBoxColors->currentData().toString();
        if (originalAppTheme != newAppTheme)
        {
            s->set("ApplicationTheme", newAppTheme);
            APPLICATION->setApplicationTheme(newAppTheme, false);
        }
    }

    // Console settings
    if (ui->showConsoleCheck)
    {
        s->set("ShowConsole", ui->showConsoleCheck->isChecked());
    }
    if (ui->autoCloseConsoleCheck)
    {
        s->set("AutoCloseConsole", ui->autoCloseConsoleCheck->isChecked());
    }
    if (ui->showConsoleErrorCheck)
    {
        s->set("ShowConsoleOnError", ui->showConsoleErrorCheck->isChecked());
    }
    if (ui->consoleFont && ui->fontSizeBox)
    {
        QString consoleFontFamily = ui->consoleFont->currentFont().family();
        s->set("ConsoleFont", consoleFontFamily);
        s->set("ConsoleFontSize", ui->fontSizeBox->value());
    }
    if (ui->lineLimitSpinBox)
    {
        s->set("ConsoleMaxLines", ui->lineLimitSpinBox->value());
    }
    if (ui->checkStopLogging)
    {
        s->set("ConsoleOverflowStop", ui->checkStopLogging->checkState() != Qt::Unchecked);
    }

    // Folders
    // TODO: Offer to move instances to new instance folder.
    if (ui->instDirTextBox)
    {
        s->set("InstanceDir", ui->instDirTextBox->text());
    }
    if (ui->modsDirTextBox)
    {
        s->set("CentralModsDir", ui->modsDirTextBox->text());
    }
    if (ui->iconsDirTextBox)
    {
        s->set("IconsDir", ui->iconsDirTextBox->text());
    }

    auto sortMode = (InstSortMode)ui->sortingModeGroup->checkedId();
    if (ui->sortLastLaunchedBtn)
    {
        switch (sortMode)
        {
        case Sort_LastLaunch:
        case Sort_Name:
        default:
            ui->sortLastLaunchedBtn->setChecked(true);
            break;
        }
    }
    else if (ui->sortByNameBtn)
    {
        switch (sortMode)
        {
        case Sort_Name:
        case Sort_LastLaunch:
        default:
            ui->sortByNameBtn->setChecked(true);
            break;
        }
    }
    else if (ui->sortingModeGroup)
    {
        switch (sortMode)
        {
        case Sort_LastLaunch:
            ui->sortingModeGroup->button(1)->setChecked(true);
            break;
        case Sort_Name:
        default:
            ui->sortingModeGroup->button(0)->setChecked(true);
            break;
        }
    }

    if (original != s->get("IconTheme"))
    {
        APPLICATION->setIconTheme(s->get("IconTheme").toString());
    }

    auto originalAppTheme = s->get("ApplicationTheme").toString();
    auto newAppTheme = ui->themeComboBoxColors->currentData().toString();
    if (originalAppTheme != newAppTheme)
    {
        s->set("ApplicationTheme", newAppTheme);
        APPLICATION->setApplicationTheme(newAppTheme, false);
    }

    // Console settings
    s->set("ShowConsole", ui->showConsoleCheck->isChecked());
    s->set("AutoCloseConsole", ui->autoCloseConsoleCheck->isChecked());
    s->set("ShowConsoleOnError", ui->showConsoleErrorCheck->isChecked());
    QString consoleFontFamily = ui->consoleFont->currentFont().family();
    s->set("ConsoleFont", consoleFontFamily);
    s->set("ConsoleFontSize", ui->fontSizeBox->value());
    s->set("ConsoleMaxLines", ui->lineLimitSpinBox->value());
    s->set("ConsoleOverflowStop", ui->checkStopLogging->checkState() != Qt::Unchecked);

    // Folders
    // TODO: Offer to move instances to new instance folder.
    s->set("InstanceDir", ui->instDirTextBox->text());
    s->set("CentralModsDir", ui->modsDirTextBox->text());
    s->set("IconsDir", ui->iconsDirTextBox->text());

    auto sortMode = (InstSortMode)ui->sortingModeGroup->checkedId();
    switch (sortMode)
    {
    case Sort_LastLaunch:
        s->set("InstSortMode", "LastLaunch");
        break;
    case Sort_Name:
    default:
        s->set("InstSortMode", "Name");
        break;
    }
}
void LauncherPage::loadSettings()
{

    auto s = APPLICATION->settings();
    // Updates
    ui->autoUpdateCheckBox->setChecked(s->get("AutoUpdate").toBool());
    // FIXME: make generic
    auto theme = s->get("IconTheme").toString();
    if (theme == "pe_dark")
    {
        ui->themeComboBox->setCurrentIndex(1);
    }
    else if (theme == "pe_light")
    {
        ui->themeComboBox->setCurrentIndex(2);
    }
    else if (theme == "pe_blue")
    {
        ui->themeComboBox->setCurrentIndex(3);
    }
    else if (theme == "pe_colored")
    {
        ui->themeComboBox->setCurrentIndex(4);
    }
    else if (theme == "OSX")
    {
        ui->themeComboBox->setCurrentIndex(5);
    }
    else if (theme == "iOS")
    {
        ui->themeComboBox->setCurrentIndex(6);
    }
    else if (theme == "flat")
    {
        ui->themeComboBox->setCurrentIndex(7);
    }
    else if (theme == "custom")
    {
        ui->themeComboBox->setCurrentIndex(8);
    }
    else
    {
        ui->themeComboBox->setCurrentIndex(0);
    }

    int i = 0;
    auto download = s->get("Downloadsource").toString();
    int selectedIndex = -1;
    for (const DownloadSource &source : sources)
    {
        if (ui->downloadcomboBox)
        {
            ui->downloadcomboBox->addItem(source.getName());
            if (source.getType() == download)
            {
                selectedIndex = i;
            }
        }
        i++;
    }

    if (selectedIndex != -1 && ui->downloadcomboBox)
    {
        ui->downloadcomboBox->setCurrentIndex(selectedIndex);
    }

    auto thread = s->get("Threads").toString();
    if (thread == "4")
    {
        ui->threadcomboBox->setCurrentIndex(0);
    }
    else if (thread == "6")
    {
        ui->threadcomboBox->setCurrentIndex(1);
    }
    else if (thread == "8")
    {
        ui->threadcomboBox->setCurrentIndex(2);
    }
    else if (thread == "16")
    {
        ui->threadcomboBox->setCurrentIndex(3);
    }
    else if (thread == "32")
    {
        ui->threadcomboBox->setCurrentIndex(4);
    }
    else
    {
        ui->threadcomboBox->setCurrentIndex(2);
    }

    {
        auto currentTheme = s->get("ApplicationTheme").toString();
        auto themes = APPLICATION->getValidApplicationThemes();
        int idx = 0;
        if (ui->themeComboBoxColors)
        {
            ui->themeComboBoxColors->clear();
            for (auto &theme : themes)
            {
                ui->themeComboBoxColors->addItem(theme->name(), theme->id());
                if (currentTheme == theme->id())
                {
                    ui->themeComboBoxColors->setCurrentIndex(idx);
                }
                idx++;
            }
        }
    }

    // Console settings
    if (ui->showConsoleCheck)
    {
        ui->showConsoleCheck->setChecked(s->get("ShowConsole").toBool());
    }
    if (ui->autoCloseConsoleCheck)
    {
        ui->autoCloseConsoleCheck->setChecked(s->get("AutoCloseConsole").toBool());
    }
    if (ui->showConsoleErrorCheck)
    {
        ui->showConsoleErrorCheck->setChecked(s->get("ShowConsoleOnError").toBool());
    }
    if (ui->consoleFont && ui->fontSizeBox)
    {
        QString fontFamily = APPLICATION->settings()->get("ConsoleFont").toString();
        QFont consoleFont(fontFamily);
        ui->consoleFont->setCurrentFont(consoleFont);

        bool conversionOk = true;
        int fontSize = APPLICATION->settings()->get("ConsoleFontSize").toInt(&conversionOk);
        if (!conversionOk)
        {
            fontSize = 11;
        }
        ui->fontSizeBox->setValue(fontSize);
    }
    refreshFontPreview();
    if (ui->lineLimitSpinBox)
    {
        ui->lineLimitSpinBox->setValue(s->get("ConsoleMaxLines").toInt());
    }
    if (ui->checkStopLogging)
    {
        ui->checkStopLogging->setChecked(s->get("ConsoleOverflowStop").toBool());
    }

    // Folders
    if (ui->instDirTextBox)
    {
        ui->instDirTextBox->setText(s->get("InstanceDir").toString());
    }
    if (ui->modsDirTextBox)
    {
        ui->modsDirTextBox->setText(s->get("CentralModsDir").toString());
    }
    if (ui->iconsDirTextBox)
    {
        ui->iconsDirTextBox->setText(s->get("IconsDir").toString());
    }

    QString sortMode = s->get("InstSortMode").toString();

    if (sortMode == "LastLaunch")
    {
        if (ui->sortLastLaunchedBtn)
        {
            ui->sortLastLaunchedBtn->setChecked(true);
        }
    }
    else
    {
        if (ui->sortByNameBtn)
        {
            ui->sortByNameBtn->setChecked(true);
        }
    }
}

void LauncherPage::refreshFontPreview()
{
    int fontSize = ui->fontSizeBox->value();
    QString fontFamily = ui->consoleFont->currentFont().family();
    ui->fontPreview->clear();
    defaultFormat->setFont(QFont(fontFamily, fontSize));
    {
        QTextCharFormat format(*defaultFormat);
        format.setForeground(m_colors->getFront(MessageLevel::Error));
        // append a paragraph/line
        auto workCursor = ui->fontPreview->textCursor();
        workCursor.movePosition(QTextCursor::End);
        workCursor.insertText(tr("[Something/ERROR] A spooky error!"), format);
        workCursor.insertBlock();
    }
    {
        QTextCharFormat format(*defaultFormat);
        format.setForeground(m_colors->getFront(MessageLevel::Message));
        // append a paragraph/line
        auto workCursor = ui->fontPreview->textCursor();
        workCursor.movePosition(QTextCursor::End);
        workCursor.insertText(tr("[Test/INFO] A harmless message..."), format);
        workCursor.insertBlock();
    }
    {
        QTextCharFormat format(*defaultFormat);
        format.setForeground(m_colors->getFront(MessageLevel::Warning));
        // append a paragraph/line
        auto workCursor = ui->fontPreview->textCursor();
        workCursor.movePosition(QTextCursor::End);
        workCursor.insertText(tr("[Something/WARN] A not so spooky warning."), format);
        workCursor.insertBlock();
    }
}

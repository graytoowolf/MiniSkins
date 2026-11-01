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
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QSignalBlocker>

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
    connect(ui->downloadcomboBox, SIGNAL(currentIndexChanged(int)), SLOT(onDownloadSourceChanged(int)));
    connect(ui->downloadcomboBox, SIGNAL(deleteRequested(int)), SLOT(onDownloadSourceDeleteRequested(int)));
    connect(ui->downloadcomboBox, SIGNAL(editRequested(int)), SLOT(onDownloadSourceEditRequested(int)));

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
    if (ui->downloadcomboBox && !sources.isEmpty())
    {
        const int index = ui->downloadcomboBox->currentIndex();
        if (index >= 0 && index < sources.size())
        {
            // 选中项立即生效并写入配置，官方默认源不显式写入
            persistDownloadSelection(index);
        }
        // 源列表（含多个用户自定义源）统一保存到独立配置文件
        APPLICATION->setDownloadSources(sources);
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

    if (ui->sortLastLaunchedBtn)
    {
        auto sortMode = (InstSortMode)ui->sortingModeGroup->checkedId();
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
        auto sortMode = (InstSortMode)ui->sortingModeGroup->checkedId();
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
        auto sortMode = (InstSortMode)ui->sortingModeGroup->checkedId();
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

    if (ui->downloadcomboBox)
    {
        QSignalBlocker blocker(ui->downloadcomboBox);
        ui->downloadcomboBox->clear();
        auto download = s->get("Downloadsource").toString();
        auto customUrl = s->get("Downloadsourcecustomurl").toString();
        int selectedIndex = -1;
        int firstCustomIndex = -1;
        m_lastDownloadSourceIndex = 0;
        for (int i = 0; i < sources.size(); i++)
        {
            const DownloadSource &source = sources[i];
            const bool isCustom = source.getType() == "Custom";
            // 末尾空的 Custom 条目是“新增自定义源”入口，不是真实的自定义源
            const bool isPlaceholder = isCustom && source.getUrl().isEmpty();
            // 只有真实的自定义源才显示行内编辑与删除按钮，官方源与 BMCLAPI 保持原样
            const bool hasItemActions = isCustom && !source.getUrl().isEmpty();
            QString displayName = source.getName();
            if (displayName.isEmpty())
            {
                displayName = tr("Custom");
            }
            ui->downloadcomboBox->addItem(displayName);
            ui->downloadcomboBox->setItemActions(i, hasItemActions, hasItemActions);

            bool matches = false;
            if (isCustom)
            {
                // 多个自定义源之间按代理地址区分，不能只按类型匹配
                matches = !isPlaceholder && source.getUrl() == customUrl;
                if (firstCustomIndex == -1 && !isPlaceholder)
                {
                    firstCustomIndex = i;
                }
            }
            else
            {
                matches = source.getType() == download;
            }
            if (matches && selectedIndex == -1)
            {
                selectedIndex = i;
            }
        }
        // 标识缺失时回退到第一个自定义源，再回退到官方源
        if (selectedIndex == -1 && download == "Custom" && firstCustomIndex != -1)
        {
            selectedIndex = firstCustomIndex;
        }
        if (selectedIndex == -1 && ui->downloadcomboBox->count() > 0)
        {
            selectedIndex = 0;
        }
        if (selectedIndex != -1)
        {
            ui->downloadcomboBox->setCurrentIndex(selectedIndex);
            // 自定义源编辑入口不作为上一次有效选择
            if (!(sources[selectedIndex].getType() == "Custom" && sources[selectedIndex].getUrl().isEmpty()))
            {
                m_lastDownloadSourceIndex = selectedIndex;
            }
        }
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

void LauncherPage::persistDownloadSelection(int index)
{
    if (index < 0 || index >= sources.size())
    {
        return;
    }
    auto s = APPLICATION->settings();
    const DownloadSource &selectedSource = sources[index];
    // 官方默认源使用内置默认值，不显式写入配置文件
    if (selectedSource.getType() == "Mojang")
    {
        s->reset("Downloadsource");
        s->reset("Downloadsourcecustomurl");
        return;
    }
    if (selectedSource.getType() == "Custom")
    {
        // 末尾的“自定义”入口不是真实的自定义源，不写入选中标识
        if (selectedSource.getUrl().isEmpty())
        {
            return;
        }
        s->set("Downloadsource", "Custom");
        s->set("Downloadsourcecustomurl", selectedSource.getUrl());
        return;
    }
    s->set("Downloadsource", selectedSource.getType());
    s->reset("Downloadsourcecustomurl");
}

bool LauncherPage::isDuplicateCustomUrl(const QString &url, int exceptIndex) const
{
    for (int i = 0; i < sources.size(); i++)
    {
        if (i == exceptIndex)
        {
            continue;
        }
        if (sources[i].getType() == "Custom" && sources[i].getUrl() == url)
        {
            return true;
        }
    }
    return false;
}

bool LauncherPage::openCustomSourceDialog(const QString &initialName, const QString &initialUrl, QString &name, QString &url)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Custom Source"));

    QLineEdit *nameEdit = new QLineEdit(initialName, &dialog);
    QLineEdit *urlEdit = new QLineEdit(initialUrl, &dialog);
    urlEdit->setPlaceholderText("https://***.****.com");
    // 按字体度量保证占位提示完整可见，并随 DPI 缩放自适应
    urlEdit->setMinimumWidth(urlEdit->fontMetrics().width(urlEdit->placeholderText()) + 24);

    // 表单只作为内容布局加入，避免与按钮布局同时挂到对话框上导致控件重叠
    QFormLayout *formLayout = new QFormLayout();
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    formLayout->addRow(tr("Source name:"), nameEdit);
    formLayout->addRow(tr("Proxy address:"), urlEdit);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    // 窗口随内容自适应，且不允许缩小到裁切控件
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    layout->addLayout(formLayout);
    layout->addWidget(buttonBox, 0, Qt::AlignRight);

    if (dialog.exec() != QDialog::Accepted)
    {
        return false;
    }

    QString newName = nameEdit->text().trimmed();
    QString newUrl = urlEdit->text().trimmed();
    if (newName.isEmpty())
    {
        newName = tr("Custom");
    }
    if (newUrl.isEmpty())
    {
        // 未填写代理地址时不写入任何配置
        QMessageBox::warning(this, tr("Custom Source"), tr("Please enter a proxy address."));
        return false;
    }
    if (!newUrl.startsWith("http://") && !newUrl.startsWith("https://"))
    {
        newUrl.prepend("https://");
    }
    while (newUrl.endsWith('/'))
    {
        newUrl.chop(1);
    }
    name = newName;
    url = newUrl;
    return true;
}

void LauncherPage::onDownloadSourceChanged(int index)
{
    if (m_editingCustomSource || index < 0 || index >= sources.size())
    {
        return;
    }

    const DownloadSource &selected = sources[index];
    // 非“自定义”入口：记录并立即持久化当前选择
    if (selected.getType() != "Custom" || !selected.getUrl().isEmpty())
    {
        m_lastDownloadSourceIndex = index;
        persistDownloadSelection(index);
        return;
    }

    // 选中末尾“自定义”入口：每次都新增一个独立的自定义代理源
    m_editingCustomSource = true;
    QString name;
    QString url;
    if (openCustomSourceDialog(QString(), QString(), name, url))
    {
        if (isDuplicateCustomUrl(url, -1))
        {
            QMessageBox::warning(this, tr("Custom Source"), tr("This proxy address is already in use."));
            QSignalBlocker blocker(ui->downloadcomboBox);
            ui->downloadcomboBox->setCurrentIndex(m_lastDownloadSourceIndex);
        }
        else
        {
            {
                QSignalBlocker blocker(ui->downloadcomboBox);
                DownloadSource newSource(name, url, "Custom", true);
                // 新自定义源插入到“自定义”入口上方，与已有自定义源一起排列
                sources.insert(index, newSource);
                ui->downloadcomboBox->insertItem(index, name);
                ui->downloadcomboBox->setItemActions(index, true, true);
                ui->downloadcomboBox->setCurrentIndex(index);
            }
            m_lastDownloadSourceIndex = index;
            // 新增后立即生效并持久化
            persistDownloadSelection(index);
            APPLICATION->setDownloadSources(sources);
        }
    }
    else
    {
        QSignalBlocker blocker(ui->downloadcomboBox);
        ui->downloadcomboBox->setCurrentIndex(m_lastDownloadSourceIndex);
    }
    m_editingCustomSource = false;
}

void LauncherPage::onDownloadSourceDeleteRequested(int index)
{
    if (index < 0 || index >= sources.size())
    {
        return;
    }
    const DownloadSource &source = sources[index];
    // 内置源与“自定义”入口没有删除按钮，这里再做一次防御
    if (source.getType() != "Custom" || source.getUrl().isEmpty())
    {
        return;
    }

    QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        tr("Delete Source"),
        tr("Delete the custom source \"%1\"?").arg(source.getName()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    const bool wasSelected = ui->downloadcomboBox->currentIndex() == index;
    int fallbackIndex = 0;
    {
        QSignalBlocker blocker(ui->downloadcomboBox);
        ui->downloadcomboBox->removeItem(index);
        sources.removeAt(index);
        // 删除当前选中的源后，退回到相邻的有效源（不会落到“自定义”入口）
        fallbackIndex = qMax(index - 1, 0);
        if (fallbackIndex >= ui->downloadcomboBox->count())
        {
            fallbackIndex = qMax(ui->downloadcomboBox->count() - 1, 0);
        }
        if (wasSelected)
        {
            ui->downloadcomboBox->setCurrentIndex(fallbackIndex);
        }
        else
        {
            fallbackIndex = ui->downloadcomboBox->currentIndex();
        }
    }
    m_lastDownloadSourceIndex = fallbackIndex;
    if (wasSelected)
    {
        // 删除当前选中源后立即生效并同步配置
        persistDownloadSelection(fallbackIndex);
    }
    APPLICATION->setDownloadSources(sources);
}

void LauncherPage::onDownloadSourceEditRequested(int index)
{
    if (index < 0 || index >= sources.size())
    {
        return;
    }
    const DownloadSource &source = sources[index];
    // 仅真实的自定义源支持编辑
    if (source.getType() != "Custom" || source.getUrl().isEmpty())
    {
        return;
    }

    QString name;
    QString url;
    if (!openCustomSourceDialog(source.getName(), source.getUrl(), name, url))
    {
        return;
    }
    if (isDuplicateCustomUrl(url, index))
    {
        QMessageBox::warning(this, tr("Custom Source"), tr("This proxy address is already in use."));
        return;
    }

    sources[index].setName(name);
    sources[index].setUrl(url);
    {
        QSignalBlocker blocker(ui->downloadcomboBox);
        ui->downloadcomboBox->setItemText(index, name);
    }
    // 正在使用该源时同步更新选中标识，保证修改立即生效
    if (ui->downloadcomboBox->currentIndex() == index)
    {
        m_lastDownloadSourceIndex = index;
        persistDownloadSelection(index);
    }
    APPLICATION->setDownloadSources(sources);
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

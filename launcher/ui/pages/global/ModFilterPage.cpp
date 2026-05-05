#include "ModFilterPage.h"
#include "ui_ModFilterPage.h"
#include "Application.h"
#include "minecraft/mod/fingerprint.h"
#include "minecraft/mod/LocalModParseTask.h"
#include "minecraft/mod/Mod.h"
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QStyleOption>
#include <QMenu>
#include <QClipboard>
#include <QAction>

using fingerprint::ModInfo;

namespace
{
// 常量定义
constexpr int CHECKBOX_COLUMN = 0;
constexpr int PROJECT_ID_COLUMN = 1;
constexpr int MOD_NAME_COLUMN = 2;
constexpr int CHECKBOX_COLUMN_WIDTH = 20;

const QString PROVISIONAL_MOD_NAME = "Provisional Name";
}

// 静态成员定义
const QStringList ModFilterPage::SUPPORTED_EXTENSIONS = {".jar", ".disabled"};

ModFilterPage::ModFilterPage(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::ModFilterPage)
{
    ui->setupUi(this);
    setupUi();
    refreshData();
    setAcceptDrops(true);
}

ModFilterPage::~ModFilterPage()
{
    delete ui;
}

bool ModFilterPage::apply()
{
    APPLICATION->saveModList();
    return true;
}

bool ModFilterPage::isSupportedFile(const QString &filePath) const
{
    return std::any_of(SUPPORTED_EXTENSIONS.begin(), SUPPORTED_EXTENSIONS.end(),
                       [&filePath](const QString &ext)
    {
        return filePath.endsWith(ext, Qt::CaseInsensitive);
    });
}

void ModFilterPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls())
    {
        return;
    }

    const QList<QUrl> urls = event->mimeData()->urls();
    bool hasValidFile = std::any_of(urls.begin(), urls.end(),
                                    [this](const QUrl &url)
    {
        return isSupportedFile(url.toLocalFile());
    });

    if (hasValidFile)
    {
        event->acceptProposedAction();
    }
}

void ModFilterPage::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    QStringList validFilePaths;

    // 收集所有有效的文件路径
    for (const QUrl &url : urls)
    {
        const QString filePath = url.toLocalFile();
        if (isSupportedFile(filePath))
        {
            validFilePaths.append(filePath);
        }
    }

    if (validFilePaths.isEmpty())
    {
        event->acceptProposedAction();
        return;
    }

    // 创建ModInfo列表
    QList<ModInfo> modInfoList;
    for (const QString &filePath : validFilePaths)
    {
        modInfoList.append(ModInfo(filePath));
    }

    // 批量处理模组信息
    QList<ModInfo> processedModInfos = fingerprint::processModInfoList(modInfoList);
    QMap<int, QString> modsToAdd;

    for (const ModInfo &modInfo : processedModInfos)
    {
        if (modInfo.isValid)
        {
            QString modName = PROVISIONAL_MOD_NAME;

            QFileInfo fileInfo(modInfo.filePath);
            LocalModParseTask parseTask(0, Mod::MOD_ZIPFILE, fileInfo);
            parseTask.run();

            auto result = parseTask.result();
            if (result && result->details && !result->details->name.isEmpty())
            {
                modName = result->details->name;
            }

            modsToAdd.insert(modInfo.projectId, modName);
        }
    }

    if (!modsToAdd.isEmpty())
    {
        const bool isWhitelist = (ui->tabWidget->currentIndex() == 1);
        if (isWhitelist)
        {
            APPLICATION->addModsToWhitelist(modsToAdd);
        }
        else
        {
            APPLICATION->addModsToBlacklist(modsToAdd);
        }
        refreshData();
        showInfoMessage(tr("Success"),
                        tr("Processed %1 mod(s) for %2.").arg(modsToAdd.size()).arg(isWhitelist ? tr("whitelist") : tr("blacklist")));
    }

    event->acceptProposedAction();
}



void ModFilterPage::setupUi()
{
    setupTableWidget(ui->blacklistTableWidget);
    setupTableWidget(ui->whitelistTableWidget);
    connectSignals();

    // 为表格的 viewport 安装事件过滤器
    ui->blacklistTableWidget->viewport()->installEventFilter(this);
    ui->whitelistTableWidget->viewport()->installEventFilter(this);

    // 设置上下文菜单策略
    ui->blacklistTableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->whitelistTableWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    // 初始化提示标签可见性 (延迟调用以确保UI完全设置)
    QTimer::singleShot(0, this, &ModFilterPage::updateDropHintVisibility);
}

void ModFilterPage::setupTableWidget(QTableWidget *tableWidget)
{
    tableWidget->setColumnCount(3);

    QStringList headers;
    headers << "" << tr("Project ID") << tr("Mod name");
    tableWidget->setHorizontalHeaderLabels(headers);

    // 设置列宽策略
    QHeaderView *header = tableWidget->horizontalHeader();
    header->setSectionResizeMode(CHECKBOX_COLUMN, QHeaderView::Fixed);
    header->setSectionResizeMode(PROJECT_ID_COLUMN, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(MOD_NAME_COLUMN, QHeaderView::Stretch);

    tableWidget->setColumnWidth(CHECKBOX_COLUMN, CHECKBOX_COLUMN_WIDTH);
    tableWidget->verticalHeader()->setVisible(false);
    tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget->setSelectionMode(QAbstractItemView::ContiguousSelection);
    tableWidget->setAlternatingRowColors(true);
}

void ModFilterPage::connectSignals()
{
    // 黑名单表格信号连接
    connect(ui->blacklistTableWidget, &QTableWidget::cellDoubleClicked,
            this, &ModFilterPage::onBlacklistCellDoubleClicked);
    connect(ui->blacklistTableWidget, &QTableWidget::cellChanged,
            this, &ModFilterPage::onBlacklistCellChanged);

    // 白名单表格信号连接
    connect(ui->whitelistTableWidget, &QTableWidget::cellDoubleClicked,
            this, &ModFilterPage::onWhitelistCellDoubleClicked);
    connect(ui->whitelistTableWidget, &QTableWidget::cellChanged,
            this, &ModFilterPage::onWhitelistCellChanged);

    // 统一处理删除按钮
    if (ui->removeButton)
    {
        connect(ui->removeButton, &QPushButton::clicked, this, [this]()
        {
            QTableWidget *currentTable = (ui->tabWidget->currentIndex() == 0)
                    ? ui->blacklistTableWidget
                    : ui->whitelistTableWidget;
            removeSelectedMods(currentTable, ui->tabWidget->currentIndex() == 1); });
    }

    // 标签页切换时更新提示标签可见性
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &ModFilterPage::updateDropHintVisibility);
}

void ModFilterPage::loadList(QTableWidget *tableWidget, const QMap<int, QString> &modList)
{
    tableWidget->blockSignals(true);
    tableWidget->setRowCount(0);

    for (auto it = modList.constBegin(); it != modList.constEnd(); ++it)
    {
        const int projectId = it.key();
        const QString &name = it.value();
        addModToTable(tableWidget, projectId, name);
    }

    tableWidget->blockSignals(false);
}

void ModFilterPage::addModToTable(QTableWidget *tableWidget, int projectId, const QString &name)
{
    const int row = tableWidget->rowCount();
    tableWidget->insertRow(row);

    // 添加复选框
    auto *checkItem = new QTableWidgetItem();
    checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    checkItem->setCheckState(Qt::Unchecked);
    checkItem->setBackground(tableWidget->palette().base());
    tableWidget->setItem(row, CHECKBOX_COLUMN, checkItem);

    // Project ID (不可编辑)
    auto *idItem = new QTableWidgetItem(QString::number(projectId));
    idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
    tableWidget->setItem(row, PROJECT_ID_COLUMN, idItem);

    // Mod 名称 (可编辑)
    tableWidget->setItem(row, MOD_NAME_COLUMN, new QTableWidgetItem(name));
}

void ModFilterPage::loadBlacklist()
{
    loadList(ui->blacklistTableWidget, APPLICATION->getModBlacklist());
}

void ModFilterPage::loadWhitelist()
{
    loadList(ui->whitelistTableWidget, APPLICATION->getModWhitelist());
}

void ModFilterPage::refreshData()
{
    loadBlacklist();
    loadWhitelist();
    updateDropHintVisibility();
}

void ModFilterPage::onBlacklistCellDoubleClicked(int row, int column)
{
    handleCellDoubleClick(ui->blacklistTableWidget, row, column);
}

void ModFilterPage::onWhitelistCellDoubleClicked(int row, int column)
{
    handleCellDoubleClick(ui->whitelistTableWidget, row, column);
}

void ModFilterPage::handleCellDoubleClick(QTableWidget *tableWidget, int row, int column)
{
    if (column == MOD_NAME_COLUMN)
    {
        return; // 名称列双击不切换复选框状态
    }

    QTableWidgetItem *checkItem = tableWidget->item(row, CHECKBOX_COLUMN);
    if (checkItem)
    {
        const bool checked = (checkItem->checkState() == Qt::Checked);
        checkItem->setCheckState(checked ? Qt::Unchecked : Qt::Checked);
    }
}

void ModFilterPage::onBlacklistCellChanged(int row, int column)
{
    handleCellChanged(ui->blacklistTableWidget, row, column, false);
}

void ModFilterPage::onWhitelistCellChanged(int row, int column)
{
    handleCellChanged(ui->whitelistTableWidget, row, column, true);
}

void ModFilterPage::handleCellChanged(QTableWidget *tableWidget, int row, int column, bool isWhitelist)
{
    if (column != MOD_NAME_COLUMN)
    {
        return; // 只处理 Mod 名称列的修改
    }

    bool ok;
    int projectId = tableWidget->item(row, PROJECT_ID_COLUMN)->text().toInt(&ok);
    if (!ok)
    {
        qWarning() << "Invalid project ID in row" << row;
        return;
    }

    QString newName = tableWidget->item(row, MOD_NAME_COLUMN)->text();

    if (isWhitelist)
    {
        APPLICATION->updateModWhitelistName(projectId, newName);
    }
    else
    {
        APPLICATION->updateModBlacklistName(projectId, newName);
    }
}

void ModFilterPage::removeSelectedMods(QTableWidget *tableWidget, bool isWhitelist)
{
    QList<int> projectIdsToRemove;

    // 收集要删除的项目 ID
    for (int row = 0; row < tableWidget->rowCount(); ++row)
    {
        QTableWidgetItem *checkItem = tableWidget->item(row, CHECKBOX_COLUMN);
        if (checkItem && checkItem->checkState() == Qt::Checked)
        {
            bool ok;
            int projectId = tableWidget->item(row, PROJECT_ID_COLUMN)->text().toInt(&ok);
            if (ok)
            {
                projectIdsToRemove.append(projectId);
            }
        }
    }

    if (projectIdsToRemove.isEmpty())
    {
        showInfoMessage(tr("Information"), tr("No items selected for removal."));
        return;
    }

    // 执行删除操作
    if (isWhitelist)
    {
        APPLICATION->removeModsFromWhitelist(projectIdsToRemove);
    }
    else
    {
        APPLICATION->removeModsFromBlacklist(projectIdsToRemove);
    }

    refreshData();
    showInfoMessage(tr("Success"),
                    tr("Attempted to remove %1 mod(s) from %2.").arg(projectIdsToRemove.size()).arg(isWhitelist ? tr("whitelist") : tr("blacklist")));
}

void ModFilterPage::showErrorMessage(const QString &title, const QString &message)
{
    QMessageBox::warning(this, title, message);
}

void ModFilterPage::showInfoMessage(const QString &title, const QString &message)
{
    QMessageBox::information(this, title, message);
}

void ModFilterPage::updateDropHintVisibility()
{
    // 触发两个表格的 viewport 重绘，以便 eventFilter 处理提示的显示
    if (ui->blacklistTableWidget && ui->blacklistTableWidget->viewport())
    {
        ui->blacklistTableWidget->viewport()->update();
    }
    if (ui->whitelistTableWidget && ui->whitelistTableWidget->viewport())
    {
        ui->whitelistTableWidget->viewport()->update();
    }
}

bool ModFilterPage::eventFilter(QObject *watched, QEvent *event)
{
    QTableWidget *targetTable = nullptr;
    bool isBlacklistTabActive = (ui->tabWidget->currentIndex() == 0);

    if (watched == ui->blacklistTableWidget->viewport())
    {
        targetTable = ui->blacklistTableWidget;
        if (!isBlacklistTabActive)
            return QWidget::eventFilter(watched, event); // 只在当前标签页绘制
    }
    else if (watched == ui->whitelistTableWidget->viewport())
    {
        targetTable = ui->whitelistTableWidget;
        if (isBlacklistTabActive)
            return QWidget::eventFilter(watched, event); // 只在当前标签页绘制
    }

    if (targetTable && event->type() == QEvent::Paint && targetTable->rowCount() == 0)
    {
        QPainter painter(targetTable->viewport());
        painter.setRenderHint(QPainter::Antialiasing);

        // 定义提示文本和字体
        QString text = tr("Drag and drop MOD files into the table to add them to %1.\nSelect the items you want to remove, then click \"Remove Selected\".")
                .arg(isBlacklistTabActive ? tr("blacklist") : tr("whitelist"));
        QFont font("sans", 16);
        font.setBold(true);
        painter.setFont(font);

        QRect bounds = targetTable->viewport()->rect();
        bounds.moveTop(0);
        auto innerBounds = bounds;
        innerBounds.adjust(10, 10, -10, -10);

        // 根据用户提供的图片2样式，背景设置为黑色，文本设置为白色
        QColor backgroundColor = Qt::black;
        QColor textColor = Qt::white;

        painter.setFont(font);
        auto fontMetrics = painter.fontMetrics();
        auto textRect = fontMetrics.boundingRect(innerBounds, Qt::AlignHCenter | Qt::TextWordWrap, text);
        textRect.moveCenter(bounds.center());

        auto wrapRect = textRect;
        wrapRect.adjust(-10, -10, 10, 10); // 与 VersionListView 保持一致的padding

        // 绘制背景
        painter.setBrush(QBrush(backgroundColor));
        painter.setPen(Qt::NoPen); // VersionListView 中有Pen，但这里为了纯色背景，可以设为NoPen
        // painter.setPen(textColor); // 如果需要边框，则使用textColor
        painter.drawRoundedRect(wrapRect, 5.0, 5.0); // 与 VersionListView 保持一致的圆角

        // 绘制文本
        painter.setPen(textColor);
        painter.drawText(textRect, Qt::AlignHCenter | Qt::TextWordWrap, text);

        return true; // 事件已处理
    }
    return QWidget::eventFilter(watched, event);
}

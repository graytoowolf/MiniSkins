#include "modblacklistpage.h"
#include "ui_modblacklistpage.h"
#include "Application.h"
#include "minecraft/mod/fingerprint.h"
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QStyleOption>

namespace
{
    // 常量定义
    constexpr int CHECKBOX_COLUMN = 0;
    constexpr int PROJECT_ID_COLUMN = 1;
    constexpr int MOD_NAME_COLUMN = 2;
    constexpr int CHECKBOX_COLUMN_WIDTH = 20;

    // API 相关常量
    const QString CURSEFORGE_API_URL = "https://api.curseforge.com/v1/fingerprints";
    const QString PROVISIONAL_MOD_NAME = "Provisional Name";
}

// 静态成员定义
const QStringList ModBlacklistPage::SUPPORTED_EXTENSIONS = {".jar", ".disabled"};

ModBlacklistPage::ModBlacklistPage(QWidget *parent)
    : QWidget(parent), ui(new Ui::ModBlacklistPage)
{
    ui->setupUi(this);
    setupUi();
    refreshData();
    setAcceptDrops(true);
}

ModBlacklistPage::~ModBlacklistPage()
{
    delete ui;
}

bool ModBlacklistPage::isSupportedFile(const QString &filePath) const
{
    return std::any_of(SUPPORTED_EXTENSIONS.begin(), SUPPORTED_EXTENSIONS.end(),
                       [&filePath](const QString &ext)
                       {
                           return filePath.endsWith(ext, Qt::CaseInsensitive);
                       });
}

void ModBlacklistPage::dragEnterEvent(QDragEnterEvent *event)
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

void ModBlacklistPage::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    QStringList fingerprints;

    for (const QUrl &url : urls)
    {
        const QString filePath = url.toLocalFile();
        if (!isSupportedFile(filePath))
        {
            continue;
        }

        const QString hash = fingerprint::getJarFingerprint(filePath);
        if (!hash.isEmpty())
        {
            fingerprints.append(hash);
        }
    }

    if (!fingerprints.isEmpty())
    {
        const bool isWhitelist = (ui->tabWidget->currentIndex() == 1);
        requestModInfo(fingerprints, isWhitelist);
    }

    event->acceptProposedAction();
}

void ModBlacklistPage::requestModInfo(const QStringList &fingerprints, bool isWhitelist)
{
    if (fingerprints.isEmpty())
    {
        return;
    }

    // 构建请求 JSON
    QJsonObject requestObj;
    QJsonArray fingerprintArray;

    for (const QString &fp : fingerprints)
    {
        bool ok;
        qlonglong fingerprint = fp.toLongLong(&ok);
        if (ok)
        {
            fingerprintArray.append(QJsonValue(fingerprint));
        }
    }

    if (fingerprintArray.isEmpty())
    {
        qWarning() << "No valid fingerprints to process";
        return;
    }

    requestObj["fingerprints"] = fingerprintArray;
    QJsonDocument doc{requestObj};
    QByteArray data = doc.toJson();

    // 创建网络请求
    QNetworkRequest request{QUrl(CURSEFORGE_API_URL)};
    request.setRawHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // 发送 POST 请求
    auto *reply = APPLICATION->network()->post(request, data);

    connect(reply, &QNetworkReply::finished, this, [this, reply, isWhitelist]()
            {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Fingerprint lookup failed:" << reply->errorString();
            showErrorMessage(tr("Network Error"),
                           tr("Failed to lookup mod information: %1").arg(reply->errorString()));
            return;
        }

        processModInfoResponse(reply->readAll(), isWhitelist); });
}

void ModBlacklistPage::processModInfoResponse(const QByteArray &responseData, bool isWhitelist)
{
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    QJsonObject root = doc.object();

    if (!root.contains("data"))
    {
        qWarning() << "Invalid response format: missing 'data' field";
        return;
    }

    QJsonObject data = root["data"].toObject();
    if (!data.contains("exactMatches"))
    {
        qWarning() << "Invalid response format: missing 'exactMatches' field";
        return;
    }

    QJsonArray matches = data["exactMatches"].toArray();
    int addedCount = 0;

    for (const QJsonValue &matchValue : matches)
    {
        QJsonObject match = matchValue.toObject();
        int projectId = match["id"].toInt();

        if (projectId <= 0)
        {
            continue;
        }

        bool success = isWhitelist
                           ? APPLICATION->addModToWhitelist(projectId, PROVISIONAL_MOD_NAME)
                           : APPLICATION->addModToBlacklist(projectId, PROVISIONAL_MOD_NAME);

        if (success)
        {
            ++addedCount;
        }
    }

    if (addedCount > 0)
    {
        refreshData();
        showInfoMessage(tr("Success"),
                        tr("Added %1 mod(s) to %2").arg(addedCount).arg(isWhitelist ? tr("whitelist") : tr("blacklist")));
    }
}

void ModBlacklistPage::setupUi()
{
    setupTableWidget(ui->blacklistTableWidget);
    setupTableWidget(ui->whitelistTableWidget);
    connectSignals();

    // 为表格的 viewport 安装事件过滤器
    ui->blacklistTableWidget->viewport()->installEventFilter(this);
    ui->whitelistTableWidget->viewport()->installEventFilter(this);

    // 初始化提示标签可见性 (延迟调用以确保UI完全设置)
    QTimer::singleShot(0, this, &ModBlacklistPage::updateDropHintVisibility);
}

void ModBlacklistPage::setupTableWidget(QTableWidget *tableWidget)
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

void ModBlacklistPage::connectSignals()
{
    // 黑名单表格信号连接
    connect(ui->blacklistTableWidget, &QTableWidget::cellDoubleClicked,
            this, &ModBlacklistPage::onBlacklistCellDoubleClicked);
    connect(ui->blacklistTableWidget, &QTableWidget::cellChanged,
            this, &ModBlacklistPage::onBlacklistCellChanged);

    // 白名单表格信号连接
    connect(ui->whitelistTableWidget, &QTableWidget::cellDoubleClicked,
            this, &ModBlacklistPage::onWhitelistCellDoubleClicked);
    connect(ui->whitelistTableWidget, &QTableWidget::cellChanged,
            this, &ModBlacklistPage::onWhitelistCellChanged);

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
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &ModBlacklistPage::updateDropHintVisibility);
}

void ModBlacklistPage::loadList(QTableWidget *tableWidget, const QMap<int, QString> &modList)
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

void ModBlacklistPage::addModToTable(QTableWidget *tableWidget, int projectId, const QString &name)
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

void ModBlacklistPage::loadBlacklist()
{
    loadList(ui->blacklistTableWidget, APPLICATION->getModBlacklist());
}

void ModBlacklistPage::loadWhitelist()
{
    loadList(ui->whitelistTableWidget, APPLICATION->getModWhitelist());
}

void ModBlacklistPage::refreshData()
{
    loadBlacklist();
    loadWhitelist();
    updateDropHintVisibility();
}

void ModBlacklistPage::onBlacklistCellDoubleClicked(int row, int column)
{
    handleCellDoubleClick(ui->blacklistTableWidget, row, column);
}

void ModBlacklistPage::onWhitelistCellDoubleClicked(int row, int column)
{
    handleCellDoubleClick(ui->whitelistTableWidget, row, column);
}

void ModBlacklistPage::handleCellDoubleClick(QTableWidget *tableWidget, int row, int column)
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

void ModBlacklistPage::onBlacklistCellChanged(int row, int column)
{
    handleCellChanged(ui->blacklistTableWidget, row, column, false);
}

void ModBlacklistPage::onWhitelistCellChanged(int row, int column)
{
    handleCellChanged(ui->whitelistTableWidget, row, column, true);
}

void ModBlacklistPage::handleCellChanged(QTableWidget *tableWidget, int row, int column, bool isWhitelist)
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

void ModBlacklistPage::removeSelectedMods(QTableWidget *tableWidget, bool isWhitelist)
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
    int removedCount = 0;
    for (int projectId : projectIdsToRemove)
    {
        bool success = isWhitelist
                           ? APPLICATION->removeModFromWhitelist(projectId)
                           : APPLICATION->removeModFromBlacklist(projectId);

        if (success)
        {
            ++removedCount;
        }
    }

    if (removedCount > 0)
    {
        refreshData();
        showInfoMessage(tr("Success"),
                        tr("Removed %1 mod(s) from %2").arg(removedCount).arg(isWhitelist ? tr("whitelist") : tr("blacklist")));
    }
}

void ModBlacklistPage::showErrorMessage(const QString &title, const QString &message)
{
    QMessageBox::warning(this, title, message);
}

void ModBlacklistPage::showInfoMessage(const QString &title, const QString &message)
{
    QMessageBox::information(this, title, message);
}

void ModBlacklistPage::updateDropHintVisibility()
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

bool ModBlacklistPage::eventFilter(QObject *watched, QEvent *event)
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

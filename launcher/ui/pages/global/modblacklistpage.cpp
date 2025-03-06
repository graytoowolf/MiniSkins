#include "modblacklistpage.h"
#include "ui_modblacklistpage.h"
#include "Application.h"
#include "minecraft/mod/fingerprint.h"

ModBlacklistPage::ModBlacklistPage(QWidget *parent) : QWidget(parent),
                                                      ui(new Ui::ModBlacklistPage)
{
    ui->setupUi(this);
    setupUi();
    loadBlacklist();
    // 启用拖放
    setAcceptDrops(true);
}

ModBlacklistPage::~ModBlacklistPage()
{
    delete ui;
}

bool ModBlacklistPage::isSupportedFile(const QString &filePath) const
{
    for (const QString &ext : m_supportedExtensions)
    {
        if (filePath.endsWith(ext, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    return false;
}

void ModBlacklistPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl &url : urls)
        {
            if (isSupportedFile(url.toLocalFile()))
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void ModBlacklistPage::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    QStringList fingerprints;

    for (const QUrl &url : urls)
    {
        QString filePath = url.toLocalFile();
        if (isSupportedFile(filePath))
        {
            QString actualPath = filePath;
            QString hash = fingerprint::getJarFingerprint(actualPath);
            if (!hash.isEmpty())
            {
                fingerprints.append(hash);
            }
        }
    }

    if (!fingerprints.isEmpty())
    {
        requestModInfo(fingerprints);
    }

    event->acceptProposedAction();
}

void ModBlacklistPage::setupUi()
{
    // 设置表格属性
    ui->tableWidget->setColumnCount(3);
    QStringList headers;
    headers << "" << tr("Project ID") << tr("Mod name"); // 复选框列不设置表头
    ui->tableWidget->setHorizontalHeaderLabels(headers);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->tableWidget->setColumnWidth(0, 20);               // 设置复选框列宽度为20
    ui->tableWidget->verticalHeader()->setVisible(false); // 隐藏序号
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->setSelectionMode(QAbstractItemView::ContiguousSelection);
    ui->tableWidget->setAlternatingRowColors(true);

    // 连接信号槽
    connect(ui->tableWidget, &QTableWidget::cellDoubleClicked,
            this, &ModBlacklistPage::onCellDoubleClicked);
    connect(ui->tableWidget, &QTableWidget::cellChanged,
            this, &ModBlacklistPage::onCellChanged);
}

void ModBlacklistPage::loadBlacklist()
{
    ui->tableWidget->blockSignals(true);
    ui->tableWidget->setRowCount(0);

    const QMap<int, QString> &blacklist = APPLICATION->getModBlacklist();

    for (auto it = blacklist.constBegin(); it != blacklist.constEnd(); ++it)
    {
        int projectId = it.key();
        QString name = it.value();

        int row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);

        // 添加复选框（默认不选中）
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setBackground(ui->tableWidget->palette().base());
        ui->tableWidget->setItem(row, 0, checkItem);

        // Project ID (不可编辑)
        QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(projectId));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        ui->tableWidget->setItem(row, 1, idItem);

        // Mod名称 (可编辑)
        ui->tableWidget->setItem(row, 2, new QTableWidgetItem(name));
    }

    ui->tableWidget->blockSignals(false);
}

void ModBlacklistPage::refreshData()
{
    loadBlacklist();
}

void ModBlacklistPage::requestModInfo(const QStringList &fingerprints)
{
    // 构建请求JSON
    QJsonObject requestObj;
    QJsonArray fingerprintArray;

    // 直接将指纹数组添加到JSON数组
    for (const QString &fp : fingerprints)
    {
        bool ok;
        qlonglong fingerprint = fp.toLongLong(&ok);
        if(ok)
        {
            fingerprintArray.append(QJsonValue(fingerprint));
        }
    }

    requestObj["fingerprints"] = fingerprintArray;
    QJsonDocument doc(requestObj);
    QByteArray data = doc.toJson();

    // 创建网络请求
    QNetworkRequest request(QUrl("https://api.curseforge.com/v1/fingerprints"));
    request.setRawHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // 发送POST请求
    auto reply = APPLICATION->network()->post(request, data);

    connect(reply, &QNetworkReply::finished, this, [this, reply]()
            {
        reply->deleteLater();

        if(reply->error() != QNetworkReply::NoError) {
            qWarning() << "Fingerprint lookup failed:" << reply->errorString();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        if(root.contains("data")) {
            QJsonObject data = root["data"].toObject();
            if(data.contains("exactMatches")) {
                QJsonArray matches = data["exactMatches"].toArray();

                // 处理所有匹配的mod
                for(const QJsonValue &matchValue : matches) {
                    QJsonObject match = matchValue.toObject();
                    int projectId = match["id"].toInt();

                    QString name = "Provisional Name"; // 临时名称

                    // 添加到黑名单,使用新的Map接口
                    APPLICATION->addModToBlacklist(projectId, name);
                }

                // 刷新显示
                loadBlacklist();
            }
        } });
}

void ModBlacklistPage::onCellDoubleClicked(int row, int column)
{
    if (column != 2)
    {
        QTableWidgetItem *checkItem = ui->tableWidget->item(row, 0);
        if (checkItem)
        {
            // 切换复选框状态
            bool checked = checkItem->checkState() == Qt::Checked;
            checkItem->setCheckState(checked ? Qt::Unchecked : Qt::Checked);
        }
    }
}

void ModBlacklistPage::onCellChanged(int row, int column)
{
    if (column == 2)
    { // 只处理Mod名称列的修改
        bool ok;
        int projectId = ui->tableWidget->item(row, 1)->text().toInt(&ok);
        if (!ok)
            return;

        QString newName = ui->tableWidget->item(row, 2)->text();
        APPLICATION->updateModBlacklistName(projectId, newName);
    }
}

void ModBlacklistPage::on_removeButton_clicked()
{
    // 从后向前遍历以避免删除行时索引变化的问题
    for (int row = ui->tableWidget->rowCount() - 1; row >= 0; row--)
    {
        QTableWidgetItem *checkItem = ui->tableWidget->item(row, 0);
        if (checkItem && checkItem->checkState() == Qt::Checked)
        {
            bool ok;
            int projectId = ui->tableWidget->item(row, 1)->text().toInt(&ok);
            if (!ok)
                continue;

            // 从黑名单中删除,使用新的Map接口
            if (APPLICATION->removeModFromBlacklist(projectId))
            {
                ui->tableWidget->removeRow(row);
            }
        }
    }
}

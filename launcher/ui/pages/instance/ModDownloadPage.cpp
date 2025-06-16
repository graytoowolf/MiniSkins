#include "ModDownloadPage.h"
#include "ui_ModDownloadPage.h"
#include <QTimer>
#include <QScrollBar>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QIcon>
#include <QDir>
#include "net/NetJob.h"
#include "net/Download.h"
#include "minecraft/mod/Mod.h"
#include <memory>
#include "minecraft/mod/Mod.h"

using fingerprint::ModInfo;

namespace
{
    const QString CURSEFORGE_API_V1_BASE = "https://api.curseforge.com/v1";
}

QMap<QString, QIcon> ModDownloadPage::logoCache;
QSet<int> ModDownloadPage::processedDependencies;
// 为Qt 5.7之前的版本添加QOverload支持
#if QT_VERSION < QT_VERSION_CHECK(5, 7, 0)
template <typename... Args>
struct QOverload
{
    template <typename R, typename T>
    static constexpr auto of(R (T::*pmf)(Args...)) -> decltype(pmf)
    {
        return pmf;
    }
};
#endif

ModDownloadPage::ModDownloadPage(MinecraftInstance *inst, QWidget *parent) : QMainWindow(parent),
                                                                             ui(new Ui::ModDownloadPage),
                                                                             m_inst(inst),
                                                                             m_currentPage(0),
                                                                             m_isLoading(false),
                                                                             m_hasMoreMods(true)
{
    ui->setupUi(this);
    m_profile = m_inst->getPackProfile();

    // 初始化UI
    initUI();

    // 获取游戏信息
    getGameInfoByIteration();

    // 加载第一页数据
    QTimer::singleShot(500, this, &ModDownloadPage::loadInitialMods);
}

ModDownloadPage::~ModDownloadPage()
{
    cleanup();
    delete ui;
}

bool ModDownloadPage::apply()
{
    cleanup();
    return true;
}

void ModDownloadPage::cleanup()
{
    // 1. 停止所有正在进行的网络请求
    if (m_network)
    {
        disconnect(m_network, nullptr, this, nullptr);
    }

    // 2. 清理所有正在运行的NetJob任务
    QList<NetJob *> jobs = this->findChildren<NetJob *>();
    for (NetJob *job : jobs)
    {
        disconnect(job, nullptr, this, nullptr);
        job->abort();
        job->deleteLater();
    }

    // 3. 清理UI中的所有定时器
    QList<QTimer *> timers = this->findChildren<QTimer *>();
    for (QTimer *timer : timers)
    {
        timer->stop();
        disconnect(timer, nullptr, this, nullptr);
    }

    // 4. 清理模组列表
    clearModList();

    // 5. 断开UI控件的信号连接
    if (ui)
    {
        if (ui->searchButton)
            disconnect(ui->searchButton, nullptr, this, nullptr);
        if (ui->searchEdit)
            disconnect(ui->searchEdit, nullptr, this, nullptr);
        if (ui->sortCombo)
            disconnect(ui->sortCombo, nullptr, this, nullptr);
        if (ui->scrollArea && ui->scrollArea->verticalScrollBar())
            disconnect(ui->scrollArea->verticalScrollBar(), nullptr, this, nullptr);
    }

    // 6. 重置状态变量
    m_isLoading = false;
    m_hasMoreMods = false;
    m_currentPage = 0;
}

void ModDownloadPage::initUI()
{
    // 连接搜索按钮信号
    connect(ui->searchButton, &QPushButton::clicked, this, &ModDownloadPage::onSearch);

    // 连接回车键搜索
    connect(ui->searchEdit, &QLineEdit::returnPressed, this, &ModDownloadPage::onSearch);

    // 连接分类和排序下拉框信号
    connect(ui->sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ModDownloadPage::onFilterChanged);

    // 连接滚动条信号以实现懒加载
    connect(ui->scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, &ModDownloadPage::onScroll);
}

void ModDownloadPage::getGameInfoByIteration()
{
    if (!m_profile)
    {
        qWarning() << "Instance is null, cannot get game info";
        return;
    }

    // 遍历所有组件
    int componentCount = m_profile->rowCount();

    for (int i = 0; i < componentCount; ++i)
    {
        Component *component = m_profile->getComponent(i);
        if (!component)
            continue;

        QString componentId = component->getName();
        QString componentVersion = component->getVersion();
        if (componentId.contains("minecraft", Qt::CaseInsensitive))
        {
            m_gameVersion = componentVersion;
        }
        else
        {
            m_modLoader = componentId;
        }
    }
}

void ModDownloadPage::loadInitialMods()
{
    // 初始化页面状态
    m_currentPage = 0;
    m_isLoading = false;
    m_hasMoreMods = true;

    // 创建网络管理器（如果尚未创建）
    if (!m_network)
    {
        m_network = APPLICATION->network().get();
    }

    // 加载第一页模组
    loadMoreMods();
}

int ModDownloadPage::getModLoaderTypeApiId()
{
    if (!m_modLoader.isEmpty())
    {
        if (m_modLoader.startsWith("forge", Qt::CaseInsensitive))
        {
            return 1; // Forge
        }
        else if (m_modLoader.startsWith("fabric", Qt::CaseInsensitive))
        {
            return 4; // Fabric
        }
        else if (m_modLoader.startsWith("neoforge", Qt::CaseInsensitive))
        {
            return 6; // NeoForge
        }
        else if (m_modLoader.startsWith("quilt", Qt::CaseInsensitive))
        {
            return 5; // Quilt
        }
    }
    return 0;
}

void ModDownloadPage::loadMoreMods()
{
    if (m_isLoading || !m_hasMoreMods)
    {
        return;
    }

    m_isLoading = true;

    // 添加加载指示器
    QLabel *loadingLabel = new QLabel(tr("Loading more mods..."));
    loadingLabel->setAlignment(Qt::AlignCenter);
    loadingLabel->setStyleSheet("font-size: 12px; color: #999999; padding: 10px;");
    ui->scrollLayout->addWidget(loadingLabel);

    // 构建CurseForge API请求
    QString searchText = ui->searchEdit->text().trimmed();
    QString sortBy = ui->sortCombo->currentText();

    // 确定排序字段和顺序
    int sortField = 1; // 默认为流行度
    QString sortOrder = "desc";

    if (sortBy == tr("Downloads"))
    {
        sortField = 2; // 下载量
    }
    else if (sortBy == tr("Last Updated"))
    {
        sortField = 3; // 最后更新
    }
    else if (sortBy == tr("Name"))
    {
        sortField = 4; // 名称
        sortOrder = "asc";
    }

    // 构建API URL
    QString apiUrl = QString(
                         "%1/mods/search?"
                         "gameId=432&"
                         "classId=6&"
                         "index=%2&"
                         "pageSize=20&"
                         "searchFilter=%3&"
                         "sortField=%4&"
                         "sortOrder=%5&"
                         "categoryId=0")
                         .arg(CURSEFORGE_API_V1_BASE)
                         .arg(m_currentPage * 20)
                         .arg(searchText)
                         .arg(sortField)
                         .arg(sortOrder);

    // 添加游戏版本过滤器（如果有）
    if (!m_gameVersion.isEmpty())
    {
        apiUrl += QString("&gameVersion=%1").arg(m_gameVersion);
    }

    // 添加模组加载器过滤器（如果指定）
    if (getModLoaderTypeApiId() > 0)
    {
        apiUrl += QString("&modLoaderType=%1").arg(getModLoaderTypeApiId());
    }

    // 创建NetJob网络请求
    NetJob *job = new NetJob(QString("ModSearch-%1").arg(m_currentPage), APPLICATION->network());
    QByteArray *responseData = new QByteArray();

    auto download = Net::Download::makeByteArray(QUrl(apiUrl), responseData);
    download->setExtraHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
    job->addNetAction(download);

    // 处理响应
    connect(job, &NetJob::succeeded, this, [this, job, loadingLabel, responseData]()
            {
        // 移除加载指示器
        ui->scrollLayout->removeWidget(loadingLabel);
        delete loadingLabel;

        m_isLoading = false;

        // 检查响应数据
        if (!responseData || responseData->isEmpty()) {
            delete responseData;
            job->deleteLater();
            return;
        }
        job->deleteLater();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(*responseData, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            QMessageBox::warning(this, tr("Parse Error"), tr("Unable to parse CurseForge response: %1").arg(parseError.errorString()));
            delete responseData;
            job->deleteLater();
            return;
        }

        // 处理模组数据
        QJsonObject rootObj = doc.object();
        QJsonArray modsArray = rootObj["data"].toArray();

        // 检查是否还有更多模组
        m_hasMoreMods = modsArray.size() >= 20;

        // 添加模组到UI
        for (const QJsonValue &modValue : modsArray) {
            QJsonObject modObj = modValue.toObject();

            ModDownloadInfo mod;
            mod.name = modObj["name"].toString();
            mod.description = modObj["summary"].toString();
            mod.modId = modObj["id"].toInt();

            // 处理作者信息
            QJsonArray authors = modObj["authors"].toArray();
            QStringList authorNames;
            for (const QJsonValue &authorValue : authors) {
                authorNames.append(authorValue.toObject()["name"].toString());
            }
            mod.author = authorNames.join(", ");

            // 处理下载量
            qint64 downloads = modObj["downloadCount"].toVariant().toLongLong();
            if (downloads > 1000000) {
                mod.downloads = QString("%1M").arg(downloads / 1000000.0, 0, 'f', 1);
            } else if (downloads > 1000) {
                mod.downloads = QString("%1K").arg(downloads / 1000.0, 0, 'f', 1);
            } else {
                mod.downloads = QString::number(downloads);
            }

            // 处理更新时间
            QString dateUpdated = modObj["dateModified"].toString();
            QDateTime updateTime = QDateTime::fromString(dateUpdated, Qt::ISODate);
            QDateTime now = QDateTime::currentDateTime();
            qint64 secsAgo = updateTime.secsTo(now);

            if (secsAgo < 3600) {
                mod.updateTime = tr("%1 minutes ago").arg(secsAgo / 60);
            } else if (secsAgo < 86400) {
                mod.updateTime = tr("%1 hours ago").arg(secsAgo / 3600);
            } else if (secsAgo < 2592000) {
                mod.updateTime = tr("%1 days ago").arg(secsAgo / 86400);
            } else {
                mod.updateTime = updateTime.toString("yyyy-MM-dd");
            }

            // 处理分类
            QJsonArray categories = modObj["categories"].toArray();
            if (!categories.isEmpty()) {
                mod.category = categories.first().toObject()["name"].toString();
            } else {
                mod.category = tr("Other");
            }

            // 生成图标信息
            mod.iconLetter = mod.name.left(1).toUpper();

            // 生成随机但一致的颜色（基于模组名称）
            int hash = 0;
            for (const QChar &c : mod.name) {
                hash = hash * 31 + c.unicode();
            }
            QList<QString> colors = {"#4CAF50", "#2196F3", "#FF5722", "#9C27B0", "#607D8B",
                                     "#795548", "#FF9800", "#E91E63", "#3F51B5", "#009688"};
            mod.iconColor = colors[qAbs(hash) % colors.size()];

            // 获取logo URL
            QString logoFileName;
            if (modObj.contains("logo") && !modObj["logo"].isNull()) {
                QJsonObject logoObj = modObj["logo"].toObject();

                // 获取logo文件名
                if (logoObj.contains("title") && !logoObj["title"].toString().isEmpty()) {
                    logoFileName = logoObj["title"].toString();
                    // 移除文件扩展名，只保留基础名称
                    logoFileName = logoFileName.section(".", 0, -2);
                }

                // 优先使用 thumbnailUrl，如果没有则使用 url
                if (logoObj.contains("thumbnailUrl") && !logoObj["thumbnailUrl"].toString().isEmpty()) {
                    mod.logoUrl = logoObj["thumbnailUrl"].toString();
                } else if (logoObj.contains("url") && !logoObj["url"].toString().isEmpty()) {
                    mod.logoUrl = logoObj["url"].toString();
                }
            }
            mod.logoFileName = logoFileName;

            // 创建并添加模组项目
            QWidget *modWidget = createModItemWidget(mod);
            ui->scrollLayout->addWidget(modWidget);
        }

        // 添加弹性空间
        if (ui->scrollLayout->count() > 0) {
            // 移除现有的弹性空间（如果有）
            QLayoutItem *lastItem = ui->scrollLayout->itemAt(ui->scrollLayout->count() - 1);
            if (lastItem && lastItem->spacerItem()) {
                ui->scrollLayout->removeItem(lastItem);
                delete lastItem;
            }
        }
        ui->scrollLayout->addStretch();

        // 增加页码
        m_currentPage++;

        // 清理内存
        delete responseData;
        job->deleteLater(); });

    // 处理失败情况
    connect(job, &NetJob::failed, this, [this, job, loadingLabel, responseData](QString reason)
            {
        // 移除加载指示器
        ui->scrollLayout->removeWidget(loadingLabel);
        delete loadingLabel;

        m_isLoading = false;

        QMessageBox::warning(this, tr("Load Failed"), tr("Unable to load mods from CurseForge: %1").arg(reason));

        // 清理内存
        delete responseData;
        job->deleteLater(); });

    // 启动网络请求
    job->start();
}

QWidget *ModDownloadPage::createModItemWidget(const ModDownloadPage::ModDownloadInfo &modInfo)
{
    QWidget *modItemWidget = new QWidget();
    modItemWidget->setObjectName("modItemWidget");
    modItemWidget->setMinimumSize(600, 90); // 增加高度以适应新的样式
    modItemWidget->setMaximumSize(16777215, 110);

    // 添加卡片样式 - 背景色、边框和圆角
    modItemWidget->setStyleSheet(
        "QWidget#modItemWidget {"
        "    background-color: palette(base);"
        "    border: 1px solid palette(mid);"
        "    border-radius: 8px;"
        "    margin: 2px;"
        "}"
        "QWidget#modItemWidget:hover {"
        "    border: 2px solid palette(highlight);"
        "    background-color: palette(alternate-base);"
        "}");

    QHBoxLayout *modItemLayout = new QHBoxLayout(modItemWidget);
    modItemLayout->setContentsMargins(12, 10, 12, 10); // 增加内边距
    modItemLayout->setSpacing(15);                     // 增加元素间距

    // 图标框架
    QFrame *iconFrame = new QFrame();
    iconFrame->setObjectName("iconFrame");
    iconFrame->setMinimumSize(68, 68); // 稍微增大图标框架
    iconFrame->setMaximumSize(68, 68);
    iconFrame->setFrameShape(QFrame::NoFrame);
    iconFrame->setStyleSheet(QString(
                                 "QFrame#iconFrame {"
                                 "    background-color: %1;"
                                 "    border: 2px solid palette(light);"
                                 "    border-radius: 10px;"
                                 "    margin: 2px;"
                                 "}"
                                 "QFrame#iconFrame:hover {"
                                 "    border: 2px solid palette(highlight);"
                                 "}")
                                 .arg(modInfo.iconColor));

    QVBoxLayout *iconLayout = new QVBoxLayout(iconFrame);
    iconLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("iconLabel");
    iconLabel->setAlignment(Qt::AlignCenter);

    // 如果有logo URL，则下载并显示logo，否则显示字母
    if (!modInfo.logoUrl.isEmpty())
    {

        // 获取缓存条目
        MetaEntryPtr entry = APPLICATION->metacache()->resolveEntry("CurseForgePacks", QString("logos/%1").arg(modInfo.logoFileName));
        QString cachedFilePath = entry->getFullPath();
        // 检查缓存文件是否存在且有效
        QFileInfo cacheFileInfo(cachedFilePath);
        bool cacheExists = cacheFileInfo.exists() && cacheFileInfo.size() > 0;

        if (cacheExists)
        {
            // 缓存文件存在，先检查内存缓存
            if (logoCache.contains(modInfo.logoFileName))
            {
                // 内存中有缓存，直接使用
                iconLabel->setPixmap(logoCache[modInfo.logoFileName].pixmap(68, 68));
            }
            else
            {
                // 内存中没有，从磁盘加载
                QIcon icon(cachedFilePath);
                if (!icon.isNull())
                {
                    // 加载成功，添加到内存缓存并显示
                    logoCache[modInfo.logoFileName] = icon;
                    iconLabel->setPixmap(icon.pixmap(68, 68));
                }
                else
                {
                    // 缓存文件损坏，删除并重新下载
                    QFile::remove(cachedFilePath);
                    iconLabel->setText(modInfo.iconLetter);
                    downloadLogo(modInfo, iconLabel, entry);
                }
            }
        }
        else
        {
            // 缓存文件不存在，显示默认图标并开始下载
            iconLabel->setText(modInfo.iconLetter);
            downloadLogo(modInfo, iconLabel, entry);
        }
    }
    else
    {
        iconLabel->setText(modInfo.iconLetter);
    }

    iconLayout->addWidget(iconLabel);

    // 内容布局
    QVBoxLayout *contentLayout = new QVBoxLayout();
    contentLayout->setSpacing(4); // 增加间距

    // 标题布局
    QHBoxLayout *titleLayout = new QHBoxLayout();

    QLabel *titleLabel = new QLabel(modInfo.name);
    titleLabel->setObjectName("titleLabel");
    titleLabel->setStyleSheet(
        "QLabel#titleLabel {"
        "    font-size: 14px;"
        "    font-weight: bold;"
        "    color: palette(window-text);"
        "    background-color: transparent;"
        "    margin-bottom: 2px;"
        "}");

    QLabel *authorLabel = new QLabel(QString("Author: %1").arg(modInfo.author));
    authorLabel->setObjectName("authorLabel");
    authorLabel->setStyleSheet(
        "QLabel#authorLabel {"
        "    font-size: 11px;"
        "    color: palette(mid);"
        "    background-color: transparent;"
        "    font-style: italic;"
        "}");

    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(authorLabel);
    titleLayout->addStretch();

    // 描述标签 - 允许换行
    QLabel *descriptionLabel = new QLabel();
    descriptionLabel->setObjectName("descriptionLabel");
    descriptionLabel->setWordWrap(false);                             // 禁止换行
    descriptionLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter); // 垂直居中可选
    descriptionLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    descriptionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse); // 可选：允许选中
    descriptionLabel->setToolTip(modInfo.description);                    // 鼠标悬停显示完整文本
    descriptionLabel->setStyleSheet(
        "QLabel#descriptionLabel {"
        "    font-size: 12px;"
        "    color: palette(window-text);"
        "    background-color: transparent;"
        "    margin: 4px 2px;"
        "    padding: 4px 2px;"
        "    min-height: 16px;"
        "}");
    // 使用fontMetrics().elidedText()设置省略文本
    QString elidedText = descriptionLabel->fontMetrics().elidedText(modInfo.description, Qt::ElideRight, 400);
    descriptionLabel->setText(elidedText);

    // 统计信息布局
    QHBoxLayout *statsLayout = new QHBoxLayout();
    statsLayout->setSpacing(8);

    QLabel *downloadLabel = new QLabel(QString("⬇ %1").arg(modInfo.downloads));
    downloadLabel->setStyleSheet(
        "font-size: 11px;"
        "color: palette(window-text);"
        "background-color: palette(light);"
        "border-radius: 3px;"
        "padding: 2px 6px;"
        "margin: 1px;");

    QLabel *timeLabel = new QLabel(QString("🕒 %1").arg(modInfo.updateTime));
    timeLabel->setStyleSheet(
        "font-size: 11px;"
        "color: palette(window-text);"
        "background-color: palette(light);"
        "border-radius: 3px;"
        "padding: 2px 6px;"
        "margin: 1px;");

    QLabel *categoryLabel = new QLabel(QString("🏷️ %1").arg(modInfo.category));
    categoryLabel->setStyleSheet(
        "font-size: 11px;"
        "color: palette(window-text);"
        "background-color: palette(light);"
        "border-radius: 3px;"
        "padding: 2px 6px;"
        "margin: 1px;");

    statsLayout->addWidget(downloadLabel);
    statsLayout->addWidget(timeLabel);
    statsLayout->addWidget(categoryLabel);
    statsLayout->addStretch();

    // 创建进度条（初始隐藏）
    QProgressBar *progressBar = new QProgressBar();
    progressBar->setMinimum(0);
    progressBar->setMaximum(100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    progressBar->setFormat("%p%");
    progressBar->setFixedHeight(16);
    progressBar->setStyleSheet(
        "QProgressBar {"
        "    background-color: palette(base);"
        "    border: 1px solid palette(mid);"
        "    border-radius: 8px;"
        "    text-align: center;"
        "    font-size: 11px;"
        "    color: palette(window-text);"
        "}"
        "QProgressBar::chunk {"
        "    background-color: palette(highlight);"
        "    border-radius: 6px;"
        "    margin: 1px;"
        "}");
    progressBar->hide();

    contentLayout->addLayout(titleLayout);
    contentLayout->addWidget(descriptionLabel);
    contentLayout->addLayout(statsLayout);
    contentLayout->addWidget(progressBar);

    // 按钮布局
    QVBoxLayout *buttonLayout = new QVBoxLayout();

    QPushButton *installButton = new QPushButton(tr("Install"));
    installButton->setObjectName("installButton");
    installButton->setMinimumSize(90, 32); // 增加按钮尺寸
    installButton->setFixedWidth(90);      // 固定宽度

    // 设置按钮基础样式
    installButton->setStyleSheet(
        "QPushButton#installButton {"
        "    background-color: palette(highlight);"
        "    color: palette(highlighted-text);"
        "    border: 1px solid palette(highlight);"
        "    border-radius: 6px;"
        "    font-weight: bold;"
        "    font-size: 12px;"
        "    padding: 4px 8px;"
        "}"
        "QPushButton#installButton:hover {"
        "    background-color: palette(light);"
        "    color: palette(dark);"
        "    border: 2px solid palette(highlight);"
        "}"
        "QPushButton#installButton:pressed {"
        "    background-color: palette(dark);"
        "    color: palette(bright-text);"
        "}"
        "QPushButton#installButton:disabled {"
        "    background-color: palette(mid);"
        "    color: palette(dark);"
        "    border: 1px solid palette(mid);"
        "}");

    // 连接按钮信号
    connect(installButton, &QPushButton::clicked, this, [this, modInfo, progressBar, statsLayout]()
            {
        // 清空下载队列
        m_downloadQueue.clear();
        processedDependencies.clear();
        if (isModInstalled(modInfo.modId)){
            qDebug()<<"mod已经存在:"<<modInfo.modId;
            QMessageBox::information(this, tr("Mod Already Installed"),
                                   tr("The mod '%1' is already installed.").arg(modInfo.name));
            return ;
        }

        // 构建下载队列（包含依赖）
        buildDownloadQueue(modInfo.modId, [this, progressBar, statsLayout]() {
            // 队列构建完成，开始处理下载
            if (m_downloadQueue.isEmpty()) {
                QMessageBox::warning(this, tr("Download Error"),
                                     tr("Unable to get mod download information, please try again later."));
                return;
            }

            // 开始处理下载队列
            processDownloadQueue(progressBar, statsLayout);
        }); });

    buttonLayout->addWidget(installButton);
    buttonLayout->addStretch();

    // 组装布局
    modItemLayout->addWidget(iconFrame);
    modItemLayout->addLayout(contentLayout, 1); // 给内容区域分配更多空间
    modItemLayout->addLayout(buttonLayout);

    return modItemWidget;
}

bool ModDownloadPage::isModInstalled(int modId)
{
    // 直接调用Mod类的静态函数
    QString modJsonPath = m_inst->modlist();
    QString modsRoot = m_inst->modsRoot();

    return Mod::isModInstalled(modJsonPath, modId, modsRoot);
}

void ModDownloadPage::fetchModDownloadInfo(int modId, std::function<void(const DownloadItem &)> callback)
{
    QString url = QString("%1/mods/%2/files").arg(CURSEFORGE_API_V1_BASE).arg(modId);

    NetJob *netJob = new NetJob(QString("CurseForge::ModDownloadInfo(%1)").arg(modId), APPLICATION->network());
    std::shared_ptr<QByteArray> response = std::make_shared<QByteArray>();
    auto download = Net::Download::makeByteArray(QUrl(url), response.get());
    download->setExtraHeader("x-api-key", APPLICATION->curseAPIKey());
    netJob->addNetAction(download);

    connect(netJob, &NetJob::succeeded, this, [this, response, modId, callback]()
            {
        QJsonParseError parse_error;
        QJsonDocument doc = QJsonDocument::fromJson(*response, &parse_error);
        if (parse_error.error != QJsonParseError::NoError) {
            qDebug() << "Failed to parse mod download info JSON:" << parse_error.errorString();
            callback(DownloadItem());
            return;
        }

        QJsonObject rootObj = doc.object();
        QJsonArray filesArray = rootObj["data"].toArray();

        // 查找匹配当前游戏版本和加载器的文件
        for (const QJsonValue &fileValue : filesArray) {
            QJsonObject fileObj = fileValue.toObject();
            QJsonArray gameVersions = fileObj["gameVersions"].toArray();

            bool matchesVersion = false;
            bool matchesLoader = false;

            for (const QJsonValue &version : gameVersions) {
                QString versionStr = version.toString();

                if (versionStr == m_gameVersion) {
                    matchesVersion = true;
                }

                if (versionStr.contains(m_modLoader, Qt::CaseInsensitive)) {
                    matchesLoader = true;
                }
            }

            if (matchesVersion && matchesLoader) {
                DownloadItem item;
                item.downloadUrl = fileObj["downloadUrl"].toString();
                item.fileName = fileObj["fileName"].toString();
                item.fileID = fileObj["id"].toInt();
                item.modId = modId;

                // 获取依赖信息
                QJsonArray dependencies = fileObj["dependencies"].toArray();
                for (const QJsonValue &depValue : dependencies) {
                    QJsonObject depObj = depValue.toObject();
                    int relationType = depObj["relationType"].toInt();
                    if (relationType == 3) { // 3 = RequiredDependency
                        int depModId = depObj["modId"].toInt();
                        item.requiredDependencies.append(depModId);
                    }
                }
                callback(item);
                return;
            }
        }

        // 没有找到匹配的文件
        callback(DownloadItem()); });

    connect(netJob, &NetJob::failed, this, [callback](QString reason)
            {
        qDebug() << "Failed to fetch mod download info:" << reason;
        callback(DownloadItem()); });

    netJob->start();
}

void ModDownloadPage::buildDownloadQueue(int modId, std::function<void()> onComplete)
{
    if (processedDependencies.contains(modId))
    {
        onComplete();
        return;
    }

    processedDependencies.insert(modId);

    fetchModDownloadInfo(modId, [this, onComplete](const DownloadItem &item)
                         {
        if (item.downloadUrl.isEmpty()) {
            onComplete();
            return;
        }

        // 检查是否已安装
        if (!isModInstalled(item.modId)) {
            m_downloadQueue.append(item);
        }

        // 递归处理依赖
        if (item.requiredDependencies.isEmpty()) {
            onComplete();
        } else {
            auto pendingDepsPtr = std::make_shared<int>(item.requiredDependencies.size());
            for (int depId : item.requiredDependencies) {
                buildDownloadQueue(depId, [pendingDepsPtr, onComplete]() {
                    (*pendingDepsPtr)--;
                    if (*pendingDepsPtr == 0) {
                        onComplete();
                    }
                });
            }
        } });
}

void ModDownloadPage::processDownloadQueue(QProgressBar *progressBar, QHBoxLayout *statsLayout)
{
    if (m_downloadQueue.isEmpty())
    {
        QMessageBox::information(this, tr("Complete"), tr("All mods are already installed, no download needed."));
        return;
    }

    // 隐藏统计信息，显示进度条
    for (int i = 0; i < statsLayout->count(); i++)
    {
        QLayoutItem *item = statsLayout->itemAt(i);
        if (item && item->widget())
        {
            item->widget()->hide();
        }
    }
    progressBar->show();

    // 开始下载第一个项目
    downloadNextInQueue(progressBar, statsLayout, 0);
}

void ModDownloadPage::downloadNextInQueue(QProgressBar *progressBar, QHBoxLayout *statsLayout, int index)
{
    if (index >= m_downloadQueue.size())
    {
        // 所有下载完成，批量写入模组信息
        progressBar->hide();
        for (int i = 0; i < statsLayout->count(); i++)
        {
            QLayoutItem *item = statsLayout->itemAt(i);
            if (item && item->widget())
            {
                item->widget()->show();
            }
        }

        // 批量写入所有下载完成的模组信息
        if (!m_completedMods.isEmpty()) {
            QList<ModInfo> modInfoList;
            QString jsonPath = m_inst->modlist();

            for (const auto &modInfo : m_completedMods) {
                ModInfo jsonInfo;
                jsonInfo.projectId = modInfo.modId;
                jsonInfo.fileId = modInfo.fileID;
                jsonInfo.name = modInfo.name;
                modInfoList.append(jsonInfo);
            }

            Mod::addModsToJson(jsonPath, modInfoList, false);
            m_completedMods.clear();
        }

        QString message = tr("Successfully downloaded and installed %1 mods.").arg(m_downloadQueue.size());
        QMessageBox::information(this, tr("Installation Complete"), message);
        return;
    }

    const DownloadItem &item = m_downloadQueue[index];
    downloadSingleItem(item, progressBar, statsLayout, [this, progressBar, statsLayout, index]()
                       { downloadNextInQueue(progressBar, statsLayout, index + 1); });
}

void ModDownloadPage::downloadSingleItem(const DownloadItem &item, QProgressBar *progressBar, QHBoxLayout *statsLayout, std::function<void()> onComplete)
{
    // 创建模组文件夹（如果不存在）
    QString modsDir = m_inst->modsRoot();
    QDir dir(modsDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    QString filePath = modsDir + "/" + item.fileName;

    // 创建下载任务
    NetJob *job = new NetJob(QString("ModDownload-%1").arg(item.fileName), APPLICATION->network());

    // 创建下载
    auto download = Net::Download::makeFile(QUrl(item.downloadUrl), filePath);

    // 连接进度信号
    connect(job, &Task::progress, this, [progressBar](qint64 current, qint64 total)
            {
        if (total > 0) {
            progressBar->setValue((int)((float)current / total * 100));
        } });

    // 添加下载到任务
    job->addNetAction(download);

    // 连接任务完成信号
    connect(job, &NetJob::succeeded, this, [this, item, onComplete, job]()
            {
        // 将模组信息添加到完成列表，等待批量写入
        ModDownloadInfo modInfo;
        modInfo.modId = item.modId;
        modInfo.name = item.fileName;
        modInfo.fileID = item.fileID;
        m_completedMods.append(modInfo);

        job->deleteLater();
        onComplete(); });

    // 连接任务失败信号
    connect(job, &NetJob::failed, this, [this, item, progressBar, statsLayout, job, onComplete](QString reason)
            {
        progressBar->hide();
        for (int i = 0; i < statsLayout->count(); i++) {
            QLayoutItem *item = statsLayout->itemAt(i);
            if (item && item->widget()) {
                item->widget()->show();
            }
        }

        QMessageBox::warning(this, tr("Download Failed"),
                             tr("Mod %1 download failed: %2").arg(item.fileName).arg(reason));

        job->deleteLater();
        onComplete(); });

    // 启动下载
    job->start();
}

void ModDownloadPage::downloadLogo(const ModDownloadInfo &modInfo, QLabel *iconLabel, MetaEntryPtr entry)
{
    NetJob *job = new NetJob(QString("CurseForge Icon Download %1").arg(modInfo.logoFileName), APPLICATION->network());
    job->addNetAction(Net::Download::makeCached(QUrl(modInfo.logoUrl), entry));

    QObject::connect(job, &NetJob::succeeded, [iconLabel, modInfo, entry]()
                     {
        QString filePath = entry->getFullPath();

        // 验证下载的文件是否有效
        QFileInfo fileInfo(filePath);
        if (fileInfo.exists() && fileInfo.size() > 0)
        {
            QIcon icon(filePath);
            if (!icon.isNull())
            {
                // 文件有效，添加到内存缓存并显示
                logoCache[modInfo.logoFileName] = icon;
                iconLabel->setPixmap(icon.pixmap(64, 64));
            }
        } });

    QObject::connect(job, &NetJob::failed, [modInfo](QString reason) { /* Logo download failed, silently ignore */ });

    job->start();
}

void ModDownloadPage::clearModList()
{
    // 清除所有mod项目，但保留最后的弹性空间
    QLayoutItem *child;
    while ((child = ui->scrollLayout->takeAt(0)) != nullptr)
    {
        if (child->widget())
        {
            delete child->widget();
        }
        delete child;
    }
}

void ModDownloadPage::addModToJson(const ModDownloadInfo &modInfo)
{
    // 直接调用Mod类的静态函数
    QString jsonPath = m_inst->modlist();

    ModInfo modJsonInfo;
    modJsonInfo.projectId = modInfo.modId;
    modJsonInfo.fileId = modInfo.fileID;
    modJsonInfo.name = modInfo.name;

    QList<ModInfo> modInfoList;
    modInfoList.append(modJsonInfo);
    Mod::addModsToJson(jsonPath, modInfoList, true);
}

void ModDownloadPage::onSearch()
{
    // 获取搜索关键词
    QString searchText = ui->searchEdit->text().trimmed();

    // 清空当前列表并重新加载
    clearModList();
    m_currentPage = 0;
    m_hasMoreMods = true;
    loadMoreMods();
}

void ModDownloadPage::onFilterChanged()
{
    // 获取当前分类和排序方式
    QString sortBy = ui->sortCombo->currentText();

    // 清空当前列表并重新加载
    clearModList();
    m_currentPage = 0;
    m_hasMoreMods = true;
    loadMoreMods();
}

void ModDownloadPage::onScroll(int value)
{
    // 检查是否滚动到底部附近
    QScrollBar *scrollBar = ui->scrollArea->verticalScrollBar();
    if (!m_isLoading && m_hasMoreMods && value >= scrollBar->maximum() - 100)
    {
        loadMoreMods();
    }
}

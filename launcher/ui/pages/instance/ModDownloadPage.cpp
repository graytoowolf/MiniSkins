#include "ModDownloadPage.h"
#include "ui_ModDownloadPage.h"
#include "ModDownloadPageStyles.h"
#include "ModDownloadPageUIFactory.h"
#include <QTimer>
#include <QScrollBar>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QIcon>
#include <QDir>
#include <QUrl>
#include "net/NetJob.h"
#include "net/Download.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ModJsonManager.h"
#include <memory>

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

    // 初始化ModJsonManager
    QString modJsonPath = m_inst->modlist();
    m_modJsonManager.initialize(modJsonPath);

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
    // 保存并清除ModJsonManager
    m_modJsonManager.save();
    m_modJsonManager.clear();

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
                         "sortOrder=%5&")
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

            // 解析fileID - 从latestFilesIndexes中匹配gameVersion和modLoader
            QJsonArray latestFilesIndexes = modObj["latestFilesIndexes"].toArray();
            mod.fileID = 0; // 默认值
            for (const QJsonValue &fileValue : latestFilesIndexes) {
                QJsonObject fileObj = fileValue.toObject();
                QString fileGameVersion = fileObj["gameVersion"].toString();
                int fileModLoader = fileObj["modLoader"].toInt();

                // 完全匹配gameVersion和modLoader
                if (fileGameVersion == m_gameVersion && fileModLoader == getModLoaderTypeApiId()) {
                    mod.fileID = fileObj["fileId"].toInt();
                    break; // 取第一个完全匹配的
                }
            }

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

            // 获取模组安装状态
            mod.installStatus = getModInstallStatus(mod.modId, mod.fileID);

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
    // 使用UIFactory创建主容器
    QWidget *modItemWidget = ModDownloadPageUIFactory::createModItemWidget();
    QHBoxLayout *modItemLayout = ModDownloadPageUIFactory::createMainLayout(modItemWidget);

    // 使用UIFactory创建图标框架
    QFrame *iconFrame = ModDownloadPageUIFactory::createIconFrame();
    QVBoxLayout *iconLayout = ModDownloadPageUIFactory::createIconLayout(iconFrame);
    QLabel *iconLabel = ModDownloadPageUIFactory::createIconLabel();

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
                    iconLabel->setPixmap(modInfo.getDefaultIcon().pixmap(68, 68));
                    downloadLogo(modInfo, iconLabel, entry);
                }
            }
        }
        else
        {
            // 缓存文件不存在，显示默认图标并开始下载
            iconLabel->setPixmap(modInfo.getDefaultIcon().pixmap(68, 68));
            downloadLogo(modInfo, iconLabel, entry);
        }
    }
    else
    {
        iconLabel->setPixmap(modInfo.getDefaultIcon().pixmap(68, 68));
    }

    iconLayout->addWidget(iconLabel);

    // 使用UIFactory创建内容布局
    QVBoxLayout *contentLayout = ModDownloadPageUIFactory::createContentLayout();
    QHBoxLayout *titleLayout = ModDownloadPageUIFactory::createTitleLayout();

    QLabel *titleLabel = ModDownloadPageUIFactory::createTitleLabel(modInfo.name);
    QLabel *authorLabel = ModDownloadPageUIFactory::createAuthorLabel(QString("Author: %1").arg(modInfo.author));

    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(authorLabel);
    titleLayout->addStretch();

    // 使用UIFactory创建描述标签
    QLabel *descriptionLabel = ModDownloadPageUIFactory::createDescriptionLabel(modInfo.description);

    // 使用UIFactory创建统计信息布局
    QHBoxLayout *statsLayout = ModDownloadPageUIFactory::createStatsLayout();

    QLabel *downloadLabel = ModDownloadPageUIFactory::createStatsLabel(QString("⬇ %1").arg(modInfo.downloads));
    QLabel *timeLabel = ModDownloadPageUIFactory::createStatsLabel(QString("🕒 %1").arg(modInfo.updateTime));
    QLabel *categoryLabel = ModDownloadPageUIFactory::createStatsLabel(QString("🏷️ %1").arg(modInfo.category));

    statsLayout->addWidget(downloadLabel);
    statsLayout->addWidget(timeLabel);
    statsLayout->addWidget(categoryLabel);
    statsLayout->addStretch();

    // 使用UIFactory创建进度条
    QProgressBar *progressBar = ModDownloadPageUIFactory::createProgressBar();
    progressBar->hide();

    contentLayout->addLayout(titleLayout);
    contentLayout->addWidget(descriptionLabel);
    contentLayout->addLayout(statsLayout);
    contentLayout->addWidget(progressBar);

    // 使用UIFactory创建按钮布局
    QVBoxLayout *buttonLayout = ModDownloadPageUIFactory::createButtonLayout();
    QPushButton *installButton = ModDownloadPageUIFactory::createInstallButton(tr("Install"));

    // 根据mod安装状态设置按钮初始状态
    switch (modInfo.installStatus)
    {
    case ModDownloadPageUIFactory::MOD_INSTALLED:
        installButton->setText(tr("已安装"));
        installButton->setEnabled(false);
        break;
    case ModDownloadPageUIFactory::MOD_NEEDS_UPDATE:
        installButton->setText(tr("更新"));
        installButton->setEnabled(true);
        break;
    case ModDownloadPageUIFactory::MOD_NOT_INSTALLED:
    default:
        installButton->setText(tr("Install"));
        installButton->setEnabled(true);
        break;
    }

    // 连接按钮信号
    connect(installButton, &QPushButton::clicked, this, [this, modInfo, progressBar, statsLayout, installButton]()
            {
        // 获取当前mod安装状态
        ModInstallStatus status = getModInstallStatus(modInfo.modId, modInfo.fileID);

        if (status == ModDownloadPageUIFactory::MOD_INSTALLED) {
            qDebug()<<"mod已经存在:"<<modInfo.modId;
            return;
        }

        // 如果是更新状态，先删除原文件和JSON记录
        if (status == ModDownloadPageUIFactory::MOD_NEEDS_UPDATE) {
            QString modsRoot = m_inst->modsRoot();
            QString oldModName = m_modJsonManager.getModNameByProjectId(modInfo.modId);
            QString modFilePath = QDir(modsRoot).absoluteFilePath(oldModName);
            qDebug()<<"Updating mod, removing old file:"<<modFilePath;

            // 删除旧文件
            if (QFile::exists(modFilePath)) {
                QFile::remove(modFilePath);
            }

            // 从JSON记录中移除旧模组信息
            if (!oldModName.isEmpty()) {
                m_modJsonManager.removeMod(oldModName);
                m_modJsonManager.save();
            }
        }

        // 清空下载队列
        m_downloadQueue.clear();
        processedDependencies.clear();

        // 构建下载队列（包含依赖）
        buildDownloadQueue(modInfo.modId, [this, progressBar, statsLayout, installButton]() {
            // 队列构建完成，开始处理下载
            if (m_downloadQueue.isEmpty()) {
                QMessageBox::warning(this, tr("Download Error"),
                                     tr("Unable to get mod download information, please try again later."));
                return;
            }

            // 下载完成后设置按钮为已安装状态
            installButton->setText(tr("已安装"));
            installButton->setEnabled(false);

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

ModDownloadPage::ModInstallStatus ModDownloadPage::getModInstallStatus(int modId, int fileId)
{
    QString modsRoot = m_inst->modsRoot();

    // 检查JSON中是否存在该projectID的记录
    QString modName = m_modJsonManager.getModNameByProjectId(modId);
    if (modName.isEmpty())
    {
        // JSON中没有记录，说明未安装
        return ModDownloadPageUIFactory::MOD_NOT_INSTALLED;
    }

    // JSON中有记录，检查文件是否存在
    bool fileExists = m_modJsonManager.isModFileExistsByProjectId(modId, modsRoot);
    if (!fileExists)
    {
        // JSON中有记录但文件不存在，说明未安装（可能文件被手动删除）
        return ModDownloadPageUIFactory::MOD_NOT_INSTALLED;
    }

    // 文件存在，检查fileID是否匹配
    int existingFileId = m_modJsonManager.getModFileIdByProjectId(modId);
    if (existingFileId == fileId)
    {
        // modID相同、文件存在、fileID相同，说明已安装
        return ModDownloadPageUIFactory::MOD_INSTALLED;
    }
    else
    {
        // modID相同、文件存在、fileID不同，说明需要更新
        return ModDownloadPageUIFactory::MOD_NEEDS_UPDATE;
    }
}

void ModDownloadPage::fetchModDownloadInfo(int modId, std::function<void(const DownloadItem &)> callback)
{
    QString url = QString("%1/mods/%2/files?pageSize=1")
                      .arg(CURSEFORGE_API_V1_BASE)
                      .arg(modId);

    // 添加游戏版本参数（如果有）
    if (!m_gameVersion.isEmpty())
    {
        url += QString("&gameVersion=%1").arg(QUrl::toPercentEncoding(m_gameVersion).constData());
    }

    // 添加模组加载器类型参数（如果有）
    int modLoaderType = getModLoaderTypeApiId();
    if (modLoaderType > 0)
    {
        url += QString("&modLoaderType=%1").arg(modLoaderType);
    }

    NetJob *netJob = new NetJob(QString("CurseForge::ModDownloadInfo(%1)").arg(modId), APPLICATION->network());
    std::shared_ptr<QByteArray> response = std::make_shared<QByteArray>();
    auto download = Net::Download::makeByteArray(QUrl(url), response.get());
    download->setExtraHeader("x-api-key", APPLICATION->curseAPIKey());
    netJob->addNetAction(download);

    connect(netJob, &NetJob::succeeded, this, [response, modId, callback]()
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

        // API已经通过参数筛选返回匹配的文件，直接使用第一个文件
        if (!filesArray.isEmpty()) {
            QJsonObject fileObj = filesArray.first().toObject();

            DownloadItem item;
            item.downloadUrl = fileObj["downloadUrl"].toString();
            item.fileName = fileObj["fileName"].toString();
            item.fileID = fileObj["id"].toInt();
            item.fileFingerprint = QString::number(fileObj["fileFingerprint"].toVariant().toULongLong());
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
        } else {
            // 没有找到匹配的文件
            callback(DownloadItem());
        } });

    connect(netJob, &NetJob::failed, this, [callback](QString reason)
            {
        qDebug() << "Failed to fetch mod download info:" << reason;
        callback(DownloadItem()); });

    netJob->start();
}

void ModDownloadPage::buildDownloadQueue(int modId, std::function<void()> onComplete, bool isDependency)
{
    if (processedDependencies.contains(modId))
    {
        onComplete();
        return;
    }

    processedDependencies.insert(modId);

    fetchModDownloadInfo(modId, [this, onComplete, isDependency](const DownloadItem &item)
                         {
        if (item.downloadUrl.isEmpty()) {
            onComplete();
            return;
        }

        // 检查是否需要下载（未安装或需要更新）
        ModInstallStatus status = getModInstallStatus(item.modId, item.fileID);

        bool shouldDownload = false;
        if (status == ModDownloadPageUIFactory::MOD_NOT_INSTALLED) {
            shouldDownload = true;
        } else if (status == ModDownloadPageUIFactory::MOD_NEEDS_UPDATE) {
            // 如果是依赖项且已经安装了（哪怕是旧版本），则不再下载，避免覆盖或重复
            if (!isDependency) {
                shouldDownload = true;
            } else {
                qDebug() << "Skipping update for dependency mod:" << item.fileName;
            }
        }

        if (shouldDownload) {
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
                }, true);
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
    progressBar->setValue(0);

    // 准备下载环境
    QString modsDir = m_inst->modsRoot();
    QDir dir(modsDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    // 创建单一的 NetJob 处理所有下载
    NetJob *job = new NetJob(tr("Mod Batch Download"), APPLICATION->network());
    
    // 清空映射
    m_currentDownloadMap.clear();

    // 添加所有下载任务到 NetJob
    for (const auto &item : m_downloadQueue)
    {
        QString filePath = modsDir + "/" + item.fileName;
        auto download = Net::Download::makeFile(QUrl(item.downloadUrl), filePath);
        
        // 记录索引与下载项的映射 (NetJob 的索引从 0 开始递增)
        int index = job->size(); // 当前添加前的 size 即为新任务的 index
        m_currentDownloadMap.insert(index, item);
        
        job->addNetAction(download);
    }

    // 连接 NetJob 信号
    // 使用 lambda 捕获 progressBar 和 statsLayout 是不安全的，因为它们可能被销毁？
    // 但在这个上下文中，Page 应该还在。为了安全，我们可以将它们保存为成员变量或者确保生命周期。
    // 这里我们假设下载过程中页面不会被销毁。
    
    // 连接总进度
    connect(job, &NetJob::progress, this, [progressBar](qint64 current, qint64 total) {
        if (total > 0) {
            progressBar->setValue((int)((float)current / total * 100));
        }
    });

    // 连接单个部分成功/失败
    connect(job, SIGNAL(partSucceeded(int)), this, SLOT(onDownloadPartSucceeded(int)));
    connect(job, SIGNAL(partFailed(int)), this, SLOT(onDownloadPartFailed(int)));
    
    // 连接整个任务完成（无论成功与否，NetJob 结束时我们都应该恢复 UI）
    connect(job, &NetJob::succeeded, this, &ModDownloadPage::onAllDownloadsFinished);
    connect(job, &NetJob::failed, this, [this](QString reason) {
        qWarning() << "Batch download failed:" << reason;
        onAllDownloadsFinished();
    });
    
    // 启动下载
    job->start();
}

void ModDownloadPage::onDownloadPartSucceeded(int index)
{
    if (m_currentDownloadMap.contains(index))
    {
        const auto &item = m_currentDownloadMap[index];
        
        ModDownloadInfo modInfo;
        modInfo.modId = item.modId;
        modInfo.name = item.fileName;
        modInfo.fileID = item.fileID;
        modInfo.fileFingerprint = item.fileFingerprint;
        
        m_completedMods.append(modInfo);
    }
}

void ModDownloadPage::onDownloadPartFailed(int index)
{
    if (m_currentDownloadMap.contains(index))
    {
        const auto &item = m_currentDownloadMap[index];
        qWarning() << "Failed to download mod part:" << item.fileName;
        // 这里可以选择记录失败的模组，或者在最后统一提示
    }
}

void ModDownloadPage::onAllDownloadsFinished()
{
    // 获取 sender 所在的 NetJob 并删除
    NetJob *job = qobject_cast<NetJob*>(sender());
    if (job) {
        job->deleteLater();
    }
    
    // 恢复 UI
    // 注意：这里需要访问 statsLayout 和 progressBar
    // 由于我们重构了函数，不再直接传递这些指针。我们需要通过 ui 指针访问它们，
    // 或者在 ModDownloadPage 中保存对当前正在操作的 ModWidget 的引用？
    // 实际上，processDownloadQueue 是在点击安装按钮时调用的，那时我们有局部变量。
    // 但现在转为异步，我们需要一种方式来恢复 UI。
    
    // 简单的做法是遍历 UI 寻找隐藏的 statsLayout？
    // 或者，我们可以只弹窗提示，因为安装完成后通常不需要恢复“安装”按钮状态（已经变成已安装了）
    
    // 批量写入所有下载完成的模组信息
    if (!m_completedMods.isEmpty())
    {
        QList<ModInfo> modInfoList;
        
        for (const auto &modInfo : m_completedMods)
        {
            ModInfo jsonInfo;
            jsonInfo.projectId = modInfo.modId;
            jsonInfo.fileId = modInfo.fileID;
            jsonInfo.name = modInfo.name;
            jsonInfo.fileFingerprint = modInfo.fileFingerprint;
            modInfoList.append(jsonInfo);
        }

        // 使用ModJsonManager批量添加模组
        for (const auto &modInfo : modInfoList)
        {
            m_modJsonManager.addMod(modInfo, true);
        }
        
        QString message = tr("Successfully downloaded and installed %1 mods.").arg(m_completedMods.size());
        if (m_completedMods.size() < m_downloadQueue.size()) {
            message += tr("\n%1 mods failed to download.").arg(m_downloadQueue.size() - m_completedMods.size());
        }
        QMessageBox::information(this, tr("Installation Complete"), message);
        
        m_completedMods.clear();
    }
    else
    {
        QMessageBox::warning(this, tr("Installation Failed"), tr("All mod downloads failed."));
    }
    
    // 刷新列表或者更新按钮状态？
    // 由于我们是在列表项内部操作，可能需要刷新整个列表来反映“已安装”状态
    // 或者让用户手动刷新。
    // 为了用户体验，我们可以尝试重新加载列表（虽然会丢失当前滚动位置）
    // 或者仅仅依靠 m_modJsonManager 的状态更新，下次渲染时会正确显示。
    // 在旧代码中，按钮被设置为 "已安装" 并禁用，这是在 processDownloadQueue 的回调中做的。
    // 但现在是批量异步。
    
    // 作为一个折衷方案，我们可以在这里触发一次列表刷新
    // onSearch(); // 这会重置一切
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

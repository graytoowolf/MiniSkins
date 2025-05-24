#include "FileResolvingTask.h"
#include "Application.h"
#include "FileSystem.h"
#include "Json.h"

namespace
{
const char *metabase = "https://api.curseforge.com/v1/mods";
}

CurseForge::FileResolvingTask::FileResolvingTask(shared_qobject_ptr<QNetworkAccessManager> network, CurseForge::Manifest &toProcess, const QString &path)
    : m_network(network), m_toProcess(toProcess), m_path(path)
{
}

void CurseForge::FileResolvingTask::executeTask()
{
    // 初始化用于处理MOD依赖的成员变量
    m_processedModIds.clear();
    m_modsToQueryQueue.clear();
    m_activeNetJobs.clear();

    // 记录初始清单中的MOD ID
    m_initialManifestModIds.clear();
    for (const auto &file : m_toProcess.files)
    {
        m_initialManifestModIds.insert(file.projectId);
    }

    // 处理白名单MOD
    processWhitelistedMods();
}

// 继续执行后续流程的方法
void CurseForge::FileResolvingTask::continueExecution()
{
    // 检查是否还有待处理的MOD或活动的网络请求
    if (!m_modsToQueryQueue.isEmpty() || !m_activeNetJobs.isEmpty())
    {
        // 如果还有待处理的项目，则先处理它们
        if (!m_modsToQueryQueue.isEmpty())
        {
            processNextModInQueue();
        }
        return;
    }

    if (m_toProcess.files.isEmpty())
    {
        emitSucceeded();
        return;
    }
    setStatus(tr("Parsing directory"));
    QJsonObject requestObject;
    QJsonArray modIdsArray;
    for (auto &file : m_toProcess.files)
    {
        modIdsArray.append(file.projectId);
    }
    requestObject["modIds"] = modIdsArray;
    requestObject["filterPcOnly"] = true;
    QNetworkRequest netRequest{QUrl(metabase)};
    netRequest.setRawHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_rep = m_network->post(netRequest, QJsonDocument(requestObject).toJson());
    connect(m_rep, &QNetworkReply::finished, this, &CurseForge::FileResolvingTask::downloadFinished);
}

void CurseForge::FileResolvingTask::downloadFinished()
{
    if (m_rep->error() != QNetworkReply::NoError)
    {
        qDebug() << "Network Error:" << m_rep->errorString();
        m_rep->deleteLater();
        return;
    }

    QByteArray response = m_rep->readAll();
    m_rep->deleteLater();

    auto rootObj = QJsonDocument::fromJson(response).object();
    auto dataArray = rootObj.value("data").toArray();
    processModData(dataArray);

    setStatus(tr("Resolving mod IDs..."));
    setProgress(0, m_toProcess.files.size());
    prepareDownloads();
}

void CurseForge::FileResolvingTask::processModData(const QJsonArray &dataArray)
{
    for (const auto &dataValue : dataArray)
    {
        auto modObj = dataValue.toObject();
        int classId = modObj.value("classId").toInt();
        int m_id = modObj.value("id").toInt();
        QString m_name = modObj.value("name").toString();
        for (auto &m_file : m_toProcess.files)
        {
            if (m_file.projectId == m_id)
            {
                m_file.targetFolder = getTargetFolderByClassId(classId);
            }
        }
        if (APPLICATION->isModBlacklisted(m_id))
        {
            QString modName = APPLICATION->getModNameFromBlacklist(m_id);
            if (modName == "Provisional Name")
            {
                APPLICATION->updateModBlacklistName(m_id, m_name);
            }
        }
    }
}

QString CurseForge::FileResolvingTask::getTargetFolderByClassId(int classId)
{
    switch (classId)
    {
    case 6:
        return "mods";
    case 6552:
        return "shaderpacks";
    case 12:
        return "resourcepacks";
    default:
        return "other"; // 其他情况根据需要设置
    }
}

void CurseForge::FileResolvingTask::prepareDownloads()
{
    m_dljob = new NetJob("Mod id resolver", m_network);
    results.resize(m_toProcess.files.size());
    QString InstanceDir = APPLICATION->settings()->get("InstanceDir").toString();
    QString m_instDir = QDir(InstanceDir).canonicalPath();
    QString m_modpacksid = APPLICATION->getID();
    QString m_modpacksfile = FS::PathCombine(m_instDir, m_modpacksid);

    CurseForge::ComparisonResult result;

    // 只有在更新模式下才执行MOD对比相关操作
    if (APPLICATION->isUpdating())
    {
        QString m_mod = FS::PathCombine(m_modpacksfile, "mod.json");
        result = compareManifests(m_mod);

        QString basePath = "minecraft";
        QString minecraftPath = FS::PathCombine(m_modpacksfile, basePath);
        if (!QDir(minecraftPath).exists())
        {
            basePath = ".minecraft";
        }

        for (const QString &fileName : result.filesToDelete)
        {
            QString m_name = FS::PathCombine(m_modpacksfile, basePath, "mods", fileName);
            QFile::remove(m_name);
        }
    }
    else
    {
        // 非更新模式下，将所有文件添加到下载列表
        for (const auto &file : m_toProcess.files)
        {
            if (file.fileId != 0)
            {
                result.filesToDownload.append(file.fileId);
            }
        }
    }

    if (!result.filesToDownload.isEmpty())
    {
        QJsonObject requestObject;
        requestObject["fileIds"] = result.filesToDownload;
        QString metaurl = QString("%1/files").arg(metabase);
        QNetworkRequest netRequest{QUrl(metaurl)};
        netRequest.setRawHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        m_rep = m_network->post(netRequest, QJsonDocument(requestObject).toJson());

        connect(m_rep, &QNetworkReply::finished, this, &CurseForge::FileResolvingTask::netJobFinished);
    }
    else
    {
        emitSucceeded();
    }
}

void CurseForge::FileResolvingTask::netJobprogress(qint64 current, qint64 total)
{
    setProgress(current, total);
}

void CurseForge::FileResolvingTask::netJobFinished()
{
    bool failed = false;
    // int index = -1;
    QByteArray response = m_rep->readAll();
    m_rep->deleteLater();
    auto rootObj = QJsonDocument::fromJson(response).object();
    auto dataArray = rootObj.value("data").toArray();
    for (const auto &dataValue : dataArray)
    {
        auto modObj = dataValue.toObject();
        int m_id = Json::requireInteger(modObj, "modId");
        // int m_id = modObj.value("modId").toInt();
        for (auto &m_file : m_toProcess.files)
        {
            if (m_file.projectId == m_id)
            {
                m_file.fileName = Json::requireString(modObj, "fileName");
                QString rawUrl = Json::requireString(modObj, "downloadUrl");
                m_file.url = QUrl(rawUrl, QUrl::TolerantMode);
                if (!m_file.url.isValid())
                {
                    throw JSONValidationError(QString("Invalid URL: %1").arg(rawUrl));
                }
            }
        }
    }

    QString m_modPath = FS::PathCombine(m_path, "mod.json");
    QFile m_modFile(m_modPath);
    if (m_modFile.open(QIODevice::WriteOnly))
    {
        QJsonArray newArray;
        for (const auto &file : m_toProcess.files)
        {
            QString m_name = file.fileName;
            bool required = file.required;
            if (APPLICATION->isModBlacklisted(file.projectId))
            {
                m_name += ".disabled";
                required = false;
            }
            QJsonObject fileObj{
                {"projectID", file.projectId},
                {"fileID", file.fileId},
                {"required", required},
                {"name", m_name}};
            newArray.append(fileObj);
        }
        QJsonDocument newDoc(newArray);
        m_modFile.write(newDoc.toJson());
        m_modFile.close();
    }
    else
    {
        qDebug() << "Unable to open file for writing:" << m_filePath;
    }

    if (!failed)
    {
        emitSucceeded();
    }
    else
    {
        emitFailed(tr("Some mod ID resolving tasks failed."));
    }
}

CurseForge::ComparisonResult CurseForge::FileResolvingTask::compareManifests(const QString &jsonFilePathA)
{
    CurseForge::ComparisonResult result;

    // A文件不存在的情况
    QFile fileA(jsonFilePathA);
    if (!fileA.open(QIODevice::ReadOnly))
    {
        qWarning() << "无法打开A文件:" << jsonFilePathA;
        for (const auto &file : m_toProcess.files) // 现在可以直接访问 m_toProcess
        {
            if (file.fileId != 0)
            {
                result.filesToDownload.append(file.fileId);
            }
        }
        return result;
    }

    // 读取并解析A文件
    QJsonDocument documentA = QJsonDocument::fromJson(fileA.readAll());
    fileA.close();

    if (!documentA.isArray())
    {
        qWarning() << "A文件JSON不是数组格式:" << jsonFilePathA;
        return result;
    }

    QJsonArray aFiles = documentA.array();

    // 创建A文件中projectID到完整信息的映射
    struct AFileInfo
    {
        int fileID;
        QString name;
        bool required;
    };
    QMap<int, AFileInfo> aFileMap; // projectID -> FileInfo
    for (const QJsonValue &value : aFiles)
    {
        QJsonObject obj = value.toObject();
        int projectID = obj["projectID"].toInt();
        AFileInfo info;
        info.fileID = obj["fileID"].toInt();
        info.name = obj["name"].toString();
        info.required = obj["required"].toBool(true); // 默认为true

        aFileMap[projectID] = info;
    }

    // 创建B文件中projectID的集合
    QSet<int> bProjectIDs;

    // 遍历B的manifest
    for (auto &file : m_toProcess.files) // 注意:这里需要是引用才能修改
    {
        int projectID = file.projectId;
        int fileID = file.fileId;

        if (projectID == 0 || fileID == 0)
        {
            qWarning() << "B文件中存在无效的项目:" << projectID;
            continue;
        }

        bProjectIDs.insert(projectID);

        if (!aFileMap.contains(projectID))
        {
            result.filesToDownload.append(fileID);
            continue;
        }

        const AFileInfo &aInfo = aFileMap[projectID];
        if (aInfo.fileID != fileID)
        {
            result.filesToDownload.append(fileID);
            result.filesToDelete.append(aInfo.name);
        }
        else
        {
            // fileID相同时,同步name和required状态
            file.fileName = aInfo.name;
            file.required = aInfo.required;
        }

        // 如果A文件中required为false,则在B中也设为false
        if (!aInfo.required)
        {
            file.required = false;
        }
    }

    // 检查需要删除的文件
    for (auto it = aFileMap.begin(); it != aFileMap.end(); ++it)
    {
        int projectID = it.key();
        QString fileName = it.value().name;

        if (!bProjectIDs.contains(projectID))
        {
            result.filesToDelete.append(fileName);
        }
    }

    return result;
}

void CurseForge::FileResolvingTask::processWhitelistedMods()
{
    QMap<int, QString> whitelist = APPLICATION->getModWhitelist();
    if (whitelist.isEmpty())
    {
        // 如果白名单为空，直接继续执行后续流程
        continueExecution();
        return;
    }

    // 创建已有mod的ID集合，用于检查白名单mod是否已存在
    QSet<int> existingModIds;
    for (const auto &file : m_toProcess.files)
    {
        existingModIds.insert(file.projectId);
        m_processedModIds.insert(file.projectId); // 将现有MOD添加到已处理集合中
    }

    // 获取Minecraft版本和加载器信息
    m_currentMcVersion = m_toProcess.minecraft.version;
    QString modLoader;
    m_currentModLoaderType = 0; // 默认为Any

    // 确定当前的模组加载器
    for (const auto &loader : m_toProcess.minecraft.modLoaders)
    {
        if (loader.primary)
        {
            modLoader = loader.id;
            break;
        }
    }

    // 确定加载器类型
    if (!modLoader.isEmpty())
    {
        if (modLoader.startsWith("forge-"))
        {
            m_currentModLoaderType = 1; // Forge
        }
        else if (modLoader.startsWith("fabric-"))
        {
            m_currentModLoaderType = 4; // Fabric
        }
        else if (modLoader.startsWith("neoforge-"))
        {
            m_currentModLoaderType = 6; // NeoForge
        }
        else if (modLoader.startsWith("quilt-"))
        {
            m_currentModLoaderType = 5; // Quilt
        }
    }

    setStatus(tr("Processing whitelist mods..."));

    // 将白名单中的MOD添加到处理队列
    for (auto it = whitelist.constBegin(); it != whitelist.constEnd(); ++it)
    {
        int modId = it.key();
        if (!existingModIds.contains(modId) && !m_processedModIds.contains(modId))
        {
            m_modsToQueryQueue.enqueue(modId);
            m_processedModIds.insert(modId); // 标记为已处理，避免重复添加
        }
    }

    // 如果没有需要处理的MOD，直接继续
    if (m_modsToQueryQueue.isEmpty())
    {
        continueExecution();
        return;
    }

    // 开始处理队列中的第一个MOD
    processNextModInQueue();
}

void CurseForge::FileResolvingTask::processNextModInQueue()
{
    // 如果队列为空，继续执行后续流程
    if (m_modsToQueryQueue.isEmpty())
    {
        continueExecution();
        return;
    }

    // 从队列中取出一个MOD ID
    int modId = m_modsToQueryQueue.dequeue();
    qDebug() << "Processing mod from queue:" << modId;

    // 创建网络请求获取MOD信息
    NetJob *netJob = new NetJob(QString("CurseForge::ModFile(%1)").arg(modId), APPLICATION->network());
    std::shared_ptr<QByteArray> response = std::make_shared<QByteArray>();

    // 构建API请求URL
    QString apiUrl = QString("%1/%2/files").arg(metabase).arg(modId);
    QUrlQuery query;

    // 添加查询参数
    query.addQueryItem("gameVersion", m_currentMcVersion);
    query.addQueryItem("modLoaderType", QString::number(m_currentModLoaderType));
    query.addQueryItem("pageSize", "1"); // 只获取一个文件（最新的）

    QUrl url(apiUrl);
    url.setQuery(query);

    // 创建下载任务
    auto download = Net::Download::makeByteArray(url, response.get());
    download->setExtraHeader("x-api-key", APPLICATION->curseAPIKey());
    netJob->addNetAction(download);

    // 处理请求成功的情况
    connect(netJob, &NetJob::succeeded, [this, response, modId, netJob]()
    {
        // 将网络任务从活动列表中移除
        m_activeNetJobs.removeOne(netJob);
        netJob->deleteLater();

        QJsonParseError parse_error;
        QJsonDocument doc = QJsonDocument::fromJson(*response, &parse_error);
        if (parse_error.error != QJsonParseError::NoError) {
            qWarning() << "Error while parsing JSON response from CurseForge at " << parse_error.offset
                       << " reason: " << parse_error.errorString();
            qWarning() << *response;

            // 即使解析失败也继续处理下一个MOD
            processNextModInQueue();
            return;
        }

        QJsonObject rootObj = doc.object();
        QJsonArray dataArray = rootObj["data"].toArray();

        // 如果没有找到匹配的文件，记录日志但继续处理
        if (dataArray.isEmpty()) {
            qDebug() << "No compatible files found for mod" << modId;
            processNextModInQueue();
            return;
        }

        // 获取第一个文件（最新的）
        QJsonObject fileObj = dataArray.first().toObject();

        // 获取MOD名称并更新白名单中的名称（如果是临时名称）
        if (APPLICATION->getModNameFromWhitelist(modId) == "Provisional Name") {
            QString modName;

            // 优先使用displayName字段
            if (fileObj.contains("displayName")) {
                QString displayName = fileObj["displayName"].toString();
                // 提取名称部分（去除加载器名称和版本号）
                QRegExp rx("([\\w\\s\\-]+?)(?:-(?:NeoForge|Forge|Fabric|Quilt)-[\\d\\.]+|$)");
                if (rx.indexIn(displayName) != -1) {
                    modName = rx.cap(1).trimmed();
                } else {
                    modName = displayName; // 如果无法提取，使用完整名称
                }
            }

            // 如果成功获取到名称，更新白名单
            if (!modName.isEmpty()) {
                APPLICATION->updateModWhitelistName(modId, modName);
                qDebug() << "Updated whitelist mod name for" << modId << "from 'Provisional Name' to" << modName;
            }
        }

        // 创建File对象并添加到下载列表
        CurseForge::File file;
        file.projectId = modId;
        file.fileId = fileObj["id"].toInt();
        file.fileName = fileObj["fileName"].toString();
        QString rawUrl = fileObj["downloadUrl"].toString();
        file.url = QUrl(rawUrl, QUrl::TolerantMode);
        file.required = true;
        file.targetFolder = "mods"; // 默认放在mods文件夹

        // 添加到下载列表
        m_toProcess.files.append(file);
        qDebug() << "Added mod" << modId << "to download list:" << file.fileName;

        // 处理MOD依赖关系
        if (fileObj.contains("dependencies")) {
            QJsonArray dependencies = fileObj["dependencies"].toArray();
            for (const auto &depValue : dependencies) {
                QJsonObject depObj = depValue.toObject();
                int depModId = depObj["modId"].toInt();
                int relationType = depObj["relationType"].toInt();

                // 只处理必需的依赖（RequiredDependency）
                if (relationType == 3) {
                    // 检查依赖的MOD是否已经在处理列表中
                    if (!m_processedModIds.contains(depModId) && !m_initialManifestModIds.contains(depModId)) {
                        qDebug() << "Adding dependency mod to queue:" << depModId;
                        m_modsToQueryQueue.enqueue(depModId);
                        m_processedModIds.insert(depModId); // 标记为已处理，避免重复添加
                    }
                }
            }
        }

        // 继续处理队列中的下一个MOD
        processNextModInQueue(); });

    // 处理请求失败的情况
    connect(netJob, &NetJob::failed, [this, modId, netJob](QString reason)
    {
        qDebug() << "Failed to get information for mod" << modId << ":" << reason;

        // 将网络任务从活动列表中移除
        m_activeNetJobs.removeOne(netJob);
        netJob->deleteLater();

        // 继续处理队列中的下一个MOD
        processNextModInQueue(); });

    // 将网络任务添加到活动列表中
    m_activeNetJobs.append(netJob);

    // 启动网络请求
    netJob->start();
}

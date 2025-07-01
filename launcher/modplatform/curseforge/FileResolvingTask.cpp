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
    if (m_toProcess.files.isEmpty())
    {
        emitSucceeded();
        return;
    }
    setStatus(tr("Parsing directory"));

    // 首先获取模组信息
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

    QNetworkReply *modInfoReply = m_network->post(netRequest, QJsonDocument(requestObject).toJson());
    connect(modInfoReply, &QNetworkReply::finished, this, [this, modInfoReply]() {
        modInfoFinished(modInfoReply);
    });
}



void CurseForge::FileResolvingTask::modInfoFinished(QNetworkReply *reply)
{
    QByteArray response = reply->readAll();
    reply->deleteLater();
    auto rootObj = QJsonDocument::fromJson(response).object();
    auto dataArray = rootObj.value("data").toArray();
    processModData(dataArray);

    // 处理完模组信息后，开始准备下载
    prepareDownloads();
}

void CurseForge::FileResolvingTask::downloadFinished(QNetworkReply *reply)
{
    bool failed = false;
    QByteArray response = reply->readAll();
    reply->deleteLater();
    auto rootObj = QJsonDocument::fromJson(response).object();
    auto dataArray = rootObj.value("data").toArray();

    for (const auto &dataValue : dataArray)
    {
        auto modObj = dataValue.toObject();
        int m_id = Json::requireInteger(modObj, "id");
        for (auto &m_file : m_toProcess.files)
        {
            if (m_file.fileId == m_id)
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

    if (!failed)
    {
        emitSucceeded();
    }
    else
    {
        emitFailed(tr("Some mod ID resolving tasks failed."));
    }
}

void CurseForge::FileResolvingTask::processModData(const QJsonArray &dataArray)
{
    for (const auto &dataValue : dataArray)
    {
        auto modObj = dataValue.toObject();
        int classId = modObj.value("classId").toInt();
        int m_id = modObj.value("id").toInt();
        for (auto &m_file : m_toProcess.files)
        {
            if (m_file.projectId == m_id)
            {
                m_file.targetFolder = getTargetFolderByClassId(classId);
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

        QNetworkReply *downloadReply = m_network->post(netRequest, QJsonDocument(requestObject).toJson());

        connect(downloadReply, &QNetworkReply::finished, this, [this, downloadReply]() {
            netJobFinished(downloadReply);
        });
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

void CurseForge::FileResolvingTask::netJobFinished(QNetworkReply *reply)
{
    setProgress(100, 100);
    downloadFinished(reply);
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

#include "FileResolvingTask.h"
#include "Application.h"
#include "FileSystem.h"
#include "Json.h"

namespace {
    const char * metabase = "https://api.curseforge.com/v1/mods";
}

CurseForge::FileResolvingTask::FileResolvingTask(shared_qobject_ptr<QNetworkAccessManager> network, CurseForge::Manifest& toProcess,const QString& path)
    : m_network(network), m_toProcess(toProcess),m_path(path)
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
    QJsonObject requestObject;
    QJsonArray modIdsArray;
    for (auto &file : m_toProcess.files) {
        modIdsArray.append(file.projectId);
    }
    requestObject["modIds"] = modIdsArray;
    requestObject["filterPcOnly"] = true;
    QNetworkRequest netRequest{QUrl(metabase)};
    netRequest.setRawHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_rep = m_network->post(netRequest,QJsonDocument(requestObject).toJson());
    connect(m_rep, &QNetworkReply::finished, this, &CurseForge::FileResolvingTask::downloadFinished);


}
void CurseForge::FileResolvingTask::downloadFinished()
{
    if (m_rep->error() != QNetworkReply::NoError) {
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
    for (const auto &dataValue : dataArray) {
        auto modObj = dataValue.toObject();
        int classId = modObj.value("classId").toInt();
        int m_id = modObj.value("id").toInt();
        QString m_name = modObj.value("name").toString();
        for (auto &m_file : m_toProcess.files) {
            if (m_file.projectId == m_id){
                m_file.targetFolder = getTargetFolderByClassId(classId);
            }
        }
        if (APPLICATION->isModBlacklisted(m_id)){
            QString modName = APPLICATION->getModNameFromBlacklist(m_id);
            if (modName == "Provisional Name"){
                APPLICATION->updateModBlacklistName(m_id,m_name);
            }
        }
    }
}

QString CurseForge::FileResolvingTask::getTargetFolderByClassId(int classId)
{
    switch (classId) {
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
    QString m_modpacksfile = FS::PathCombine(m_instDir,m_modpacksid);
    QJsonArray modArray;
    m_filePath = m_path;
    QFile modFile(FS::PathCombine(m_modpacksfile,"mod.json"));
    if (APPLICATION->isUpdating()){
        if (modFile.exists()) {
            if (modFile.open(QIODevice::ReadOnly)) {
                QJsonDocument modDoc = QJsonDocument::fromJson(modFile.readAll());
                modArray = modDoc.array();
                m_filePath = FS::PathCombine(m_instDir,m_modpacksid);
                modFile.close();
            }
        }
    }
    int index = 0;
    //int indextask = 0;
    QJsonArray modIdsArray;
    for (auto &file : m_toProcess.files) {
        auto projectIdStr = QString::number(file.projectId);
        auto fileIdStr = QString::number(file.fileId);
        bool shouldQueueForDownload = true;
        if (!modArray.isEmpty()) {
            for (int i = 0; i < modArray.size(); i++) {
                QJsonObject modObject = modArray[i].toObject();
                if (modObject["projectID"].toInt() == file.projectId) {
                    if (modObject["fileID"].toInt() == file.fileId) {
                        file.fileName = modObject["name"].toString();
                        shouldQueueForDownload = false;
                        break;
                    } else {
                        // 删除文件
                        QString m_name = FS::PathCombine(m_modpacksfile, "minecraft",
                                                       file.targetFolder,
                                                       modObject["name"].toString());
                        QFile::remove(m_name);
                        // 从数组中移除该元素
                        modArray.removeAt(i);
                        i--; // 因为删除了元素，需要减少索引
                        break;
                    }
                }
            }

            // 检查并删除不再使用的mod
            for (int i = modArray.size() - 1; i >= 0; i--) {
                QJsonObject jsonFile = modArray[i].toObject();
                const int projectID = jsonFile["projectID"].toInt();
                auto fileIt = std::find_if(m_toProcess.files.begin(),
                                         m_toProcess.files.end(),
                                         [projectID](const File& file) {
                                             return file.projectId == projectID;
                                         });
                if (fileIt == m_toProcess.files.end()) {
                    // 删除文件
                    QFile::remove(FS::PathCombine(m_modpacksfile, "minecraft",
                                                file.targetFolder,
                                                jsonFile["name"].toString()));
                    // 从数组中移除
                    modArray.removeAt(i);
                }
            }

        }
        if (shouldQueueForDownload) {
            modIdsArray.append(fileIdStr);
        }
        index++;
    }
    if (modFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(modArray);
        modFile.write(doc.toJson());
        modFile.close();
    }

    if (!modIdsArray.isEmpty())
    {
        QJsonObject requestObject;
        requestObject["fileIds"] = modIdsArray;
        QString metaurl = QString("%1/files").arg(metabase);
        QNetworkRequest netRequest{QUrl(metaurl)};
        netRequest.setRawHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        m_rep = m_network->post(netRequest,QJsonDocument(requestObject).toJson());

        connect(m_rep, &QNetworkReply::finished, this, &CurseForge::FileResolvingTask::netJobFinished);
    } else {
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
    //int index = -1;
    QByteArray response = m_rep->readAll();
    m_rep->deleteLater();
    auto rootObj = QJsonDocument::fromJson(response).object();
    auto dataArray = rootObj.value("data").toArray();
    for(const auto &dataValue : dataArray)
    {
        auto modObj = dataValue.toObject();
        int m_id = Json::requireInteger(modObj,"modId");
        //int m_id = modObj.value("modId").toInt();
        for (auto &m_file : m_toProcess.files) {
            if (m_file.projectId == m_id)
            {
                m_file.fileName = Json::requireString(modObj, "fileName");
                QString rawUrl = Json::requireString(modObj, "downloadUrl");
                m_file.url = QUrl(rawUrl, QUrl::TolerantMode);
                if(!m_file.url.isValid())
                {
                    throw JSONValidationError(QString("Invalid URL: %1").arg(rawUrl));
                }
            }
        }
    }

    m_filePath = FS::PathCombine(m_filePath,"mod.json");
    QFile m_modFile(m_filePath);
    if (m_modFile.open(QIODevice::WriteOnly)) {
        QJsonArray newArray;
        for (const auto &file : m_toProcess.files) {
            QString m_name = file.fileName;
            if (APPLICATION->isModBlacklisted(file.projectId)) {
                m_name += ".disabled";
            }
            QJsonObject fileObj{
                {"projectID", file.projectId},
                {"fileID", file.fileId},
                {"required", true},
                {"name", m_name}
            };
            newArray.append(fileObj);
        }
        QJsonDocument newDoc(newArray);
        m_modFile.write(newDoc.toJson());
        m_modFile.close();
    } else {
        qDebug() << "Unable to open file for writing:" << m_filePath;
    }

    if(!failed)
    {
        emitSucceeded();
    }
    else
    {
        emitFailed(tr("Some mod ID resolving tasks failed."));
    }
}

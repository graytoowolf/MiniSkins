#include "ModFingerprintLookupTask.h"

#include "Application.h"
#include "BuildConfig.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QVariant>

namespace
{
const char curseForgeFingerprintUrl[] = "https://api.curseforge.com/v1/fingerprints";

void applyMatch(fingerprint::ModInfo &modInfo, const QJsonObject &match)
{
    const QJsonObject fileObj = match.value("file").toObject();
    if (fileObj.isEmpty())
    {
        return;
    }

    modInfo.projectId = match.value("id").toInt();
    modInfo.fileId = fileObj.value("id").toInt();
    modInfo.name = fileObj.value("displayName").toString();
    if (modInfo.name.isEmpty())
    {
        modInfo.name = fileObj.value("fileName").toString();
    }

    const QVariant fingerprintValue = fileObj.value("fileFingerprint").toVariant();
    if (fingerprintValue.isValid())
    {
        modInfo.fileFingerprint = QString::number(fingerprintValue.toULongLong());
    }
    modInfo.isValid = true;
}
}

namespace CurseForge
{
ModFingerprintLookupTask::ModFingerprintLookupTask(const QList<fingerprint::ModInfo> &mods)
    : NetAction(), m_results(mods)
{
    m_url = QUrl(QString::fromLatin1(curseForgeFingerprintUrl));
    m_status = Job_NotStarted;
    m_timeoutTimer.setSingleShot(true);
    m_timeoutTimer.setInterval(15000);
    connect(&m_timeoutTimer, &QTimer::timeout, this, [this]()
    {
        qWarning() << "CurseForge fingerprint lookup timed out";
        if (m_reply)
        {
            m_reply->abort();
        }
        else
        {
            finish(Job_Failed);
        }
    });
}

void ModFingerprintLookupTask::startImpl()
{
    m_finished = false;
    m_status = Job_InProgress;

    QJsonArray fingerprints;
    for (int i = 0; i < m_results.size(); ++i)
    {
        const fingerprint::ModInfo &modInfo = m_results.at(i);
        if (modInfo.isValid || modInfo.fileFingerprint.isEmpty())
        {
            continue;
        }

        bool ok = false;
        const qulonglong fingerprintValue = modInfo.fileFingerprint.toULongLong(&ok);
        if (ok)
        {
            fingerprints.append(QJsonValue(static_cast<double>(fingerprintValue)));
            m_fingerprintToIndex.insert(fingerprintValue, i);
        }
    }

    if (fingerprints.isEmpty())
    {
        finish(Job_Finished);
        return;
    }

    QJsonObject requestObject;
    requestObject.insert("fingerprints", fingerprints);

    QNetworkRequest request(m_url);
    request.setRawHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, BuildConfig.USER_AGENT_UNCACHED);

    QNetworkReply *reply = m_network->post(request, QJsonDocument(requestObject).toJson());
    m_reply.reset(reply);
    connect(reply, &QNetworkReply::uploadProgress, this, &ModFingerprintLookupTask::downloadProgress);
    connect(reply, &QNetworkReply::finished, this, &ModFingerprintLookupTask::downloadFinished);
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), SLOT(downloadError(QNetworkReply::NetworkError)));
    m_timeoutTimer.start();
}

void ModFingerprintLookupTask::downloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    m_total_progress = bytesTotal;
    m_progress = bytesReceived;
    emit netActionProgress(m_index_within_job, bytesReceived, bytesTotal);
}

void ModFingerprintLookupTask::downloadError(QNetworkReply::NetworkError)
{
    if (m_finished)
    {
        return;
    }

    if (m_status == Job_Aborted)
    {
        m_timeoutTimer.stop();
        m_reply.reset();
        finish(Job_Aborted);
        return;
    }

    qWarning() << "CurseForge fingerprint lookup failed:" << m_reply->errorString();
    m_timeoutTimer.stop();
    m_reply.reset();
    finish(Job_Failed);
}

void ModFingerprintLookupTask::downloadFinished()
{
    if (m_finished)
    {
        return;
    }

    const QByteArray data = m_reply->readAll();
    m_timeoutTimer.stop();
    m_reply.reset();

    QJsonParseError parseError;
    const QJsonDocument responseDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !responseDoc.isObject())
    {
        qWarning() << "Failed to parse CurseForge fingerprint response:" << parseError.errorString();
        finish(Job_Failed);
        return;
    }

    const QJsonArray matches = responseDoc.object()
        .value("data").toObject()
        .value("exactMatches").toArray();

    for (const QJsonValue &matchValue : matches)
    {
        const QJsonObject match = matchValue.toObject();
        const QJsonObject fileObj = match.value("file").toObject();
        const qulonglong responseFingerprint = fileObj.value("fileFingerprint").toVariant().toULongLong();
        if (!m_fingerprintToIndex.contains(responseFingerprint))
        {
            continue;
        }

        applyMatch(m_results[m_fingerprintToIndex.value(responseFingerprint)], match);
    }

    finish(Job_Finished);
}

bool ModFingerprintLookupTask::abort()
{
    if (m_reply)
    {
        m_status = Job_Aborted;
        m_reply->abort();
    }
    else
    {
        finish(Job_Aborted);
    }
    return true;
}

bool ModFingerprintLookupTask::canAbort()
{
    return true;
}

void ModFingerprintLookupTask::finish(JobStatus status)
{
    if (m_finished)
    {
        return;
    }

    m_status = status;
    m_finished = true;

    if (status == Job_Finished)
    {
        emit succeeded(m_index_within_job);
    }
    else if (status == Job_Aborted)
    {
        emit aborted(m_index_within_job);
    }
    else
    {
        emit failed(m_index_within_job);
    }
}
}

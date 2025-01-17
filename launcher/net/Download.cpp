/* Copyright 2013-2023 MultiMC Contributors
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

#include "Download.h"

#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

#include "FileSystem.h"
#include "Application.h"
#include "ChecksumValidator.h"
#include "MetaCacheSink.h"
#include "ByteArraySink.h"

#include "BuildConfig.h"

namespace Net {

Download::Download():NetAction()
{
    m_status = Job_NotStarted;
}

Download::Ptr Download::makeCached(QUrl url, MetaEntryPtr entry, Options options)
{
    Download * dl = new Download();
    dl->m_url = url;
    dl->m_options = options;
    auto md5Node = new ChecksumValidator(QCryptographicHash::Md5);
    auto cachedNode = new MetaCacheSink(entry, md5Node);
    dl->m_sink.reset(cachedNode);
    dl->m_target_path = entry->getFullPath();
    return dl;
}

Download::Ptr Download::makeByteArray(QUrl url, QByteArray *output, Options options)
{
    Download * dl = new Download();
    dl->m_url = url;
    dl->m_options = options;
    dl->m_sink.reset(new ByteArraySink(output));
    return dl;
}

Download::Ptr Download::makeFile(QUrl url, QString path, Options options)
{
    Download * dl = new Download();
    dl->m_url = url;
    dl->m_options = options;
    dl->m_sink.reset(new FileSink(path));
    return dl;
}

void Download::addValidator(Validator * v)
{
    m_sink->addValidator(v);
}

void Download::startImpl()
{
    if(m_status == Job_Aborted)
    {
        qWarning() << "Attempt to start an aborted Download:" << m_url.toString();
        emit aborted(m_index_within_job);
        return;
    }
    QString source = "Mojang";
    if(APPLICATION->getconfigfile()){
        source = APPLICATION->settings()->get("Downloadsource").toString();
    }

    if (source != "Mojang")
    {
        auto settings = APPLICATION->settings();
        bool useProxy = settings->get("Downloadsourceproxy").toBool();
        QString baseUrl = settings->get("Downloadsourceurl").toString().trimmed();
        if (!baseUrl.isEmpty())
        {
            baseUrl = QUrl::fromUserInput(baseUrl).toString();
            if (!baseUrl.startsWith("https://"))
            {
                baseUrl = baseUrl.replace("http://", "https://");
            }
            if (!baseUrl.endsWith("/"))
            {
                baseUrl.append('/');
            }
        }

        QString host = m_url.toString();
        struct Replacement {
            QString search, replace;
        };
        Replacement replacements[] = {
            {"resources.download.minecraft.net", "<j_url>/assets"},
            {"libraries.minecraft.net", "<j_url>/maven"},
            {"maven.fabricmc.net", "<j_url>/maven"},
            {"launchermeta.mojang.com", "<j_url>"},
            {"launcher.mojang.com", "<j_url>"},
            {"files.minecraftforge.net", "<j_url>"},
            {"meta.fabricmc.net", "<j_url>/fabric-meta"},
            {"maven.neoforged.net/releases", "<j_url>/maven"},
            {"maven.quiltmc.org/repository/release","<j_url>/maven"},
            {"meta.quiltmc.org","<j_url>/quilt-meta"},
            {"edge.forgecdn.net",""},
            {"mediafilez.forgecdn.net",""}
        };
        for (const auto &replacement : replacements)
        {
            if (host.contains(replacement.search) && !host.contains(baseUrl))
            {
                QString replacementUrl = baseUrl.isEmpty() ? "" : baseUrl + replacement.replace;
                if (useProxy && !baseUrl.isEmpty())
                {
                    m_url = QUrl(baseUrl + host);
                }
                else if (!replacementUrl.isEmpty())
                {
                    host.replace(replacement.search, replacementUrl);
                    m_url = QUrl(host);
                    break;
                }
            }
        }
    }

    QNetworkRequest request(m_url);
    m_status = m_sink->init(request);
    switch(m_status)
    {
        case Job_Finished:
            emit succeeded(m_index_within_job);
            qDebug() << "Download cache hit " << m_url.toString();
            return;
        case Job_InProgress:
            qDebug() << "Downloading " << m_url.toString();
            break;
        case Job_Failed_Proceed: // this is meaningless in this context. We do need a sink.
        case Job_NotStarted:
        case Job_Failed:
            emit failed(m_index_within_job);
            return;
        case Job_Aborted:
            return;
    }

    request.setHeader(QNetworkRequest::UserAgentHeader, BuildConfig.USER_AGENT);
    for(auto iter = m_extra_headers.begin(); iter != m_extra_headers.end(); iter++) {
        request.setRawHeader(iter.key().toUtf8(), iter.value().toUtf8());
    }

    QNetworkReply *rep = m_network->get(request);

    m_reply.reset(rep);
    connect(rep, SIGNAL(downloadProgress(qint64, qint64)), SLOT(downloadProgress(qint64, qint64)));
    connect(rep, SIGNAL(finished()), SLOT(downloadFinished()));
    connect(rep, SIGNAL(error(QNetworkReply::NetworkError)), SLOT(downloadError(QNetworkReply::NetworkError)));
    connect(rep, &QNetworkReply::sslErrors, this, &Download::sslErrors);
    connect(rep, &QNetworkReply::readyRead, this, &Download::downloadReadyRead);
}

void Download::downloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    m_total_progress = bytesTotal;
    m_progress = bytesReceived;
    emit netActionProgress(m_index_within_job, bytesReceived, bytesTotal);
}

void Download::downloadError(QNetworkReply::NetworkError error)
{
    if(error == QNetworkReply::OperationCanceledError)
    {
        qCritical() << "Aborted " << m_url.toString();
        m_status = Job_Aborted;
    }
    else
    {
        if(m_options & Option::AcceptLocalFiles)
        {
            if(m_sink->hasLocalData())
            {
                m_status = Job_Failed_Proceed;
                return;
            }
        }
        // error happened during download.
        qCritical() << "Failed " << m_url.toString() << " with reason " << error;
        m_status = Job_Failed;
    }
}

void Download::sslErrors(const QList<QSslError> & errors)
{
    int i = 1;
    for (auto error : errors)
    {
        qCritical() << "Download" << m_url.toString() << "SSL Error #" << i << " : " << error.errorString();
        auto cert = error.certificate();
        qCritical() << "Certificate in question:\n" << cert.toText();
        i++;
    }
}

bool Download::handleRedirect()
{
    QUrl redirect = m_reply->header(QNetworkRequest::LocationHeader).toUrl();
    if(!redirect.isValid())
    {
        if(!m_reply->hasRawHeader("Location"))
        {
            // no redirect -> it's fine to continue
            return false;
        }
        // there is a Location header, but it's not correct. we need to apply some workarounds...
        QByteArray redirectBA = m_reply->rawHeader("Location");
        if(redirectBA.size() == 0)
        {
            // empty, yet present redirect header? WTF?
            return false;
        }
        QString redirectStr = QString::fromUtf8(redirectBA);

        if(redirectStr.startsWith("//"))
        {
            /*
             * IF the URL begins with //, we need to insert the URL scheme.
             * See: https://bugreports.qt.io/browse/QTBUG-41061
             * See: http://tools.ietf.org/html/rfc3986#section-4.2
             */
            redirectStr = m_reply->url().scheme() + ":" + redirectStr;
        }
        else if(redirectStr.startsWith("/"))
        {
            /*
             * IF the URL begins with /, we need to process it as a relative URL
             */
            auto url = m_reply->url();
            url.setPath(redirectStr, QUrl::TolerantMode);
            redirectStr = url.toString();
        }

        /*
         * Next, make sure the URL is parsed in tolerant mode. Qt doesn't parse the location header in tolerant mode, which causes issues.
         * FIXME: report Qt bug for this
         */
        redirect = QUrl(redirectStr, QUrl::TolerantMode);
        if(!redirect.isValid())
        {
            qWarning() << "Failed to parse redirect URL:" << redirectStr;
            downloadError(QNetworkReply::ProtocolFailure);
            return false;
        }
        qDebug() << "Fixed location header:" << redirect;
    }
    else
    {
        qDebug() << "Location header:" << redirect;
    }

    m_url = QUrl(redirect.toString());
    qDebug() << "Following redirect to " << m_url.toString();
    start(m_network);
    return true;
}


void Download::downloadFinished()
{
    // handle HTTP redirection first
    if(handleRedirect())
    {
        qDebug() << "Download redirected:" << m_url.toString();
        return;
    }

    // if the download failed before this point ...
    if (m_status == Job_Failed_Proceed)
    {
        qDebug() << "Download failed but we are allowed to proceed:" << m_url.toString();
        m_sink->abort();
        m_reply.reset();
        emit succeeded(m_index_within_job);
        return;
    }
    else if (m_status == Job_Failed)
    {
        qDebug() << "Download failed in previous step:" << m_url.toString();
        m_sink->abort();
        m_reply.reset();
        emit failed(m_index_within_job);
        return;
    }
    else if(m_status == Job_Aborted)
    {
        qDebug() << "Download aborted in previous step:" << m_url.toString();
        m_sink->abort();
        m_reply.reset();
        emit aborted(m_index_within_job);
        return;
    }

    // make sure we got all the remaining data, if any
    auto data = m_reply->readAll();
    if(data.size())
    {
        qDebug() << "Writing extra" << data.size() << "bytes to" << m_target_path;
        m_status = m_sink->write(data);
    }

    // otherwise, finalize the whole graph
    m_status = m_sink->finalize(*m_reply.get());
    if (m_status != Job_Finished)
    {
        qDebug() << "Download failed to finalize:" << m_url.toString();
        m_sink->abort();
        m_reply.reset();
        emit failed(m_index_within_job);
        return;
    }
    m_reply.reset();
    qDebug() << "Download succeeded:" << m_url.toString();
    emit succeeded(m_index_within_job);
}

void Download::downloadReadyRead()
{
    if(m_status == Job_InProgress)
    {
        auto data = m_reply->readAll();
        m_status = m_sink->write(data);
        if(m_status == Job_Failed)
        {
            qCritical() << "Failed to process response chunk for " << m_target_path;
        }
        // qDebug() << "Download" << m_url.toString() << "gained" << data.size() << "bytes";
    }
    else
    {
        qCritical() << "Cannot write to " << m_target_path << ", illegal status" << m_status;
    }
}

}

bool Net::Download::abort()
{
    if(m_reply)
    {
        m_reply->abort();
    }
    else
    {
        m_status = Job_Aborted;
    }
    return true;
}

bool Net::Download::canAbort()
{
    return true;
}

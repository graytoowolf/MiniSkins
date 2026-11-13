/* Copyright 2013-2023 MiniSkins Contributors
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
#include "Mirrors.h"
#include "RangeFileSink.h"
#include "RangePlan.h"
#include "RequestBudget.h"

#include "BuildConfig.h"

namespace Net
{
namespace
{
    // 手动跟随重定向时的最大跳数，防止重定向循环
    const int kMaxRedirects = 10;

    // 只有大于该大小的响应才尝试分片（恰好等于时不触发）
    const qint64 kSegmentThreshold = 5 * 1024 * 1024;
    // 默认并发分段数，以及允许的上限
    const int kDefaultSegments = 4;
    const int kMaxSegments = 8;
    // 单个分段请求的最大重试次数
    const int kMaxSegmentRetries = 3;

    // 日志里不输出查询串，避免暴露签名等敏感参数
    QString loggableUrl(const QUrl &url)
    {
        return url.host() + url.path();
    }

    // 分段请求遇到这些错误时值得重试该段，而不是放弃整个任务
    bool isTransientSegmentError(QNetworkReply::NetworkError error, int status)
    {
        if (status >= 500 && status <= 599)
        {
            return true;
        }
        if (error == QNetworkReply::NoError)
        {
            return false;
        }
        switch (error)
        {
            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::RemoteHostClosedError:
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::TimeoutError:
            case QNetworkReply::TemporaryNetworkFailureError:
            case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::UnknownNetworkError:
                return true;
            default:
                return false;
        }
    }

    // BMCLAPI 支持的 Mojang / 模组平台资源域名及其对应的路径前缀
    struct SourceReplacement
    {
        QString search;
        QString prefix;
    };

    const SourceReplacement bmclapiReplacements[] = {
        {"resources.download.minecraft.net", "/assets"},
        {"libraries.minecraft.net", "/maven"},
        {"maven.fabricmc.net", "/maven"},
        {"meta.fabricmc.net", "/fabric-meta"},
        {"maven.minecraftforge.net", "/maven"},
        {"files.minecraftforge.net", ""},
        {"maven.neoforged.net/releases", "/maven"},
        {"maven.quiltmc.org/repository/release", "/maven"},
        {"meta.quiltmc.org", "/quilt-meta"},
        {"dl.liteloader.com/versions/versions.json", "/maven/com/mumfrey/liteloader/versions.json"},
        {"dl.liteloader.com/versions", "/maven"},
        {"launchermeta.mojang.com", ""},
        {"launcher.mojang.com", ""},
        {"piston-meta.mojang.com", ""},
        {"piston-data.mojang.com", ""}};

    bool isKnownResourceUrl(const QString &url)
    {
        for (const auto &replacement : bmclapiReplacements)
        {
            if (url.contains(replacement.search))
            {
                return true;
            }
        }
        return false;
    }

    QString normalizeBaseUrl(QString url, const QString &fallback)
    {
        url = url.trimmed();
        if (url.isEmpty())
        {
            url = fallback;
        }
        if (url.isEmpty())
        {
            return url;
        }
        if (!url.startsWith("http://") && !url.startsWith("https://"))
        {
            url.prepend("https://");
        }
        while (url.endsWith('/'))
        {
            url.chop(1);
        }
        return url;
    }

    QUrl applyDownloadSource(const QUrl &url, const DownloadSource &source)
    {
        if (source.getType() == "Bmclapi")
        {
            const QString baseUrl = normalizeBaseUrl(source.getUrl(), DownloadSourceDefaults::bmclapiHost());
            const QString host = url.host();
            if (baseUrl.isEmpty() || host.contains(DownloadSourceDefaults::bmclapiHost()))
            {
                return url;
            }
            // 用“域名 + 路径”匹配并替换为 BMCLAPI 对应的域名与路径前缀
            const QString remainder = host + url.path();
            for (const auto &replacement : bmclapiReplacements)
            {
                if (!remainder.contains(replacement.search))
                {
                    continue;
                }
                QString newPath = remainder;
                newPath.replace(replacement.search, replacement.prefix);
                QUrl result(baseUrl);
                result.setPath(newPath);
                result.setQuery(url.query());
                return result;
            }
            return url;
        }
        if (source.getType() == "Custom")
        {
            // 自定义源：在原始地址前拼接代理地址，仅处理 Minecraft 相关资源
            const QString proxyUrl = normalizeBaseUrl(source.getUrl(), QString());
            const QString target = url.toString();
            if (proxyUrl.isEmpty() || !isKnownResourceUrl(target) || target.startsWith(proxyUrl + "/"))
            {
                return url;
            }
            return QUrl(proxyUrl + "/" + target);
        }
        return url;
    }
}

    Download::Download() : NetAction()
    {
        m_status = Job_NotStarted;
        m_stall_timer.setSingleShot(true);
        connect(&m_stall_timer, &QTimer::timeout, this, [this]()
        {
            if (m_range_stage == RangeStage::Segmenting || m_switch_to_segments)
            {
                qCritical() << "Segmented download" << loggableUrl(m_url) << "stalled, giving up";
                failSegments(QNetworkReply::TimeoutError, QStringLiteral("stalled"));
                return;
            }
            qCritical() << "Download" << m_url.toString() << "stalled, giving up";
            stop(QNetworkReply::TimeoutError);
        });
    }

    Download::~Download()
    {
        // nothing may be granted to or wait for a dead download
        RequestBudget::instance().cancel(this);
        m_stall_timer.stop();
        stopAllSegments();
        if (m_reply)
        {
            m_reply->disconnect(this);
            m_reply->abort();
        }
        m_reply.reset();
        if (m_probe_reply)
        {
            m_probe_reply->disconnect(this);
            m_probe_reply->abort();
        }
        m_probe_reply.reset();
        releaseBudgetSlot();
        if (m_range_sink)
        {
            m_range_sink->discardSegmentedWrite();
        }
    }

    Download::Ptr Download::makeCached(QUrl url, MetaEntryPtr entry, Options options)
    {
        Download *dl = new Download();
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
        Download *dl = new Download();
        dl->m_url = url;
        dl->m_options = options;
        dl->m_sink.reset(new ByteArraySink(output));
        return dl;
    }

    Download::Ptr Download::makeFile(QUrl url, QString path, Options options)
    {
        Download *dl = new Download();
        dl->m_url = url;
        dl->m_options = options;
        // 与 FileSink 行为一致，同时让大文件有机会走分段路径
        dl->m_sink.reset(new RangeFileSink(path));
        return dl;
    }

    void Download::addValidator(Validator *v)
    {
        m_sink->addValidator(v);
    }

    void Download::setStallTimeout(int msecs)
    {
        m_stall_timer.setInterval(msecs);
    }

    void Download::setSegmentCount(int count)
    {
        m_segment_count = qBound(1, count, kMaxSegments);
    }

    void Download::stop(QNetworkReply::NetworkError error)
    {
        // the reply's abort() comes back through downloadError() and downloadFinished(), which see Job_Failed
        m_status = Job_Failed;
        m_network_error = error;
        m_stall_timer.stop();
        if (m_reply)
        {
            m_reply->abort();
        }
    }

    void Download::startImpl()
    {
        if (m_status == Job_Aborted)
        {
            qWarning() << "Attempt to start an aborted Download:" << m_url.toString();
            emit aborted(m_index_within_job);
            return;
        }
        // 每个顶层尝试都从干净的分段状态开始
        m_range_sink = dynamic_cast<RangeFileSink *>(m_sink.get());
        m_range_stage = RangeStage::None;
        m_range_stopping = false;
        m_switch_to_segments = false;
        m_segments_attempted = false;
        m_range_total = -1;
        m_range_received = 0;
        m_range_validator.clear();
        m_range_etag.clear();
        m_active_segments = 0;
        m_segments.clear();
        m_segment_queue.clear();
        m_probe_reply.reset();

        // 应用本地配置中选中的下载源（测试等场景下实例可能不是 Application，需要判断类型）
        auto app = qobject_cast<Application *>(QCoreApplication::instance());
        if (app)
        {
            DownloadSource source = app->getSelectedDownloadSource();
            if (source.getType() != "Mojang")
            {
                m_url = applyDownloadSource(m_url, source);
            }
        }

        // every attempt starts again from the first mirror of the URL it was made for
        if (!m_requested_url.isValid())
        {
            m_requested_url = m_url;
        }
        m_mirrors = (m_options & Option::NoMirrors) ? QList<QUrl>{m_requested_url} : Mirrors::candidates(m_requested_url);
        m_mirror = 0;
        m_mirrorSaidNotFound = false;
        m_url = m_mirrors.first();
        startRequest();
    }

    void Download::startRequest()
    {
        m_network_error = QNetworkReply::NoError;
        m_http_status = 0;

        QNetworkRequest request = buildRequest();
        m_status = m_sink->init(request);
        switch (m_status)
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

        m_redirect_count = 0;
        sendRequest(request);
    }

    QNetworkRequest Download::buildRequest() const
    {
        QNetworkRequest request(m_url);
        // Qt 5.6 起支持内建跟随 301/302/303/307，由 QNetworkReply 内部完成跳转，
        // 这样 sink 只初始化一次，重定向地址也不会再次套用下载源转换
        request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
        return request;
    }

    void Download::sendRequest(QNetworkRequest &request)
    {
        if (m_holds_budget_slot)
        {
            doSendRequest(request);
            return;
        }
        QNetworkRequest copy = request;
        m_waiting_for_budget = true;
        RequestBudget::instance().acquire(this, [this, copy]() mutable
        {
            m_waiting_for_budget = false;
            if (m_status == Job_Aborted || m_status == Job_Failed)
            {
                // the download went away while this request was waiting for a slot
                RequestBudget::instance().release();
                return;
            }
            doSendRequest(copy);
        });
    }

    void Download::doSendRequest(QNetworkRequest request)
    {
        request.setHeader(QNetworkRequest::UserAgentHeader, BuildConfig.USER_AGENT);
        for (auto iter = m_extra_headers.begin(); iter != m_extra_headers.end(); iter++)
        {
            request.setRawHeader(iter.key().toUtf8(), iter.value().toUtf8());
        }

        // the slot is ours from now on, a reply that finishes early must be able to free it
        m_holds_budget_slot = true;
        QNetworkReply *rep = m_network->get(request);

        m_reply.reset(rep);
        connect(rep, SIGNAL(downloadProgress(qint64, qint64)), SLOT(downloadProgress(qint64, qint64)));
        connect(rep, SIGNAL(finished()), SLOT(downloadFinished()));
        connect(rep, SIGNAL(error(QNetworkReply::NetworkError)), SLOT(downloadError(QNetworkReply::NetworkError)));
        connect(rep, &QNetworkReply::sslErrors, this, &Download::sslErrors);
        connect(rep, &QNetworkReply::readyRead, this, &Download::downloadReadyRead);
        if (m_stall_timer.interval() > 0)
        {
            m_stall_timer.start();
        }
    }

    void Download::releaseBudgetSlot()
    {
        if (!m_holds_budget_slot)
        {
            return;
        }
        m_holds_budget_slot = false;
        RequestBudget::instance().release();
    }

    void Download::downloadProgress(qint64 bytesReceived, qint64 bytesTotal)
    {
        m_total_progress = bytesTotal;
        m_progress = bytesReceived;
        if (m_stall_timer.isActive())
        {
            m_stall_timer.start();
        }
        emit netActionProgress(m_index_within_job, bytesReceived, bytesTotal);
    }

    void Download::downloadError(QNetworkReply::NetworkError error)
    {
        if (!m_reply)
        {
            return;
        }
        if (sender() && sender() != m_reply.get())
        {
            return;
        }
        if (m_switch_to_segments || m_range_stage != RangeStage::None)
        {
            // the probe was aborted on purpose: the body is fetched as segments instead
            return;
        }
        if (error == QNetworkReply::OperationCanceledError)
        {
            if (m_status == Job_Failed)
            {
                // stopped by us, not aborted: the data was refused, or the transfer stalled
                return;
            }
            qCritical() << "Aborted " << m_url.toString();
            m_status = Job_Aborted;
        }
        else
        {
            m_network_error = error;
            if (m_options & Option::AcceptLocalFiles)
            {
                if (m_sink->hasLocalData())
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

    void Download::sslErrors(const QList<QSslError> &errors)
    {
        int i = 1;
        for (auto error : errors)
        {
            qCritical() << "Download" << m_url.toString() << "SSL Error #" << i << " : " << error.errorString();
            auto cert = error.certificate();
            qCritical() << "Certificate in question:\n"
                        << cert.toText();

            // 忽略过期证书错误（证书已过期，可能是测试环境或临时问题）
            if (error.error() == QSslError::CertificateExpired)
            {
                qWarning() << "Ignoring expired certificate for:" << m_url.toString();
                continue;
            }

            i++;
        }
    }

    bool Download::handleRedirect()
    {
        /*
         * Qt 5.6 自动跟随重定向后，最终响应（例如 200）上仍可能残留上一跳 302 的 Location 头。
         * 若只凭 Location 头判断，会把普通响应再次当作重定向，导致同一文件被下载两次、
         * 校验必然失败。因此只有 HTTP 状态码是 3xx 时才认为是需要手动跟随的重定向。
         */
        const QVariant statusVar = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (statusVar.isValid())
        {
            const int statusCode = statusVar.toInt();
            if (statusCode < 300 || statusCode > 399)
            {
                return false;
            }
        }

        QUrl redirect = m_reply->header(QNetworkRequest::LocationHeader).toUrl();
        if (!redirect.isValid())
        {
            if (!m_reply->hasRawHeader("Location"))
            {
                // no redirect -> it's fine to continue
                return false;
            }
            // there is a Location header, but it's not correct. we need to apply some workarounds...
            QByteArray redirectBA = m_reply->rawHeader("Location");
            if (redirectBA.size() == 0)
            {
                // empty, yet present redirect header? WTF?
                return false;
            }
            QString redirectStr = QString::fromUtf8(redirectBA);

            if (redirectStr.startsWith("//"))
            {
                /*
                 * IF the URL begins with //, we need to insert the URL scheme.
                 * See: https://bugreports.qt.io/browse/QTBUG-41061
                 * See: http://tools.ietf.org/html/rfc3986#section-4.2
                 */
                redirectStr = m_reply->url().scheme() + ":" + redirectStr;
            }
            else if (redirectStr.startsWith("/"))
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
            if (!redirect.isValid())
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

        if (m_redirect_count >= kMaxRedirects)
        {
            qCritical() << "Too many redirects for " << m_url.toString();
            m_status = Job_Failed;
            return false;
        }
        m_redirect_count++;

        m_url = QUrl(redirect.toString());
        qDebug() << "Following redirect to " << m_url.toString();

        // 兜底跟随：只重新发起请求，不重复初始化 sink，也不再套用下载源转换
        QNetworkRequest request = buildRequest();
        sendRequest(request);
        return true;
    }

    bool Download::tryNextMirror()
    {
        // only failures to reach the host at all, not HTTP errors or refused data
        const bool unreachable = m_network_error > QNetworkReply::NoError && m_network_error < QNetworkReply::ProxyConnectionRefusedError
            && m_network_error != QNetworkReply::OperationCanceledError;
        if (unreachable)
        {
            Mirrors::markUnreachable(m_url);
        }
        if (wasNotFound())
        {
            m_mirrorSaidNotFound = true;
        }
        if (m_mirror + 1 >= m_mirrors.size())
        {
            // a mirror that answered knows better than ones that couldn't be reached: the file isn't there
            if (unreachable && m_mirrorSaidNotFound)
            {
                m_network_error = QNetworkReply::ContentNotFoundError;
            }
            return false;
        }
        m_mirror++;
        m_url = m_mirrors[m_mirror];
        qWarning() << "Trying the next mirror:" << m_url.toString();
        startRequest();
        return true;
    }

    void Download::downloadFinished()
    {
        if (!m_reply)
        {
            // a reply we already handed over or tore down
            return;
        }
        if (sender() && sender() != m_reply.get())
        {
            return;
        }

        m_stall_timer.stop();
        // the response is over, so its request slot is free again; a redirect takes a new one
        releaseBudgetSlot();

        bool switchedToSegments = false;
        if (m_status == Job_InProgress)
        {
            // the whole body may have fit into the first packet, or arrived without a readyRead
            switchedToSegments = m_switch_to_segments ||
                                 (m_range_stage == RangeStage::None && tryStartSegments());
        }
        if (switchedToSegments)
        {
            m_switch_to_segments = false;
            // the probe response is only kept for its headers; its body is not written
            m_probe_reply = std::move(m_reply);
            m_reply.reset();
            const QVariant probeStatus = m_probe_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            m_http_status = probeStatus.isValid() ? probeStatus.toInt() : 0;
            startSegments();
            return;
        }
        m_switch_to_segments = false;

        auto status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        m_http_status = status.isValid() ? status.toInt() : 0;

        // handle HTTP redirection first, unless it was stopped
        if (m_status != Job_Failed && handleRedirect())
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
            if (!tryNextMirror())
            {
                emit failed(m_index_within_job);
            }
            return;
        }
        else if (m_status == Job_Aborted)
        {
            qDebug() << "Download aborted in previous step:" << m_url.toString();
            m_sink->abort();
            m_reply.reset();
            emit aborted(m_index_within_job);
            return;
        }

        // make sure we got all the remaining data, if any
        auto data = m_reply->readAll();
        if (data.size())
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
            if (!tryNextMirror())
            {
                emit failed(m_index_within_job);
            }
            return;
        }
        m_reply.reset();
        qDebug() << "Download succeeded:" << m_url.toString();
        emit succeeded(m_index_within_job);
    }

    void Download::downloadReadyRead()
    {
        if (!m_reply)
        {
            return;
        }
        if (sender() && sender() != m_reply.get())
        {
            // a readyRead from a reply we are no longer reading
            return;
        }
        if (m_switch_to_segments || m_range_stage != RangeStage::None)
        {
            // the body of a probe or of a segment is never written by the regular path
            return;
        }
        if (m_status == Job_InProgress && tryStartSegments())
        {
            // decide before writing a single body byte; the abort comes back through
            // downloadError() and downloadFinished(), which start the segments
            m_reply->abort();
            return;
        }
        if (m_status == Job_InProgress)
        {
            auto data = m_reply->readAll();
            m_status = m_sink->write(data);
            if (m_status == Job_Failed)
            {
                // nothing more is wanted from this response
                qCritical() << "Failed to process response chunk for " << m_url.toString();
                stop(QNetworkReply::NoError);
            }
            // qDebug() << "Download" << m_url.toString() << "gained" << data.size() << "bytes";
        }
        else
        {
            qCritical() << "Cannot write to " << m_target_path << ", illegal status" << m_status;
        }
    }

    bool Download::tryStartSegments()
    {
        if (m_segments_attempted || m_switch_to_segments || m_range_stage != RangeStage::None)
        {
            return false;
        }
        if (!m_reply || !m_range_sink)
        {
            return false;
        }
        QNetworkReply *reply = m_reply.get();
        const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (!statusAttribute.isValid() || statusAttribute.toInt() != 200)
        {
            return false;
        }
        bool ok = false;
        const qint64 total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
        if (!ok || total <= kSegmentThreshold)
        {
            return false;
        }
        // ranges of an encoded body are not ranges of the file
        const QByteArray contentEncoding = reply->rawHeader("Content-Encoding");
        if (!contentEncoding.isEmpty() && contentEncoding.toLower() != "identity")
        {
            return false;
        }
        const QByteArray etag = reply->rawHeader("ETag");
        const bool hasStrongEtag = !etag.isEmpty() && !etag.startsWith("W/");
        const QByteArray lastModified = reply->rawHeader("Last-Modified");
        if (!hasStrongEtag && lastModified.isEmpty())
        {
            // without a strong validator there is no safe way to mix segment responses
            return false;
        }

        m_segments_attempted = true;
        m_switch_to_segments = true;
        m_range_total = total;
        m_range_received = 0;
        // a strong ETag is the only thing that guarantees one version across segments
        m_range_validator = hasStrongEtag ? etag : lastModified;
        m_range_etag = hasStrongEtag ? etag : QByteArray();
        m_total_progress = total;
        qDebug() << "Range candidate" << loggableUrl(m_url) << "size" << total << "strong ETag"
                 << hasStrongEtag << "Last-Modified" << !lastModified.isEmpty();
        return true;
    }

    void Download::startSegments()
    {
        m_range_stage = RangeStage::Segmenting;
        m_range_stopping = false;
        m_range_received = 0;
        m_active_segments = 0;
        m_segments.clear();
        m_segment_queue.clear();

        if (!m_range_sink)
        {
            failSegments(QNetworkReply::NoError, QStringLiteral("no range capable sink"));
            return;
        }
        if (m_range_total <= 0)
        {
            failSegments(QNetworkReply::NoError, QStringLiteral("invalid total size"));
            return;
        }
        if (m_range_sink->beginSegmentedWrite(m_range_total) != Job_InProgress)
        {
            failSegments(QNetworkReply::NoError, QStringLiteral("could not create the temporary file"));
            return;
        }

        int segmentCount = m_segment_count > 0 ? m_segment_count : kDefaultSegments;
        if (segmentCount > kMaxSegments)
        {
            segmentCount = kMaxSegments;
        }
        const QVector<ByteRange> plan = planSegments(m_range_total, segmentCount);
        if (!validatePlan(plan, m_range_total))
        {
            failSegments(QNetworkReply::NoError, QStringLiteral("invalid segment plan"));
            return;
        }

        m_segments.resize(plan.size());
        for (int i = 0; i < plan.size(); ++i)
        {
            m_segments[i].start = plan.at(i).start;
            m_segments[i].end = plan.at(i).end;
            m_segment_queue.enqueue(i);
            qDebug() << "Range segment" << i << ":" << plan.at(i).start << "-" << plan.at(i).end;
        }
        qDebug() << "Segmented download of" << loggableUrl(m_url) << "into"
                 << static_cast<int>(m_segments.size()) << "segments," << m_range_total << "bytes";
        emitRangeProgress();
        startMoreSegments();
    }

    void Download::startMoreSegments()
    {
        if (!segmentsRunning())
        {
            return;
        }
        int segmentCount = m_segment_count > 0 ? m_segment_count : kDefaultSegments;
        if (segmentCount > kMaxSegments)
        {
            segmentCount = kMaxSegments;
        }
        while (m_active_segments < segmentCount && !m_segment_queue.isEmpty())
        {
            const int index = m_segment_queue.dequeue();
            if (index < 0 || index >= static_cast<int>(m_segments.size()) || m_segments[index].done)
            {
                continue;
            }
            ++m_active_segments;
            m_segments[index].active = true;
            qDebug() << "Range starting segment" << index << "active" << m_active_segments
                     << "budget active" << RequestBudget::instance().activeCount();
            RequestBudget::instance().acquire(this, [this, index]()
            {
                if (!segmentsRunning())
                {
                    if (m_active_segments > 0)
                    {
                        --m_active_segments;
                    }
                    RequestBudget::instance().release();
                    return;
                }
                startSegment(index);
            });
        }
    }

    void Download::startSegment(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_segments.size()) || !segmentsRunning())
        {
            // the slot was granted for this segment, so give it back
            if (m_active_segments > 0)
            {
                --m_active_segments;
            }
            RequestBudget::instance().release();
            return;
        }
        SegmentState &segment = m_segments[index];
        QNetworkRequest request = buildRequest();
        request.setHeader(QNetworkRequest::UserAgentHeader, BuildConfig.USER_AGENT);
        for (auto iter = m_extra_headers.begin(); iter != m_extra_headers.end(); iter++)
        {
            request.setRawHeader(iter.key().toUtf8(), iter.value().toUtf8());
        }
        const QByteArray rangeHeader = "bytes=" + QByteArray::number(segment.start) + "-" +
                                       QByteArray::number(segment.end);
        request.setRawHeader("Range", rangeHeader);
        if (!m_range_validator.isEmpty())
        {
            request.setRawHeader("If-Range", m_range_validator);
        }

        QNetworkReply *reply = m_network->get(request);
        segment.reply = reply;
        connect(reply, &QNetworkReply::readyRead, this, [this, index, reply]()
        {
            segmentReadyRead(index, reply);
        });
        connect(reply, &QNetworkReply::finished, this, [this, index, reply]()
        {
            segmentFinished(index, reply);
        });
        if (m_stall_timer.interval() > 0)
        {
            m_stall_timer.start();
        }
        qDebug() << "Range request" << rangeHeader << "for" << loggableUrl(m_url);
    }

    void Download::segmentReadyRead(int index, QNetworkReply *reply)
    {
        if (m_range_stopping || !segmentsRunning())
        {
            return;
        }
        if (index < 0 || index >= static_cast<int>(m_segments.size()))
        {
            return;
        }
        SegmentState &segment = m_segments[index];
        if (!reply || segment.reply != reply || !m_range_sink)
        {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200)
        {
            // the server ignored the Range header: a regular download is the only option
            fallbackToRegularDownload(QStringLiteral("server ignored Range"));
            return;
        }

        QByteArray data = reply->readAll();
        if (data.isEmpty())
        {
            return;
        }
        const qint64 segmentLength = segment.end - segment.start + 1;
        if (segment.received + data.size() > segmentLength)
        {
            fallbackToRegularDownload(QStringLiteral("more data than the range holds"));
            return;
        }
        if (m_range_sink->writeAt(segment.start + segment.received, data) != Job_InProgress)
        {
            failSegments(QNetworkReply::NoError, QStringLiteral("could not write to the temporary file"));
            return;
        }
        segment.received += data.size();
        m_range_received += data.size();
        emitRangeProgress();
        if (m_stall_timer.isActive())
        {
            m_stall_timer.start();
        }
    }

    void Download::segmentFinished(int index, QNetworkReply *reply)
    {
        if (m_range_stopping)
        {
            return;
        }
        if (index < 0 || index >= static_cast<int>(m_segments.size()))
        {
            return;
        }
        SegmentState &segment = m_segments[index];
        if (!reply || segment.reply != reply)
        {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray contentRangeHeader = reply->rawHeader("Content-Range");
        const QByteArray etag = reply->rawHeader("ETag");

        // this reply is done: free its slot before anything else can run
        segment.reply = nullptr;
        segment.active = false;
        reply->disconnect(this);
        reply->deleteLater();
        RequestBudget::instance().release();
        if (m_active_segments > 0)
        {
            --m_active_segments;
        }

        if (!segmentsRunning())
        {
            return;
        }

        if (status == 200 || status == 416)
        {
            fallbackToRegularDownload(QStringLiteral("unexpected HTTP status %1").arg(status));
            return;
        }
        if (status != 206)
        {
            if (isTransientSegmentError(error, status))
            {
                retrySegment(index, QStringLiteral("network error %1").arg(static_cast<int>(error)), error);
            }
            else
            {
                fallbackToRegularDownload(QStringLiteral("unexpected HTTP status %1").arg(status));
            }
            return;
        }
        if (error != QNetworkReply::NoError)
        {
            retrySegment(index, QStringLiteral("network error %1").arg(static_cast<int>(error)), error);
            return;
        }

        const ContentRange parsed = parseContentRange(contentRangeHeader);
        if (!parsed.valid || parsed.start != segment.start || parsed.end != segment.end ||
            parsed.total != m_range_total)
        {
            fallbackToRegularDownload(QStringLiteral("invalid Content-Range"));
            return;
        }
        if (!m_range_etag.isEmpty() && etag != m_range_etag)
        {
            // the file changed under us: never mix two versions
            fallbackToRegularDownload(QStringLiteral("ETag changed"));
            return;
        }

        const qint64 segmentLength = segment.end - segment.start + 1;
        if (segment.received != segmentLength)
        {
            retrySegment(index, QStringLiteral("short segment"), error);
            return;
        }

        segment.done = true;
        qDebug() << "Range segment" << index << "done, received" << m_range_received << "of"
                 << m_range_total;

        bool allDone = true;
        for (int i = 0; i < static_cast<int>(m_segments.size()); ++i)
        {
            if (!m_segments[i].done)
            {
                allDone = false;
                break;
            }
        }
        if (allDone)
        {
            finishSegments();
            return;
        }
        if (m_active_segments == 0)
        {
            m_stall_timer.stop();
        }
        startMoreSegments();
    }

    void Download::retrySegment(int index, const QString &reason, QNetworkReply::NetworkError error)
    {
        if (!segmentsRunning() || index < 0 || index >= static_cast<int>(m_segments.size()))
        {
            return;
        }
        SegmentState &segment = m_segments[index];
        if (segment.done)
        {
            return;
        }
        if (segment.attempts >= kMaxSegmentRetries)
        {
            failSegments(error,
                         QStringLiteral("segment %1 failed after %2 retries: %3")
                             .arg(index)
                             .arg(segment.attempts)
                             .arg(reason));
            return;
        }
        ++segment.attempts;
        // the bytes of a failed attempt aren't part of the result anymore
        m_range_received -= segment.received;
        if (m_range_received < 0)
        {
            m_range_received = 0;
        }
        segment.received = 0;
        qWarning() << "Range segment" << index << "retry" << segment.attempts << "because" << reason;
        m_segment_queue.enqueue(index);
        emitRangeProgress();
        startMoreSegments();
    }

    void Download::finishSegments()
    {
        m_stall_timer.stop();
        m_range_stage = RangeStage::None;
        m_active_segments = 0;
        qDebug() << "Segmented download assembled" << loggableUrl(m_url) << "size" << m_range_total
                 << "budget active" << RequestBudget::instance().activeCount();
        if (!m_probe_reply)
        {
            failSegments(QNetworkReply::NoError, QStringLiteral("probe response is gone"));
            return;
        }

        const JobStatus finalStatus = m_sink->finalize(*m_probe_reply);
        if (finalStatus == Job_Finished)
        {
            m_status = Job_Finished;
            m_progress = m_range_total;
            m_total_progress = m_range_total;
            m_probe_reply.reset();
            m_segments.clear();
            m_segment_queue.clear();
            qDebug() << "Segmented download committed" << loggableUrl(m_url);
            emit succeeded(m_index_within_job);
        }
        else
        {
            m_sink->abort();
            m_status = Job_Failed;
            m_network_error = QNetworkReply::NoError;
            m_probe_reply.reset();
            m_segments.clear();
            m_segment_queue.clear();
            qCritical() << "Segmented download failed to validate or commit" << loggableUrl(m_url);
            emit failed(m_index_within_job);
        }
    }

    void Download::failSegments(QNetworkReply::NetworkError error, const QString &reason)
    {
        qCritical() << "Segmented download of" << loggableUrl(m_url) << "failed:" << reason;
        // a probe still in flight (never turned into segments) must not linger either
        if (m_reply)
        {
            m_reply->disconnect(this);
            m_reply->abort();
            m_reply.reset();
        }
        releaseBudgetSlot();
        stopAllSegments();
        if (m_range_sink)
        {
            m_range_sink->discardSegmentedWrite();
        }
        m_segments.clear();
        m_segment_queue.clear();
        m_probe_reply.reset();
        m_range_stage = RangeStage::None;
        m_switch_to_segments = false;
        m_range_stopping = false;
        m_range_total = -1;
        m_range_received = 0;
        m_range_validator.clear();
        m_range_etag.clear();
        m_active_segments = 0;
        m_network_error = error;
        m_status = Job_Failed;
        emit failed(m_index_within_job);
    }

    void Download::fallbackToRegularDownload(const QString &reason)
    {
        qWarning() << "Range download of" << loggableUrl(m_url) << "fell back to a regular download:"
                   << reason;
        // no probe may survive into the regular request
        if (m_reply)
        {
            m_reply->disconnect(this);
            m_reply->abort();
            m_reply.reset();
        }
        releaseBudgetSlot();
        stopAllSegments();
        if (m_range_sink)
        {
            m_range_sink->discardSegmentedWrite();
        }
        m_segments.clear();
        m_segment_queue.clear();
        m_probe_reply.reset();
        m_range_stage = RangeStage::None;
        m_switch_to_segments = false;
        m_range_stopping = false;
        m_range_total = -1;
        m_range_received = 0;
        m_range_validator.clear();
        m_range_etag.clear();
        m_active_segments = 0;
        m_progress = 0;
        m_total_progress = 1;
        m_status = Job_InProgress;
        m_network_error = QNetworkReply::NoError;
        m_http_status = 0;
        startRequest();
    }

    void Download::stopAllSegments()
    {
        m_range_stopping = true;
        for (int i = 0; i < static_cast<int>(m_segments.size()); ++i)
        {
            SegmentState &segment = m_segments[i];
            QNetworkReply *reply = segment.reply;
            segment.reply = nullptr;
            segment.active = false;
            if (!reply)
            {
                continue;
            }
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
            // every started segment holds exactly one slot
            RequestBudget::instance().release();
        }
        m_active_segments = 0;
        m_segment_queue.clear();
        // also drops segment requests that were still waiting for a slot
        RequestBudget::instance().cancel(this);
        m_stall_timer.stop();
    }

    bool Download::segmentsRunning() const
    {
        return m_range_stage == RangeStage::Segmenting && !m_range_stopping && m_status == Job_InProgress;
    }

    void Download::emitRangeProgress()
    {
        if (m_range_total <= 0)
        {
            return;
        }
        qint64 current = m_range_received;
        if (current > m_range_total)
        {
            current = m_range_total;
        }
        if (current < m_progress)
        {
            // never report backwards progress, even after a retry gives bytes back
            current = m_progress;
        }
        m_progress = current;
        m_total_progress = m_range_total;
        emit netActionProgress(m_index_within_job, current, m_range_total);
    }

}

bool Net::Download::abort()
{
    if (m_range_stage == RangeStage::Segmenting || m_switch_to_segments)
    {
        // stop everything first so no late signal can start more work
        m_status = Job_Aborted;
        m_switch_to_segments = false;
        m_range_stopping = true;
        stopAllSegments();
        if (m_range_sink)
        {
            m_range_sink->discardSegmentedWrite();
        }
        m_segments.clear();
        m_segment_queue.clear();
        if (m_reply)
        {
            // drop our interest before aborting: the abort may emit finished right away
            m_reply->disconnect(this);
            m_reply->abort();
        }
        m_reply.reset();
        m_probe_reply.reset();
        releaseBudgetSlot();
        m_range_stage = RangeStage::None;
        m_range_stopping = false;
        m_stall_timer.stop();
        qDebug() << "Segmented download aborted:" << loggableUrl(m_url);
        emit aborted(m_index_within_job);
        return true;
    }
    if (m_reply)
    {
        m_reply->abort();
        return true;
    }
    if (m_waiting_for_budget)
    {
        // nothing was sent yet: cancel the pending grant and end the task here, so NetJob isn't stuck
        RequestBudget::instance().cancel(this);
        m_waiting_for_budget = false;
        releaseBudgetSlot();
        m_status = Job_Aborted;
        m_stall_timer.stop();
        qDebug() << "Download aborted while waiting for a request slot:" << loggableUrl(m_url);
        emit aborted(m_index_within_job);
        return true;
    }
    m_status = Job_Aborted;
    return true;
}

bool Net::Download::canAbort()
{
    return true;
}

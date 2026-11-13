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

#pragma once

#include "NetAction.h"
#include "HttpMetaCache.h"
#include "Validator.h"
#include "Sink.h"

#include <QMap>
#include <QQueue>

#include <vector>

#include "QObjectPtr.h"

#include <QTimer>

namespace Net
{
    class RangeFileSink;

    class Download : public NetAction
    {
        Q_OBJECT

    public: /* types */
        typedef shared_qobject_ptr<class Download> Ptr;
        enum class Option
        {
            NoOptions = 0,
            AcceptLocalFiles = 1,
            /// only the URL given, without falling back to the copies of its site (for a caller that
            /// goes through Net::Mirrors::candidates() itself, keeping files of one mirror together)
            NoMirrors = 2
        };
        Q_DECLARE_FLAGS(Options, Option)

    protected: /* con/des */
        explicit Download();

    public:
        ~Download() override;
        static Download::Ptr makeCached(QUrl url, MetaEntryPtr entry, Options options = Option::NoOptions);
        static Download::Ptr makeByteArray(QUrl url, QByteArray *output, Options options = Option::NoOptions);
        static Download::Ptr makeFile(QUrl url, QString path, Options options = Option::NoOptions);

    public: /* methods */
        QString getTargetFilepath()
        {
            return m_target_path;
        }
        void addValidator(Validator *v);
        /// fail the download when no data arrives for this long; 0 (the default) waits forever
        void setStallTimeout(int msecs);
        /// how many segments a single file may be split into (1..8, 4 unless changed)
        void setSegmentCount(int count);
        bool abort() override;
        bool canAbort() override;

    private: /* methods */
        bool handleRedirect();
        void stop(QNetworkReply::NetworkError error);
        /// starts a request for m_url, without going back to the first mirror
        void startRequest();
        /// after a failure, starts a request for the next mirror if there is one
        bool tryNextMirror();
        QNetworkRequest buildRequest() const;
        /// takes a request budget slot and sends the request once it has one
        void sendRequest(QNetworkRequest &request);
        /// sends the request, assumes the budget slot is held
        void doSendRequest(QNetworkRequest request);
        void releaseBudgetSlot();

        /// decides whether this response should be fetched as byte ranges, without consuming it
        bool tryStartSegments();
        void startSegments();
        void startMoreSegments();
        void startSegment(int index);
        void segmentReadyRead(int index, QNetworkReply *reply);
        void segmentFinished(int index, QNetworkReply *reply);
        void retrySegment(int index, const QString &reason, QNetworkReply::NetworkError error = QNetworkReply::NoError);
        void finishSegments();
        /// stops everything, deletes the partial file and fails without committing
        void failSegments(QNetworkReply::NetworkError error, const QString &reason);
        /// drops the segments and downloads the file as one regular response again
        void fallbackToRegularDownload(const QString &reason);
        /// aborts and deletes all segment replies, frees their budget slots and empties the queue
        void stopAllSegments();
        bool segmentsRunning() const;
        void emitRangeProgress();

    private: /* types */
        enum class RangeStage
        {
            None,
            Probing,
            Segmenting
        };

        struct SegmentState
        {
            qint64 start = 0;
            qint64 end = 0;
            qint64 received = 0;
            int attempts = 0;
            bool done = false;
            bool active = false;
            QNetworkReply *reply = nullptr;
        };

    protected slots:
        void downloadProgress(qint64 bytesReceived, qint64 bytesTotal) override;
        void downloadError(QNetworkReply::NetworkError error) override;
        void sslErrors(const QList<QSslError> &errors);
        void downloadFinished() override;
        void downloadReadyRead() override;

    public slots:
        void startImpl() override;

    private: /* data */
        // FIXME: remove this, it has no business being here.
        QString m_target_path;
        std::unique_ptr<Sink> m_sink;
        Options m_options;
        // 手动重定向兜底时已跟随的跳数，避免重定向循环
        int m_redirect_count = 0;
        QTimer m_stall_timer;
        /// the URL the download was made for; m_url is the one being requested
        QUrl m_requested_url;
        /// the URLs to try, in order (Net::Mirrors), and which one is being tried
        QList<QUrl> m_mirrors;
        int m_mirror = 0;
        /// a mirror tried so far answered that the file isn't there
        bool m_mirrorSaidNotFound = false;

        /// the sink as a range sink, when it is one; not owned
        RangeFileSink *m_range_sink = nullptr;
        RangeStage m_range_stage = RangeStage::None;
        /// the response turned out to be segmentable, it is about to be turned into a probe
        bool m_switch_to_segments = false;
        /// segment replies are being torn down, their slots must not react
        bool m_range_stopping = false;
        /// segmentation is only attempted once per attempt, so a fallback can't loop
        bool m_segments_attempted = false;
        bool m_holds_budget_slot = false;
        bool m_waiting_for_budget = false;
        qint64 m_range_total = -1;
        qint64 m_range_received = 0;
        /// 0 means the default; see setSegmentCount()
        int m_segment_count = 0;
        /// strong ETag, or Last-Modified when there is no strong ETag; sent as If-Range
        QByteArray m_range_validator;
        /// strong ETag only, every segment has to come from the same version
        QByteArray m_range_etag;
        int m_active_segments = 0;
        std::vector<SegmentState> m_segments;
        QQueue<int> m_segment_queue;
        /// the finished probe response, kept for its headers until the segmented file is committed
        unique_qobject_ptr<QNetworkReply> m_probe_reply;
    };
}

Q_DECLARE_OPERATORS_FOR_FLAGS(Net::Download::Options)

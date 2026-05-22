#pragma once

#include "fingerprint.h"
#include "net/NetAction.h"

#include <QList>
#include <QMap>
#include <QTimer>

namespace CurseForge
{
class ModFingerprintLookupTask : public NetAction
{
    Q_OBJECT
public:
    using Ptr = shared_qobject_ptr<ModFingerprintLookupTask>;

    explicit ModFingerprintLookupTask(const QList<fingerprint::ModInfo> &mods);

    static Ptr make(const QList<fingerprint::ModInfo> &mods)
    {
        return Ptr(new ModFingerprintLookupTask(mods));
    }

    QList<fingerprint::ModInfo> results() const
    {
        return m_results;
    }

    bool abort() override;
    bool canAbort() override;

protected slots:
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal) override;
    void downloadError(QNetworkReply::NetworkError error) override;
    void downloadFinished() override;
    void downloadReadyRead() override {}

public slots:
    void startImpl() override;

private:
    void finish(JobStatus status);

private:
    QList<fingerprint::ModInfo> m_results;
    QMap<qulonglong, int> m_fingerprintToIndex;
    QTimer m_timeoutTimer;
    bool m_finished = true;
};
}

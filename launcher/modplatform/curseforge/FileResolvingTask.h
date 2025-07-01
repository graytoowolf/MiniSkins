#pragma once

#include "tasks/Task.h"
#include "net/NetJob.h"
#include "PackManifest.h"

namespace CurseForge
{
    struct ComparisonResult
    {
        QJsonArray filesToDownload; // 需要下载的fileID列表
        QStringList filesToDelete;  // 需要删除的文件名列表
    };

    class FileResolvingTask : public Task
    {
        Q_OBJECT
    public:
        explicit FileResolvingTask(shared_qobject_ptr<QNetworkAccessManager> network, CurseForge::Manifest &toProcess, const QString &path = QString());
        virtual ~FileResolvingTask() {};

        const CurseForge::Manifest &getResults() const
        {
            return m_toProcess;
        }

    protected:
        virtual void executeTask() override;

    private:
        CurseForge::ComparisonResult compareManifests(const QString &jsonFilePathA);

    protected slots:
        void netJobFinished(QNetworkReply *reply);
        void netJobprogress(qint64 current, qint64 total);
        void processModData(const QJsonArray &dataArray);
        QString getTargetFolderByClassId(int classId);
        void prepareDownloads();
        void downloadFinished(QNetworkReply *reply);
        void modInfoFinished(QNetworkReply *reply);

    private: /* data */
        shared_qobject_ptr<QNetworkAccessManager> m_network;
        CurseForge::Manifest m_toProcess;
        QVector<QByteArray> results;
        NetJob::Ptr m_dljob;
        QString m_path;
        QString m_filePath;
        // 移除了 m_rep 成员变量，现在每个网络请求都使用独立的 QNetworkReply 对象
    };
}

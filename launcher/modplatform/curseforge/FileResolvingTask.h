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
        void processWhitelistedMods();
        void continueExecution();
        void processNextModInQueue();

    protected slots:
        void netJobFinished();
        void netJobprogress(qint64 current, qint64 total);
        void processModData(const QJsonArray &dataArray);
        QString getTargetFolderByClassId(int classId);
        void prepareDownloads();
        void downloadFinished();

    private: /* data */
        shared_qobject_ptr<QNetworkAccessManager> m_network;
        CurseForge::Manifest m_toProcess;
        QVector<QByteArray> results;
        NetJob::Ptr m_dljob;
        QString m_path;
        QString m_filePath;
        QNetworkReply *m_rep;

        // 用于处理MOD依赖的成员变量
        QQueue<int> m_modsToQueryQueue; // 待处理的MOD ID队列
        QSet<int> m_processedModIds; // 已处理的MOD ID集合
        QList<NetJob*> m_activeNetJobs; // 活动的网络请求任务
        QString m_currentMcVersion; // 当前Minecraft版本
        int m_currentModLoaderType; // 当前模组加载器类型
        QSet<int> m_initialManifestModIds; // 初始清单中的MOD ID集合
    };
}

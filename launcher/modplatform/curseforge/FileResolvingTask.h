#pragma once

#include "tasks/Task.h"
#include "net/NetJob.h"
#include "PackManifest.h"
#include "ModpackUpdateContext.h"

namespace CurseForge
{
    struct ComparisonResult
    {
        QJsonArray filesToDownload;
        QStringList filesToDelete;
        QStringList filesToBackup;
    };

    class FileResolvingTask : public Task
    {
        Q_OBJECT
    public:
        explicit FileResolvingTask(shared_qobject_ptr<QNetworkAccessManager> network, CurseForge::Manifest &toProcess, const QString &path = QString(), const ModpackUpdateContext &updateContext = ModpackUpdateContext());
        virtual ~FileResolvingTask() {};

        const CurseForge::Manifest &getResults() const
        {
            return m_toProcess;
        }

        QString backupDir() const
        {
            return m_backupDir;
        }

        QString basePath() const
        {
            return m_basePath;
        }

        void performRollback();
        void performCleanup();

    protected:
        virtual void executeTask() override;

    private:
        CurseForge::ComparisonResult compareManifests(const QString &jsonFilePathA);
        bool backupFiles(const QStringList &filePaths, const QString &backupDir);
        void rollbackFiles(const QString &backupDir, const QString &targetBasePath);
        void cleanupBackup(const QString &backupDir);

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
        ModpackUpdateContext m_updateContext;
        QString m_backupDir;
        QString m_basePath;
    };
}

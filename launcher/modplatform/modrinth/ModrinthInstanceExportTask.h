/*
 * Copyright 2023-2024 arthomnix
 *
 * This source is subject to the Microsoft Public License (MS-PL).
 * Please see the COPYING.md file for more information.
 */

#pragma once

#include "tasks/Task.h"
#include "BaseInstance.h"
#include "net/NetJob.h"
#include "ui/dialogs/ModrinthExportDialog.h"
#include "ModrinthHashLookupRequest.h"
#include <QFuture>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <atomic>

namespace Modrinth
{

struct ExportSettings
{
    QString version;
    QString name;
    QString description;

    bool includeGameConfig;
    bool includeModConfigs;
    bool includeResourcePacks;
    bool includeShaderPacks;
    bool treatDisabledAsOptional;
    QString datapacksPath;

    QString gameVersion;
    QString forgeVersion;
    QString fabricVersion;
    QString quiltVersion;
    QString neoforgeVersion;

    QString exportPath;
};

struct ExportFile
{
    QString path;
    QString sha512;
    QString sha1;
    QString download;
    qint64 fileSize;
    bool optional = false;
};

class InstanceExportTask : public Task
{
Q_OBJECT

public:
    explicit InstanceExportTask(InstancePtr instance, ExportSettings settings);

protected:
    virtual void executeTask() override;
    virtual bool abort() override;

private slots:
    void lookupSucceeded();
    void lookupFailed(const QString &reason);
    void lookupProgress(qint64 current, qint64 total);
    void compressFinished();

private:
    InstancePtr m_instance;
    ExportSettings m_settings;
    std::shared_ptr<QList<HashLookupResponseData>> m_response;
    NetJob::Ptr m_netJob;
    QTemporaryDir m_tmpDir;
    QFuture<bool> m_compressFuture;
    QFutureWatcher<bool> m_compressFutureWatcher;
    std::atomic<bool> m_aborted;
};

}
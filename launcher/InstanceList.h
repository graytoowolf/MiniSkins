/* Copyright 2013-2021 MiniSkins Contributors
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

#include <QObject>
#include <QAbstractListModel>
#include <QSet>
#include <QList>

#include "BaseInstance.h"

#include "QObjectPtr.h"
#include "minecraft/MinecraftInstance.h"
#include "net/NetJob.h"
#include <QDateTime>

class QFileSystemWatcher;
class InstanceTask;
class PackProfile;
using InstanceId = QString;
using GroupId = QString;
using InstanceLocator = std::pair<InstancePtr, int>;
using MinecraftInstancePtr = std::shared_ptr<MinecraftInstance>;
using PackProfilePtr = std::shared_ptr<PackProfile>;

// MOD下载信息结构体
struct ModDownloadInfo
{
    int modId;
    int fileId;
    QString fileName;
    QString filePath;
};

enum class InstCreateError
{
    NoCreateError = 0,
    NoSuchVersion,
    UnknownCreateError,
    InstExists,
    CantCreateDir
};

enum class GroupsState
{
    NotLoaded,
    Steady,
    Dirty
};

class InstanceList : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit InstanceList(SettingsObjectPtr settings, const QString &instDir, QObject *parent = 0);
    virtual ~InstanceList();

public:
    QModelIndex index(int row, int column = 0, const QModelIndex &parent = QModelIndex()) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    enum AdditionalRoles
    {
        GroupRole = Qt::UserRole,
        InstancePointerRole = 0x34B1CB48, ///< Return pointer to real instance
        InstanceIDRole = 0x34B1CB49       ///< Return id if the instance
    };
    /*!
     * \brief Error codes returned by functions in the InstanceList class.
     * NoError Indicates that no error occurred.
     * UnknownError indicates that an unspecified error occurred.
     */
    enum InstListError
    {
        NoError = 0,
        UnknownError
    };

    InstancePtr at(int i) const
    {
        return m_instances.at(i);
    }

    int count() const
    {
        return m_instances.count();
    }

    InstListError loadList();
    void saveNow();

    InstancePtr getInstanceById(QString id) const;
    QModelIndex getInstanceIndexById(const QString &id) const;
    QStringList getGroups();
    bool isGroupCollapsed(const QString &groupName);

    GroupId getInstanceGroup(const InstanceId &id) const;
    void setInstanceGroup(const InstanceId &id, const GroupId &name);

    void deleteGroup(const GroupId &name);
    void deleteInstance(const InstanceId &id);

    // Wrap an instance creation task in some more task machinery and make it ready to be used
    Task *wrapInstanceTask(InstanceTask *task);

    /**
     * Create a new empty staging area for instance creation and @return a path/key top commit it later.
     * Used by instance manipulation tasks.
     */
    QString getStagedInstancePath();

    /**
     * Commit the staging area given by @keyPath to the provider - used when creation succeeds.
     * Used by instance manipulation tasks.
     */
    bool commitStagedInstance(const QString &keyPath, const QString &instanceName, const QString &groupName);

    /**
     * Destroy a previously created staging area given by @keyPath - used when creation fails.
     * Used by instance manipulation tasks.
     */
    bool destroyStagingPath(const QString &keyPath);

    int getTotalPlayTime();

    Qt::DropActions supportedDragActions() const override;

    Qt::DropActions supportedDropActions() const override;

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const override;

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;

    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;

signals:
    void dataIsInvalid();
    void instancesChanged();
    void instanceSelectRequest(QString instanceId);
    void groupsChanged(QSet<QString> groups);

public slots:
    void on_InstFolderChanged(const Setting &setting, QVariant value);
    void on_GroupStateChanged(const QString &group, bool collapsed);

private slots:
    void propertiesChanged(BaseInstance *inst);
    void providerUpdated();
    void instanceDirContentsChanged(const QString &path);

private:
    int getInstIndex(BaseInstance *inst) const;
    void updateTotalPlayTime();
    void suspendWatch();
    void resumeWatch();
    void add(const QList<InstancePtr> &list);
    void loadGroupList();
    void saveGroupList();
    QList<InstanceId> discoverInstances();
    InstancePtr loadInstance(const InstanceId &id);

private:
    void scanAndProcessBlacklistedModsAsync(InstancePtr instance);
    void scanAndProcessBlacklistedMods(InstancePtr instance);
    void processWhitelistedMods(InstancePtr instance, const QMap<int, QString> &whitelist, const QList<fingerprint::ModInfo> &modInfoList, QSet<int> &whitelistedModIds);

    // 白名单MOD下载相关方法
    QJsonArray parseMMCPackComponents(MinecraftInstancePtr instance);
    int getModLoaderTypeFromInstance(PackProfilePtr packProfile);
    QString getMinecraftVersionFromInstance(PackProfilePtr packProfile);
    void downloadWhitelistMods(MinecraftInstancePtr instance, const QList<int> &modIds, const QString &gameVersion, int modLoaderType);
    void processNextModInQueue();
    void fetchModFileInfo(int modId);
    void downloadModFile(int modId, int fileId, const QString &fileName, const QString &downloadUrl);

    // 辅助方法
    static const QMap<QString, int> &getModLoaderTypeMap();

private:
    int m_watchLevel = 0;
    int totalPlayTime = 0;
    bool m_dirty = false;
    QList<InstancePtr> m_instances;
    QSet<QString> m_groupNameCache;

    SettingsObjectPtr m_globalSettings;
    QString m_instDir;
    QFileSystemWatcher *m_watcher;
    // FIXME: this is so inefficient that looking at it is almost painful.
    QSet<QString> m_collapsedGroups;
    QMap<InstanceId, GroupId> m_instanceGroupIndex;
    QSet<InstanceId> instanceSet;
    bool m_groupsLoaded = false;
    bool m_instancesProbed = false;

    // 白名单MOD下载状态跟踪
    QList<int> m_downloadQueue;               // 待下载的MOD ID队列
    QSet<int> m_processedMods;                // 已处理的MOD ID集合（避免循环依赖）
    QList<ModDownloadInfo> m_downloadedFiles; // 已下载的文件信息
    QString m_currentGameVersion;             // 当前游戏版本
    int m_currentModLoaderType;               // 当前模组加载器类型
    MinecraftInstancePtr m_currentInstance;   // 当前处理的实例
};

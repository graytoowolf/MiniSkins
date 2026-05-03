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

#include <QDir>
#include <QDirIterator>
#include <QSet>
#include <QFile>
#include <QThread>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QTimer>
#include <QDebug>
#include <QFileSystemWatcher>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>

#include "Application.h"
#include "InstanceList.h"
#include "BaseInstance.h"
#include "InstanceTask.h"
#include "settings/INISettingsObject.h"
#include "minecraft/legacy/LegacyInstance.h"
#include "NullInstance.h"
#include "minecraft/MinecraftInstance.h"
#include "FileSystem.h"
#include "ExponentialSeries.h"
#include "WatchLock.h"
#include "minecraft/mod/fingerprint.h"
#include "net/Download.h"
#include "net/NetJob.h"
#include "minecraft/PackProfile.h"
#include "minecraft/LaunchProfile.h"
#include "minecraft/Component.h"

const static int GROUP_FILE_FORMAT_VERSION = 1;

InstanceList::InstanceList(SettingsObjectPtr settings, const QString &instDir, QObject *parent)
    : QAbstractListModel(parent), m_globalSettings(settings)
{
    resumeWatch();
    // Create and normalize path
    if (!QDir::current().exists(instDir))
    {
        QDir::current().mkpath(instDir);
    }

    connect(this, &InstanceList::instancesChanged, this, &InstanceList::providerUpdated);

    // NOTE: canonicalPath requires the path to exist. Do not move this above the creation block!
    m_instDir = QDir(instDir).canonicalPath();
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &InstanceList::instanceDirContentsChanged);
    m_watcher->addPath(m_instDir);
}

InstanceList::~InstanceList()
{
}

Qt::DropActions InstanceList::supportedDragActions() const
{
    return Qt::MoveAction;
}

Qt::DropActions InstanceList::supportedDropActions() const
{
    return Qt::MoveAction;
}

bool InstanceList::canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const
{
    if (data && data->hasFormat("application/x-instanceid"))
    {
        return true;
    }
    return false;
}

bool InstanceList::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    if (data && data->hasFormat("application/x-instanceid"))
    {
        return true;
    }
    return false;
}

QStringList InstanceList::mimeTypes() const
{
    auto types = QAbstractListModel::mimeTypes();
    types.push_back("application/x-instanceid");
    return types;
}

QMimeData *InstanceList::mimeData(const QModelIndexList &indexes) const
{
    auto mimeData = QAbstractListModel::mimeData(indexes);
    if (indexes.size() == 1)
    {
        auto instanceId = data(indexes[0], InstanceIDRole).toString();
        mimeData->setData("application/x-instanceid", instanceId.toUtf8());
    }
    return mimeData;
}

int InstanceList::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_instances.count();
}

QModelIndex InstanceList::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    if (row < 0 || row >= m_instances.size())
        return QModelIndex();
    return createIndex(row, column, (void *)m_instances.at(row).get());
}

QVariant InstanceList::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }
    BaseInstance *pdata = static_cast<BaseInstance *>(index.internalPointer());
    switch (role)
    {
    case InstancePointerRole:
    {
        QVariant v = qVariantFromValue((void *)pdata);
        return v;
    }
    case InstanceIDRole:
    {
        return pdata->id();
    }
    case Qt::EditRole:
    case Qt::DisplayRole:
    {
        return pdata->name();
    }
    case Qt::AccessibleTextRole:
    {
        return tr("%1 Instance").arg(pdata->name());
    }
    case Qt::ToolTipRole:
    {
        return pdata->instanceRoot();
    }
    case Qt::DecorationRole:
    {
        return pdata->iconKey();
    }
    // HACK: see InstanceView.h in gui!
    case GroupRole:
    {
        return getInstanceGroup(pdata->id());
    }
    default:
        break;
    }
    return QVariant();
}

bool InstanceList::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
    {
        return false;
    }
    if (role != Qt::EditRole)
    {
        return false;
    }
    BaseInstance *pdata = static_cast<BaseInstance *>(index.internalPointer());
    auto newName = value.toString();
    if (pdata->name() == newName)
    {
        return true;
    }
    pdata->setName(newName);
    return true;
}

Qt::ItemFlags InstanceList::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f;
    if (index.isValid())
    {
        f |= (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    }
    return f;
}

GroupId InstanceList::getInstanceGroup(const InstanceId &id) const
{
    auto inst = getInstanceById(id);
    if (!inst)
    {
        return GroupId();
    }
    auto iter = m_instanceGroupIndex.find(inst->id());
    if (iter != m_instanceGroupIndex.end())
    {
        return *iter;
    }
    return GroupId();
}

void InstanceList::setInstanceGroup(const InstanceId &id, const GroupId &name)
{
    auto inst = getInstanceById(id);
    if (!inst)
    {
        return;
    }

    bool changed = false;
    auto iter = m_instanceGroupIndex.find(inst->id());
    if (iter != m_instanceGroupIndex.end())
    {
        if (*iter != name)
        {
            *iter = name;
            changed = true;
        }
    }
    else
    {
        changed = true;
        m_instanceGroupIndex[id] = name;
    }

    if (changed)
    {
        m_groupNameCache.insert(name);
        auto idx = getInstIndex(inst.get());
        emit dataChanged(index(idx), index(idx), {GroupRole});
        saveGroupList();
    }
}

QStringList InstanceList::getGroups()
{
    return m_groupNameCache.toList();
}

void InstanceList::deleteGroup(const QString &name)
{
    bool removed = false;
    for (auto &instance : m_instances)
    {
        const auto &instID = instance->id();
        auto instGroupName = getInstanceGroup(instID);
        if (instGroupName == name)
        {
            m_instanceGroupIndex.remove(instID);
            removed = true;
            auto idx = getInstIndex(instance.get());
            if (idx > 0)
            {
                emit dataChanged(index(idx), index(idx), {GroupRole});
            }
        }
    }
    if (removed)
    {
        saveGroupList();
    }
}

bool InstanceList::isGroupCollapsed(const QString &group)
{
    return m_collapsedGroups.contains(group);
}

void InstanceList::deleteInstance(const InstanceId &id)
{
    auto inst = getInstanceById(id);
    if (!inst)
    {
        return;
    }

    if (m_instanceGroupIndex.remove(id))
    {
        saveGroupList();
    }

    if (!FS::deletePath(inst->instanceRoot()))
    {
        qWarning() << "Deletion of instance" << id << "has not been completely successful ...";
        return;
    }
}

static QMap<InstanceId, InstanceLocator> getIdMapping(const QList<InstancePtr> &list)
{
    QMap<InstanceId, InstanceLocator> out;
    int i = 0;
    for (auto &item : list)
    {
        auto id = item->id();
        if (out.contains(id))
        {
            qWarning() << "Duplicate ID" << id << "in instance list";
        }
        out[id] = std::make_pair(item, i);
        i++;
    }
    return out;
}

QList<InstanceId> InstanceList::discoverInstances()
{
    QList<InstanceId> out;
    QDirIterator iter(m_instDir, QDir::Dirs | QDir::NoDot | QDir::NoDotDot | QDir::Readable | QDir::Hidden, QDirIterator::FollowSymlinks);
    while (iter.hasNext())
    {
        QString subDir = iter.next();
        QFileInfo dirInfo(subDir);
        if (!QFileInfo(FS::PathCombine(subDir, "instance.cfg")).exists())
            continue;
        // if it is a symlink, ignore it if it goes to the instance folder
        if (dirInfo.isSymLink())
        {
            QFileInfo targetInfo(dirInfo.symLinkTarget());
            QFileInfo instDirInfo(m_instDir);
            if (targetInfo.canonicalPath() == instDirInfo.canonicalFilePath())
            {
                continue;
            }
        }
        auto id = dirInfo.fileName();
        out.append(id);
    }
    instanceSet = out.toSet();
    m_instancesProbed = true;
    return out;
}

InstanceList::InstListError InstanceList::loadList()
{
    auto existingIds = getIdMapping(m_instances);

    QList<InstancePtr> newList;

    for (auto &id : discoverInstances())
    {
        bool isadded = false;
        if (existingIds.contains(id))
        {
            if (APPLICATION->isUpdating() && id == APPLICATION->getUpdateTargetInstanceId())
            {
                isadded = true;
            }
            else
            {
                auto instPair = existingIds[id];
                existingIds.remove(id);
                // Keep existing instance
            }
        }
        else
        {
            isadded = true;
        }
        if (isadded)
        {
            InstancePtr instPtr = loadInstance(id);
            if (instPtr)
            {
                newList.append(instPtr);
            }
        }
    }

    // TODO: looks like a general algorithm with a few specifics inserted. Do something about it.
    if (!existingIds.isEmpty())
    {
        // get the list of removed instances and sort it by their original index, from last to first
        auto deadList = existingIds.values();
        auto orderSortPredicate = [](const InstanceLocator &a, const InstanceLocator &b) -> bool
        {
            return a.second > b.second;
        };
        std::sort(deadList.begin(), deadList.end(), orderSortPredicate);
        // remove the contiguous ranges of rows
        int front_bookmark = -1;
        int back_bookmark = -1;
        int currentItem = -1;
        auto removeNow = [&]()
        {
            beginRemoveRows(QModelIndex(), front_bookmark, back_bookmark);
            m_instances.erase(m_instances.begin() + front_bookmark, m_instances.begin() + back_bookmark + 1);
            endRemoveRows();
            front_bookmark = -1;
            back_bookmark = currentItem;
        };
        for (auto &removedItem : deadList)
        {
            auto instPtr = removedItem.first;
            instPtr->invalidate();
            currentItem = removedItem.second;
            if (back_bookmark == -1)
            {
                // no bookmark yet
                back_bookmark = currentItem;
            }
            else if (currentItem == front_bookmark - 1)
            {
                // part of contiguous sequence, continue
            }
            else
            {
                // seam between previous and current item
                removeNow();
            }
            front_bookmark = currentItem;
        }
        if (back_bookmark != -1)
        {
            removeNow();
        }
    }
    if (newList.size())
    {
        add(newList);
    }
    m_dirty = false;
    updateTotalPlayTime();
    return NoError;
}

void InstanceList::updateTotalPlayTime()
{
    totalPlayTime = 0;
    for (auto const &itr : m_instances)
    {
        totalPlayTime += itr.get()->totalTimePlayed();
    }
}

void InstanceList::saveNow()
{
    for (auto &item : m_instances)
    {
        item->saveNow();
    }
}

void InstanceList::add(const QList<InstancePtr> &t)
{
    beginInsertRows(QModelIndex(), m_instances.count(), m_instances.count() + t.size() - 1);
    m_instances.append(t);
    for (auto &ptr : t)
    {
        connect(ptr.get(), &BaseInstance::propertiesChanged, this, &InstanceList::propertiesChanged);
    }
    endInsertRows();
}

void InstanceList::resumeWatch()
{
    if (m_watchLevel > 0)
    {
        qWarning() << "Bad suspend level resume in instance list";
        return;
    }
    m_watchLevel++;
    if (m_watchLevel > 0 && m_dirty)
    {
        loadList();
    }
}

void InstanceList::suspendWatch()
{
    m_watchLevel--;
}

void InstanceList::providerUpdated()
{
    m_dirty = true;
    if (m_watchLevel == 1)
    {
        loadList();
    }
}

InstancePtr InstanceList::getInstanceById(QString instId) const
{
    if (instId.isEmpty())
        return InstancePtr();
    for (auto &inst : m_instances)
    {
        if (inst->id() == instId)
        {
            return inst;
        }
    }
    return InstancePtr();
}

QModelIndex InstanceList::getInstanceIndexById(const QString &id) const
{
    return index(getInstIndex(getInstanceById(id).get()));
}

int InstanceList::getInstIndex(BaseInstance *inst) const
{
    int count = m_instances.count();
    for (int i = 0; i < count; i++)
    {
        if (inst == m_instances[i].get())
        {
            return i;
        }
    }
    return -1;
}

void InstanceList::propertiesChanged(BaseInstance *inst)
{
    int i = getInstIndex(inst);
    if (i != -1)
    {
        emit dataChanged(index(i), index(i));
        updateTotalPlayTime();
    }
}

InstancePtr InstanceList::loadInstance(const InstanceId &id)
{
    if (!m_groupsLoaded)
    {
        loadGroupList();
    }

    auto instanceRoot = FS::PathCombine(m_instDir, id);
    auto instanceSettings = std::make_shared<INISettingsObject>(FS::PathCombine(instanceRoot, "instance.cfg"));
    InstancePtr inst;

    instanceSettings->registerSetting("InstanceType", "Legacy");

    QString inst_type = instanceSettings->get("InstanceType").toString();

    if (inst_type == "OneSix" || inst_type == "Nostalgia")
    {
        inst.reset(new MinecraftInstance(m_globalSettings, instanceSettings, instanceRoot));
    }
    else if (inst_type == "Legacy")
    {
        inst.reset(new LegacyInstance(m_globalSettings, instanceSettings, instanceRoot));
    }
    else
    {
        inst.reset(new NullInstance(m_globalSettings, instanceSettings, instanceRoot));
    }
    // Instance loaded successfully
    return inst;
}

void InstanceList::saveGroupList()
{
    if (!m_instancesProbed)
    {
        return;
    }
    WatchLock foo(m_watcher, m_instDir);
    QString groupFileName = m_instDir + "/instgroups.json";
    QMap<QString, QSet<QString>> reverseGroupMap;
    for (auto iter = m_instanceGroupIndex.begin(); iter != m_instanceGroupIndex.end(); iter++)
    {
        QString id = iter.key();
        QString group = iter.value();
        if (group.isEmpty())
            continue;
        if (!instanceSet.contains(id))
        {
            continue;
        }

        if (!reverseGroupMap.count(group))
        {
            QSet<QString> set;
            set.insert(id);
            reverseGroupMap[group] = set;
        }
        else
        {
            QSet<QString> &set = reverseGroupMap[group];
            set.insert(id);
        }
    }
    QJsonObject toplevel;
    toplevel.insert("formatVersion", QJsonValue(QString("1")));
    QJsonObject groupsArr;
    for (auto iter = reverseGroupMap.begin(); iter != reverseGroupMap.end(); iter++)
    {
        auto list = iter.value();
        auto name = iter.key();
        QJsonObject groupObj;
        QJsonArray instanceArr;
        groupObj.insert("hidden", QJsonValue(m_collapsedGroups.contains(name)));
        for (auto item : list)
        {
            instanceArr.append(QJsonValue(item));
        }
        groupObj.insert("instances", instanceArr);
        groupsArr.insert(name, groupObj);
    }
    toplevel.insert("groups", groupsArr);
    QJsonDocument doc(toplevel);
    try
    {
        FS::write(groupFileName, doc.toJson());
    }
    catch (const FS::FileSystemException &e)
    {
        qCritical() << "Failed to write instance group file :" << e.cause();
    }
}

void InstanceList::loadGroupList()
{
    QString groupFileName = m_instDir + "/instgroups.json";

    // if there's no group file, fail
    if (!QFileInfo(groupFileName).exists())
        return;

    QByteArray jsonData;
    try
    {
        jsonData = FS::read(groupFileName);
    }
    catch (const FS::FileSystemException &e)
    {
        qCritical() << "Failed to read instance group file :" << e.cause();
        return;
    }

    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &error);

    // if the json was bad, fail
    if (error.error != QJsonParseError::NoError)
    {
        qCritical() << QString("Failed to parse instance group file: %1 at offset %2")
                           .arg(error.errorString(), QString::number(error.offset))
                           .toUtf8();
        return;
    }

    // if the root of the json wasn't an object, fail
    if (!jsonDoc.isObject())
    {
        qWarning() << "Invalid group file. Root entry should be an object.";
        return;
    }

    QJsonObject rootObj = jsonDoc.object();

    // Make sure the format version matches, otherwise fail.
    if (rootObj.value("formatVersion").toVariant().toInt() != GROUP_FILE_FORMAT_VERSION)
        return;

    // Get the groups. if it's not an object, fail
    if (!rootObj.value("groups").isObject())
    {
        qWarning() << "Invalid group list JSON: 'groups' should be an object.";
        return;
    }

    QSet<QString> groupSet;
    m_instanceGroupIndex.clear();

    // Iterate through all the groups.
    QJsonObject groupMapping = rootObj.value("groups").toObject();
    for (QJsonObject::iterator iter = groupMapping.begin(); iter != groupMapping.end(); iter++)
    {
        QString groupName = iter.key();

        // If not an object, complain and skip to the next one.
        if (!iter.value().isObject())
        {
            qWarning() << QString("Group '%1' in the group list should be an object.").arg(groupName).toUtf8();
            continue;
        }

        QJsonObject groupObj = iter.value().toObject();
        if (!groupObj.value("instances").isArray())
        {
            qWarning() << QString("Group '%1' in the group list is invalid. It should contain an array called 'instances'.").arg(groupName).toUtf8();
            continue;
        }

        // keep a list/set of groups for choosing
        groupSet.insert(groupName);

        auto hidden = groupObj.value("hidden").toBool(false);
        if (hidden)
        {
            m_collapsedGroups.insert(groupName);
        }

        // Iterate through the list of instances in the group.
        QJsonArray instancesArray = groupObj.value("instances").toArray();

        for (QJsonArray::iterator iter2 = instancesArray.begin(); iter2 != instancesArray.end(); iter2++)
        {
            m_instanceGroupIndex[(*iter2).toString()] = groupName;
        }
    }
    m_groupsLoaded = true;
    m_groupNameCache.unite(groupSet);
}

void InstanceList::instanceDirContentsChanged(const QString &path)
{
    Q_UNUSED(path);
    emit instancesChanged();
}

void InstanceList::on_InstFolderChanged(const Setting &setting, QVariant value)
{
    QString newInstDir = QDir(value.toString()).canonicalPath();
    if (newInstDir != m_instDir)
    {
        if (m_groupsLoaded)
        {
            saveGroupList();
        }
        m_instDir = newInstDir;
        m_groupsLoaded = false;
        emit instancesChanged();
    }
}

void InstanceList::on_GroupStateChanged(const QString &group, bool collapsed)
{
    if (collapsed)
    {
        m_collapsedGroups.insert(group);
    }
    else
    {
        m_collapsedGroups.remove(group);
    }
    saveGroupList();
}

class InstanceStaging : public Task
{
    Q_OBJECT
    const unsigned minBackoff = 1;
    const unsigned maxBackoff = 16;

public:
    InstanceStaging(
        InstanceList *parent,
        Task *child,
        const QString &stagingPath,
        const QString &instanceName,
        const QString &groupName)
        : backoff(minBackoff, maxBackoff)
    {
        m_parent = parent;
        m_child.reset(child);
        connect(child, &Task::succeeded, this, &InstanceStaging::childSucceded);
        connect(child, &Task::failed, this, &InstanceStaging::childFailed);
        connect(child, &Task::status, this, &InstanceStaging::setStatus);
        connect(child, &Task::progress, this, &InstanceStaging::setProgress);
        m_instanceName = instanceName;
        m_groupName = groupName;
        m_stagingPath = stagingPath;
        m_backoffTimer.setSingleShot(true);
        connect(&m_backoffTimer, &QTimer::timeout, this, &InstanceStaging::childSucceded);
    }

    virtual ~InstanceStaging() {};

    // FIXME/TODO: add ability to abort during instance commit retries
    bool abort() override
    {
        if (m_child && m_child->canAbort())
        {
            return m_child->abort();
        }
        return false;
    }
    bool canAbort() const override
    {
        if (m_child && m_child->canAbort())
        {
            return true;
        }
        return false;
    }

protected:
    virtual void executeTask() override
    {
        m_child->start();
    }
    QStringList warnings() const override
    {
        return m_child->warnings();
    }

private slots:
    void childSucceded()
    {
        unsigned sleepTime = backoff();
        if (m_parent->commitStagedInstance(m_stagingPath, m_instanceName, m_groupName))
        {
            emitSucceeded();
            return;
        }
        // we actually failed, retry?
        if (sleepTime == maxBackoff)
        {
            emitFailed(tr("Failed to commit instance, even after multiple retries. It is being blocked by something."));
            return;
        }
        // Failed to commit instance, retrying with backoff
        m_backoffTimer.start(sleepTime * 500);
    }
    void childFailed(const QString &reason)
    {
        m_parent->destroyStagingPath(m_stagingPath);
        emitFailed(reason);
    }

private:
    /*
     * WHY: the whole reason why this uses an exponential backoff retry scheme is antivirus on Windows.
     * Basically, it starts messing things up while the launcher is extracting/creating instances
     * and causes that horrible failure that is NTFS to lock files in place because they are open.
     */
    ExponentialSeries backoff;
    QString m_stagingPath;
    InstanceList *m_parent;
    unique_qobject_ptr<Task> m_child;
    QString m_instanceName;
    QString m_groupName;
    QTimer m_backoffTimer;
};

Task *InstanceList::wrapInstanceTask(InstanceTask *task)
{
    auto stagingPath = getStagedInstancePath();
    task->setStagingPath(stagingPath);
    task->setParentSettings(m_globalSettings);
    return new InstanceStaging(this, task, stagingPath, task->name(), task->group());
}

QString InstanceList::getStagedInstancePath()
{
    QString key = QUuid::createUuid().toString();
    QString relPath = FS::PathCombine("_LAUNCHER_TEMP/", key);
    QDir rootPath(m_instDir);
    auto path = FS::PathCombine(m_instDir, relPath);
    if (!rootPath.mkpath(relPath))
    {
        return QString();
    }
    return path;
}

bool InstanceList::commitStagedInstance(const QString &path, const QString &instanceName, const QString &groupName)
{
    QDir dir;
    QString instID = FS::DirNameFromString(instanceName, m_instDir);
    {
        if (APPLICATION->isUpdating())
        {
            instID = APPLICATION->getUpdateTargetInstanceId();
            // 复制文件的通用函数
            auto copyFileWithReplace = [](const QString &source, const QString &target) -> bool
            {
                if (QFile::exists(target))
                {
                    QFile::remove(target);
                }
                return QFile::copy(source, target);
            };

            // 处理minecraft目录下的子目录
            QString sourceDirPath = FS::PathCombine(path, "minecraft");
            QString targetDirPath = FS::PathCombine(m_instDir, instID, "minecraft");
            QDir sourceDir(sourceDirPath);
            sourceDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

            for (const QString &subdirectory : sourceDir.entryList())
            {
                QString sourceSubdirPath = FS::PathCombine(sourceDirPath, subdirectory);
                QString targetSubdirPath = FS::PathCombine(targetDirPath, subdirectory);

                if (subdirectory == "mods")
                {
                    // 特殊处理mods目录，复制文件而不是移动
                    QDir().mkpath(targetSubdirPath);
                    QDir modsDir(sourceSubdirPath);
                    modsDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
                    for (const QString &modFile : modsDir.entryList())
                    {
                        QString sourceModPath = FS::PathCombine(sourceSubdirPath, modFile);
                        QString targetModPath = FS::PathCombine(targetSubdirPath, modFile);
                        copyFileWithReplace(sourceModPath, targetModPath);
                    }
                }
                else
                {
                    // 其他目录直接替换
                    QDir targetSubdir(targetSubdirPath);
                    if (targetSubdir.exists())
                    {
                        targetSubdir.removeRecursively();
                    }
                    sourceDir.rename(sourceSubdirPath, targetSubdirPath);
                }
            }

            // 处理根目录下的文件
            QDir sourceFilesDir(path);
            sourceFilesDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
            QString targetInstancePath = FS::PathCombine(m_instDir, instID);

            for (const QString &file : sourceFilesDir.entryList())
            {
                QString sourceFilePath = FS::PathCombine(path, file);
                QString targetFilePath = FS::PathCombine(targetInstancePath, file);

                copyFileWithReplace(sourceFilePath, targetFilePath);
            }

            // 清理源目录
            FS::deletePath(path);

            // 清理备份目录
            QString backupDir = FS::PathCombine(m_instDir, instID, ".update_backup");
            if (QDir(backupDir).exists())
            {
                FS::deletePath(backupDir);
            }
        }
        else
        {
            WatchLock lock(m_watcher, m_instDir);
            QString destination = FS::PathCombine(m_instDir, instID);
            if (!dir.rename(path, destination))
            {
                qWarning() << "Failed to move" << path << "to" << destination;
                return false;
            }
            m_instanceGroupIndex[instID] = groupName;
            instanceSet.insert(instID);
            m_groupNameCache.insert(groupName);
        }

        emit instancesChanged();
        emit instanceSelectRequest(instID);

        QString minecraftDir = getInstanceById(instID)->gameRoot();

        // 检查目录是否存在，不存在则创建
        if (!minecraftDir.isEmpty())
        {
            QDir mcDir(minecraftDir);
            if (!mcDir.exists())
            {
                QDir().mkpath(minecraftDir);
            }
        }

        // 获取系统语言
        QLocale locale = QLocale::system();
        QString langCode = locale.name();

        QString optionsFilePath = FS::PathCombine(minecraftDir, "options.txt");
        if (!QFileInfo::exists(optionsFilePath))
        {
            QFile optionsFile(optionsFilePath);
            if (optionsFile.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                QTextStream out(&optionsFile);
                out.setCodec("UTF-8");
                out << "lang:" << langCode << "\n";
                optionsFile.close();
            }
        }
    }
    // 处理黑白名单功能
    auto instance = getInstanceById(instID);
    if (instance)
    {
        auto minecraftInstance = std::dynamic_pointer_cast<MinecraftInstance>(instance);
        if (minecraftInstance)
        {
            auto packProfile = minecraftInstance->getPackProfile();
            if (packProfile)
            {
                // 检查是否有模组加载器
                int modLoaderTypeInt = getModLoaderTypeFromInstance(packProfile);
                bool hasModLoader = (modLoaderTypeInt != 0);
                // 如果有模组加载器，则处理黑白名单
                if (hasModLoader)
                {
                    scanAndProcessBlacklistedMods(instance);
                }
            }
        }
    }

    APPLICATION->setUpdateTargetInstanceId("");
    APPLICATION->setUpdating(false);
    saveGroupList();
    return true;
}

void InstanceList::scanAndProcessBlacklistedMods(InstancePtr instance)
{
    auto minecraftInstance = std::dynamic_pointer_cast<MinecraftInstance>(instance);
    if (!minecraftInstance)
    {
        return;
    }

    QString modsPath = minecraftInstance->modsRoot();
    QDir modsDir(modsPath);
    if (!modsDir.exists())
    {
        return;
    }

    // 获取所有.jar文件并创建ModInfo列表
    QFileInfoList jarFiles = modsDir.entryInfoList({"*.jar"}, QDir::Files);
    if (jarFiles.isEmpty())
    {
        return;
    }

    QList<fingerprint::ModInfo> modInfoList;
    for (const QFileInfo &fileInfo : jarFiles)
    {
        modInfoList.append(fingerprint::ModInfo(fileInfo.absoluteFilePath()));
    }

    // 计算指纹
    modInfoList = fingerprint::processModInfoList(modInfoList);
    if (modInfoList.isEmpty())
    {
        return;
    }

    // 获取黑白名单
    QMap<int, QString> blacklist = APPLICATION->getModBlacklist();
    QMap<int, QString> whitelist = APPLICATION->getModWhitelist();

    QSet<int> whitelistedModIds;
    QStringList modsToDisable;
    QJsonArray modsJsonArray;

    // 处理白名单MOD
    if (!whitelist.isEmpty())
    {
        processWhitelistedMods(instance, whitelist, modInfoList, whitelistedModIds);
    }

    // 处理每个MOD
    for (const auto &modInfo : modInfoList)
    {
        if (modInfo.projectId <= 0)
            continue;

        QJsonObject modObj;
        modObj["projectID"] = modInfo.projectId;
        modObj["fileID"] = modInfo.fileId;
        modObj["name"] = modInfo.name;
        modObj["fileName"] = QFileInfo(modInfo.filePath).fileName();
        modObj["required"] = true;

        bool isBlacklisted = blacklist.contains(modInfo.projectId);
        if (isBlacklisted && !whitelistedModIds.contains(modInfo.projectId))
        {
            modsToDisable.append(modInfo.filePath);
            modObj["required"] = false;
            modObj["fileName"] = modObj["fileName"].toString() + ".disabled";
        }

        modsJsonArray.append(modObj);
    }

    // 禁用被标记的mod文件
    for (const QString &filePath : modsToDisable)
    {
        QFile file(filePath);
        if (file.exists())
        {
            file.rename(filePath + ".disabled");
        }
    }

    // 保存mods.json文件
    QString modsJsonPath = minecraftInstance->modlist();
    QFile modsJsonFile(modsJsonPath);
    if (modsJsonFile.open(QIODevice::WriteOnly))
    {
        modsJsonFile.write(QJsonDocument(modsJsonArray).toJson());
    }
}

void InstanceList::processWhitelistedMods(InstancePtr instance, const QMap<int, QString> &whitelist, const QList<fingerprint::ModInfo> &modInfoList, QSet<int> &whitelistedModIds)
{
    if (whitelist.isEmpty())
    {
        return;
    }

    auto minecraftInstance = std::dynamic_pointer_cast<MinecraftInstance>(instance);
    if (!minecraftInstance)
    {
        return;
    }

    // 收集白名单中的MOD ID
    for (auto it = whitelist.begin(); it != whitelist.end(); ++it)
    {
        whitelistedModIds.insert(it.key());
    }

    // 创建已有mod的ID集合
    QSet<int> existingModIds;
    for (const auto &modInfo : modInfoList)
    {
        if (modInfo.projectId > 0)
        {
            existingModIds.insert(modInfo.projectId);
            // 如果是白名单MOD，添加到保护列表
            if (whitelist.contains(modInfo.projectId))
            {
                whitelistedModIds.insert(modInfo.projectId);
            }
        }
    }

    // 检查白名单中缺失的MOD并开始下载流程
    QStringList missingWhitelistMods;
    QList<int> missingModIds;

    for (auto it = whitelist.constBegin(); it != whitelist.constEnd(); ++it)
    {
        int modId = it.key();
        QString modName = it.value();

        if (!existingModIds.contains(modId))
        {
            missingWhitelistMods.append(QString("%1 (ID: %2)").arg(modName).arg(modId));
            missingModIds.append(modId);
        }
    }

    // 如果有缺失的白名单MOD，开始下载流程
    if (!missingModIds.isEmpty())
    {
        // 获取游戏版本和模组加载器信息
        auto packProfile = minecraftInstance->getPackProfile();
        QString gameVersion = getMinecraftVersionFromInstance(packProfile);
        int modLoaderType = getModLoaderTypeFromInstance(packProfile);

        // 开始递归下载白名单MOD及其依赖
        downloadWhitelistMods(minecraftInstance, missingModIds, gameVersion, modLoaderType);
    }
}

// 通用的mmc-pack.json解析函数
QJsonArray InstanceList::parseMMCPackComponents(MinecraftInstancePtr instance)
{
    QString mmcPackPath = FS::PathCombine(instance->instanceRoot(), "mmc-pack.json");

    if (!QFile::exists(mmcPackPath))
    {
        return QJsonArray();
    }

    QFile mmcPackFile(mmcPackPath);
    if (!mmcPackFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return QJsonArray();
    }

    QTextStream in(&mmcPackFile);
    QString content = in.readAll();
    mmcPackFile.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        return QJsonArray();
    }

    return doc.object()["components"].toArray();
}

int InstanceList::getModLoaderTypeFromInstance(PackProfilePtr packProfile)
{
    if (!packProfile) {
        return 0; // Any/Unknown
    }

    // 确保 PackProfile 已加载组件数据
    if (packProfile->rowCount() == 0) {
        if (!packProfile->reload(Net::Mode::Offline)) {
            return 0;
        }
    }

    // 使用 PackProfile API 检查模组加载器
    const auto& loaderMap = getModLoaderTypeMap();
    for (auto it = loaderMap.begin(); it != loaderMap.end(); ++it) {
        auto component = packProfile->getComponent(it.key());
        if (component) {
            return it.value();
        }
    }

    return 0; // Any/Unknown
}

const QMap<QString, int> &InstanceList::getModLoaderTypeMap()
{
    static const QMap<QString, int> loaderMap = {
        {"net.minecraftforge", 1},         // Forge
        {"net.fabricmc.fabric-loader", 4}, // Fabric
        {"org.quiltmc.quilt-loader", 5},   // Quilt
        {"net.neoforged", 6}               // NeoForge
    };
    return loaderMap;
}

QString InstanceList::getMinecraftVersionFromInstance(PackProfilePtr packProfile)
{
    if (!packProfile) {
        return QString();
    }

    // 确保 PackProfile 已加载组件数据
    if (packProfile->rowCount() == 0) {
        if (!packProfile->reload(Net::Mode::Offline)) {
            return QString();
        }
    }

    // 使用 PackProfile API 获取 Minecraft 版本
    auto minecraftComponent = packProfile->getComponent("net.minecraft");
    if (minecraftComponent) {
        QString version = minecraftComponent->getVersion();
        return version;
    }

    return QString();
}

void InstanceList::downloadWhitelistMods(MinecraftInstancePtr instance, const QList<int> &modIds, const QString &gameVersion, int modLoaderType)
{
    if (modIds.isEmpty())
    {
        return;
    }

    // 创建下载状态跟踪
    m_downloadQueue = modIds;
    m_processedMods.clear();
    m_downloadedFiles.clear();
    m_currentGameVersion = gameVersion;
    m_currentModLoaderType = modLoaderType;
    m_currentInstance = instance;

    // 开始处理第一个MOD
    processNextModInQueue();
}

void InstanceList::processNextModInQueue()
{
    if (m_downloadQueue.isEmpty())
    {
        return;
    }

    int modId = m_downloadQueue.takeFirst();

    // 检查是否已经处理过这个MOD（避免循环依赖）
    if (m_processedMods.contains(modId))
    {
        processNextModInQueue();
        return;
    }

    m_processedMods.insert(modId);

    // 获取MOD文件信息
    fetchModFileInfo(modId);
}

void InstanceList::fetchModFileInfo(int modId)
{
    // 构建API URL
    QString apiUrl = QString("https://api.curseforge.com/v1/mods/%1/files?modLoaderType=%2&gameVersion=%3&pageSize=1")
                         .arg(modId)
                         .arg(m_currentModLoaderType)
                         .arg(m_currentGameVersion);

    // 创建网络请求
    NetJob *job = new NetJob(QString("ModFileInfo-%1").arg(modId), APPLICATION->network());
    QByteArray *responseData = new QByteArray();

    auto download = Net::Download::makeByteArray(QUrl(apiUrl), responseData);
    download->setExtraHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
    job->addNetAction(download);

    // 处理响应
    connect(job, &NetJob::succeeded, this, [this, job, responseData, modId]()
            {
        job->deleteLater();

        if (!responseData || responseData->isEmpty()) {
            delete responseData;
            processNextModInQueue();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(*responseData);
        QJsonObject rootObj = doc.object();
        QJsonArray filesArray = rootObj.value("data").toArray();

        if (filesArray.isEmpty()) {
            delete responseData;
            processNextModInQueue();
            return;
        }

        // 获取第一个（最新的）文件
        QJsonObject fileObj = filesArray.first().toObject();
        int fileId = fileObj["id"].toInt();
        QString fileName = fileObj["fileName"].toString();
        QString downloadUrl = fileObj["downloadUrl"].toString();


        // 获取MOD名称并更新白名单中的名称（如果是临时名称）
        if (APPLICATION->getModNameFromWhitelist(modId) == "Provisional Name") {
            QString modName;

            // 优先使用displayName字段
            if (fileObj.contains("displayName")) {
                QString displayName = fileObj["displayName"].toString();
                // 提取名称部分（去除加载器名称和版本号）
                QRegExp rx("([\\w\\s\\-]+?)(?:-(?:NeoForge|Forge|Fabric|Quilt)-[\\d\\.]+|$)");
                if (rx.indexIn(displayName) != -1) {
                    modName = rx.cap(1).trimmed();
                } else {
                    modName = displayName; // 如果无法提取，使用完整名称
                }
            }

            // 如果成功获取到名称，更新白名单
            if (!modName.isEmpty()) {
                APPLICATION->updateModWhitelistName(modId, modName);
            }
        }

        // 处理依赖关系
        QJsonArray dependencies = fileObj["dependencies"].toArray();
        for (const QJsonValue &depValue : dependencies) {
            QJsonObject depObj = depValue.toObject();
            int depModId = depObj["modId"].toInt();
            int relationType = depObj["relationType"].toInt();

            // relationType: 1=EmbeddedLibrary, 2=OptionalDependency, 3=RequiredDependency, 4=Tool, 5=Incompatible, 6=Include
            if (relationType == 3) { // RequiredDependency
                if (!m_processedMods.contains(depModId) && !m_downloadQueue.contains(depModId)) {
                    m_downloadQueue.prepend(depModId); // 优先处理依赖
                }
            }
        }

        // 下载文件
        if (!downloadUrl.isEmpty()) {
            downloadModFile(modId, fileId, fileName, downloadUrl);
        } else {
            processNextModInQueue();
        }

        delete responseData; });

    connect(job, &NetJob::failed, this, [this, job, responseData](QString reason)
            {
        job->deleteLater();
        delete responseData;
        processNextModInQueue(); });

    job->start();
}

void InstanceList::downloadModFile(int modId, int fileId, const QString &fileName, const QString &downloadUrl)
{
    QString modsPath = m_currentInstance->modsRoot();
    QString filePath = FS::PathCombine(modsPath, fileName);

    // 检查文件是否已存在
    if (QFile::exists(filePath))
    {
        processNextModInQueue();
        return;
    }

    // 创建下载任务
    NetJob *job = new NetJob(QString("ModDownload-%1").arg(modId), APPLICATION->network());

    auto download = Net::Download::makeFile(QUrl(downloadUrl), filePath);
    job->addNetAction(download);

    // 处理下载完成
    connect(job, &NetJob::succeeded, this, [this, job, modId, fileId, fileName, filePath]()
            {
        // 记录下载的文件信息
        ModDownloadInfo info;
        info.modId = modId;
        info.fileId = fileId;
        info.fileName = fileName;
        info.filePath = filePath;
        m_downloadedFiles.append(info);

        job->deleteLater();
        processNextModInQueue(); });

    connect(job, &NetJob::failed, this, [this, job](QString reason)
            {
        job->deleteLater();
        processNextModInQueue(); });

    job->start();
}

bool InstanceList::destroyStagingPath(const QString &keyPath)
{
    return FS::deletePath(keyPath);
}

int InstanceList::getTotalPlayTime()
{
    updateTotalPlayTime();
    return totalPlayTime;
}

#include "InstanceList.moc"

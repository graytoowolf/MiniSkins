/* Copyright 2013-2024 MiniSkins Contributors
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

#include "InstanceImportTask.h"
#include "BaseInstance.h"
#include "FileSystem.h"
#include "Application.h"
#include "MMCZip.h"
#include "NullInstance.h"
#include "settings/INISettingsObject.h"
#include "icons/IconUtils.h"
#include <QtConcurrentRun>

// FIXME: this does not belong here, it's Minecraft/CurseForge specific
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "modplatform/curseforge/FileResolvingTask.h"
#include "modplatform/curseforge/PackManifest.h"
#include "Json.h"
#include <quazipdir.h>
#include "modplatform/modrinth/ModrinthPackManifest.h"
#include "modplatform/technic/TechnicPackProcessor.h"

#include "icons/IconList.h"
#include "Application.h"
#include "net/ChecksumValidator.h"

#include <algorithm>
#include <iterator>
#include <QIcon>
#include <QMetaObject>
#include <QPointer>

namespace
{
    QString iconKeyForInstanceName(const QString &instanceName, const QString &stagingPath);
    QString importPackIcon(const QString &folder, const QString &instanceIconKey);
}

InstanceImportTask::InstanceImportTask(const QUrl sourceUrl, const ModpackUpdateContext &updateContext)
{
    m_sourceUrl = sourceUrl;
    m_updateContext = updateContext;
    m_addonId = updateContext.addonId;
    m_fileId = updateContext.fileId;
}

void InstanceImportTask::executeTask()
{
    if (m_sourceUrl.isLocalFile())
    {
        m_archivePath = m_sourceUrl.toLocalFile();
        processZipPack();
    }
    else
    {
        setStatus(tr("Downloading modpack:\n%1").arg(m_sourceUrl.toString()));
        m_downloadRequired = true;

        const QString path = m_sourceUrl.host() + '/' + m_sourceUrl.path();
        auto entry = APPLICATION->metacache()->resolveEntry("general", path);
        entry->setStale(true);
        m_filesNetJob = new NetJob(tr("Modpack download"), APPLICATION->network());
        m_filesNetJob->addNetAction(Net::Download::makeCached(m_sourceUrl, entry));
        m_archivePath = entry->getFullPath();
        auto job = m_filesNetJob.get();
        connect(job, &NetJob::succeeded, this, &InstanceImportTask::downloadSucceeded);
        connect(job, &NetJob::progress, this, &InstanceImportTask::downloadProgressChanged);
        connect(job, &NetJob::failed, this, &InstanceImportTask::downloadFailed);
        m_filesNetJob->start();
    }
}

bool InstanceImportTask::abort()
{
    if (m_updateContext.isValid() && m_modIdResolver)
    {
        m_modIdResolver->performRollback();
    }
    if (m_filesNetJob)
    {
        m_filesNetJob->abort();
    }
    if (m_modIdResolver)
    {
        m_modIdResolver->abort();
    }
    if (m_extractFuture.isRunning())
    {
        m_extractFuture.cancel();
    }
    emitFailed(tr("Instance import has been aborted."));
    return true;
}

void InstanceImportTask::downloadSucceeded()
{
    processZipPack();
    m_filesNetJob.reset();
}

void InstanceImportTask::downloadFailed(QString reason)
{
    emitFailed(reason);
    m_filesNetJob.reset();
}

void InstanceImportTask::downloadProgressChanged(qint64 current, qint64 total)
{
    setProgress(current / 2, total);
}

void InstanceImportTask::processZipPack()
{
    setStatus(tr("Extracting modpack"));
    QDir extractDir(m_stagingPath);
    qDebug() << "Attempting to create instance from" << m_archivePath;

    // open the zip and find relevant files in it
    m_packZip.reset(new QuaZip(m_archivePath));
    if (!m_packZip->open(QuaZip::mdUnzip))
    {
        emitFailed(tr("Unable to open supplied modpack zip file."));
        return;
    }

    QString root;
    QString fileName;
    QStringList filesToSearch = {"instance.cfg", "manifest.json", "modrinth.index.json"};
    QString rootDirectory = MMCZip::findFolderOfFileInZipList(m_packZip.get(), filesToSearch, fileName);
    if (!rootDirectory.isNull())
    {
        if (fileName == "instance.cfg")
        {
            // process as MultiMC instance/pack
            qDebug() << "MultiMC:" << rootDirectory;
            m_modpackType = ModpackType::MultiMC;
        }
        else if (fileName == "manifest.json")
        {
            // process as CurseForge pack
            qDebug() << "CurseForge:" << rootDirectory;
            m_modpackType = ModpackType::CurseForge;
        }
        else if (fileName == "modrinth.index.json")
        {
            // process as Modrinth pack
            qDebug() << "Modrinth:" << rootDirectory;
            m_modpackType = ModpackType::Modrinth;
        }
        root = rootDirectory;
    }
    else
    {
        QuaZipDir packZipDir(m_packZip.get());
        bool technicFound = packZipDir.exists("/bin/modpack.jar") || packZipDir.exists("/bin/version.json");
        if (technicFound)
        {
            // process as Technic pack
            qDebug() << "Technic:" << technicFound;
            extractDir.mkpath(".minecraft");
            extractDir.cd(".minecraft");
            m_modpackType = ModpackType::Technic;
        }
    }
    if (m_modpackType == ModpackType::Unknown)
    {
        emitFailed(tr("Archive does not contain a recognized modpack type."));
        return;
    }
    QPointer<InstanceImportTask> task(this);
    auto progressCallback = [task](qint64 current, qint64 total)
    {
        if (!task || total <= 0)
        {
            return;
        }
        QMetaObject::invokeMethod(task.data(), "extractProgressChanged", Qt::QueuedConnection, Q_ARG(qint64, current), Q_ARG(qint64, total));
    };

    auto extractSubDirWithProgress = static_cast<nonstd::optional<QStringList> (*)(QuaZip *, const QString &, const QString &, const MMCZip::ProgressCallback &)>(&MMCZip::extractSubDir);

    // make sure we extract just the pack
    m_extractFuture = QtConcurrent::run(QThreadPool::globalInstance(), extractSubDirWithProgress, m_packZip.get(), root, extractDir.absolutePath(), progressCallback);
    connect(&m_extractFutureWatcher, &QFutureWatcher<nonstd::optional<QStringList>>::finished, this, &InstanceImportTask::extractFinished);
    connect(&m_extractFutureWatcher, &QFutureWatcher<nonstd::optional<QStringList>>::canceled, this, &InstanceImportTask::extractAborted);
    m_extractFutureWatcher.setFuture(m_extractFuture);
}

void InstanceImportTask::extractProgressChanged(qint64 current, qint64 total)
{
    if (m_downloadRequired)
    {
        setProgress(50 + current * 50 / total, 100);
    }
    else
    {
        setProgress(current, total);
    }
}

void InstanceImportTask::extractFinished()
{
    m_packZip.reset();
    if (!m_extractFuture.result())
    {
        emitFailed(tr("Failed to extract modpack"));
        return;
    }
    QDir extractDir(m_stagingPath);

    qDebug() << "Fixing permissions for extracted pack files...";
    QDirIterator it(extractDir, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        auto filepath = it.next();
        QFileInfo file(filepath);
        auto permissions = QFile::permissions(filepath);
        auto origPermissions = permissions;
        if (file.isDir())
        {
            // Folder +rwx for current user
            permissions |= QFileDevice::Permission::ReadUser | QFileDevice::Permission::WriteUser | QFileDevice::Permission::ExeUser;
        }
        else
        {
            // File +rw for current user
            permissions |= QFileDevice::Permission::ReadUser | QFileDevice::Permission::WriteUser;
        }
        if (origPermissions != permissions)
        {
            if (!QFile::setPermissions(filepath, permissions))
            {
                logWarning(tr("Could not fix permissions for %1").arg(filepath));
            }
            else
            {
                qDebug() << "Fixed" << filepath;
            }
        }
    }

    switch (m_modpackType)
    {
    case ModpackType::MultiMC:
        processMultiMC();
        return;
    case ModpackType::Technic:
        processTechnic();
        return;
    case ModpackType::CurseForge:
        processCurseForge();
        return;
    case ModpackType::Modrinth:
        processModrinth();
        return;
    case ModpackType::Unknown:
        emitFailed(tr("Archive does not contain a recognized modpack type."));
        return;
    }
}

void InstanceImportTask::extractAborted()
{
    emitFailed(tr("Instance import has been aborted."));
    return;
}

void InstanceImportTask::processCurseForge()
{
    const static QMap<QString, QString> forgemap = {
        {"1.2.5", "3.4.9.171"},
        {"1.4.2", "6.0.1.355"},
        {"1.4.7", "6.6.2.534"},
        {"1.5.2", "7.8.1.737"}};
    CurseForge::Manifest pack;
    try
    {
        QString configPath = FS::PathCombine(m_stagingPath, "manifest.json");
        CurseForge::loadManifest(pack, configPath);
        QFile::remove(configPath);
    }
    catch (const JSONValidationError &e)
    {
        emitFailed(tr("Could not understand pack manifest:\n") + e.cause());
        return;
    }
    if (!pack.overrides.isEmpty())
    {
        QString overridePath = FS::PathCombine(m_stagingPath, pack.overrides);
        if (QFile::exists(overridePath))
        {
            QString mcPath = FS::PathCombine(m_stagingPath, "minecraft");
            if (!QFile::rename(overridePath, mcPath))
            {
                emitFailed(tr("Could not rename the overrides folder:\n") + pack.overrides);
                return;
            }
        }
        else
        {
            logWarning(tr("The specified overrides folder (%1) is missing. Maybe the modpack was already used before?").arg(pack.overrides));
        }
    }

    QString forgeVersion;
    QString fabricVersion;
    QString neoforgecVersion;
    for (auto &loader : pack.minecraft.modLoaders)
    {
        auto id = loader.id;
        if (id.startsWith("forge-"))
        {
            id.remove("forge-");
            forgeVersion = id;
            continue;
        }
        if (id.startsWith("fabric-"))
        {
            id.remove("fabric-");
            fabricVersion = id;
            continue;
        }
        if (id.startsWith("neoforge-"))
        {
            id.remove("neoforge-");
            neoforgecVersion = id;
            continue;
        }
        logWarning(tr("Unknown mod loader in manifest: %1").arg(id));
    }

    QString configPath = FS::PathCombine(m_stagingPath, "instance.cfg");
    auto instanceSettings = std::make_shared<INISettingsObject>(configPath);
    instanceSettings->registerSetting("InstanceType", "Legacy");
    instanceSettings->set("InstanceType", "OneSix");
    MinecraftInstance instance(m_globalSettings, instanceSettings, m_stagingPath);
    auto mcVersion = pack.minecraft.version;
    // Hack to correct some 'special sauce'...
    if (mcVersion.endsWith('.'))
    {
        mcVersion.remove(QRegExp("[.]+$"));
        logWarning(tr("Mysterious trailing dots removed from Minecraft version while importing pack."));
    }
    auto components = instance.getPackProfile();
    components->buildingFromScratch();
    components->setComponentVersion("net.minecraft", mcVersion, true);
    if (!forgeVersion.isEmpty())
    {
        // FIXME: dirty, nasty, hack. Proper solution requires dependency resolution and knowledge of the metadata.
        if (forgeVersion == "recommended")
        {
            if (forgemap.contains(mcVersion))
            {
                forgeVersion = forgemap[mcVersion];
            }
            else
            {
                logWarning(tr("Could not map recommended forge version for Minecraft %1").arg(mcVersion));
            }
        }
        components->setComponentVersion("net.minecraftforge", forgeVersion);
    }
    if (!fabricVersion.isEmpty())
    {
        components->setComponentVersion("net.fabricmc.fabric-loader", fabricVersion);
    }
    if (!neoforgecVersion.isEmpty())
    {
        components->setComponentVersion("net.neoforged", neoforgecVersion);
    }
    components->saveNow();
    if (m_instIcon != "default")
    {
        instance.setIconKey(m_instIcon);
    }
    QString jarmodsPath = FS::PathCombine(m_stagingPath, "minecraft", "jarmods");
    QFileInfo jarmodsInfo(jarmodsPath);
    if (jarmodsInfo.isDir())
    {
        // install all the jar mods
        qDebug() << "Found jarmods:";
        QDir jarmodsDir(jarmodsPath);
        QStringList jarMods;
        for (auto info : jarmodsDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files))
        {
            qDebug() << info.fileName();
            jarMods.push_back(info.absoluteFilePath());
        }
        auto profile = instance.getPackProfile();
        profile->installJarMods(jarMods);
        // nuke the original files
        FS::deletePath(jarmodsPath);
    }
    QString cleanVersion = pack.version;
    cleanVersion.remove('v');
    if (m_instName.contains(cleanVersion))
    {
        m_instName.replace(cleanVersion, "");
        m_instName.remove(QRegExp("-+$"));
    }

    int index = m_instName.indexOf("_v");
    if (index != -1)
    {
        m_instName.truncate(index);
    }
    instance.setModpackInfo(m_addonId, m_fileId, "curseforge");
    const QString finalInstanceName = QString("%1_v%2").arg(m_instName).arg(cleanVersion);
    if (m_instIcon == "default")
    {
        const QString importedIconKey = importPackIcon(instance.instanceRoot(), iconKeyForInstanceName(m_instName, m_stagingPath));
        if (!importedIconKey.isEmpty())
        {
            instance.setIconKey(importedIconKey);
        }
        else if (pack.name.contains("Direwolf20"))
        {
            instance.setIconKey("steve");
        }
        else if (pack.name.contains("FTB") || pack.name.contains("Feed The Beast"))
        {
            instance.setIconKey("ftb_logo");
        }
        else
        {
            // default to something other than the MultiMC default to distinguish these
            instance.setIconKey("flame");
        }
    }
    instance.setName(finalInstanceName);
    m_modIdResolver = new CurseForge::FileResolvingTask(APPLICATION->network(), pack, m_stagingPath, m_updateContext);
    connect(m_modIdResolver.get(), &CurseForge::FileResolvingTask::succeeded, [&]()
            {
        auto results = m_modIdResolver->getResults();
        int downloadCount = 0;
        m_filesNetJob = new NetJob(tr("Mod download"), APPLICATION->network());
        for(auto result: results.files)
        {
            if(result.url.isEmpty())
            {
                continue;
            }
            QString filename = result.fileName;
            if (!result.required)
            {
                filename += ".disabled";
            }

            auto relpath = FS::PathCombine("minecraft", result.targetFolder, filename);
            auto path = FS::PathCombine(m_stagingPath , relpath);

            switch(result.type)
            {
            case CurseForge::File::Type::Folder:
            {
                logWarning(tr("This 'Folder' may need extracting: %1").arg(relpath));
                // fall-through intentional, we treat these as plain old mods and dump them wherever.
            }
            case CurseForge::File::Type::SingleFile:
            case CurseForge::File::Type::Mod:
            {

                qDebug() << "Will download" << result.url << "to" << path;
                auto dl = Net::Download::makeFile(result.url, path);
                m_filesNetJob->addNetAction(dl);
                downloadCount++;
                break;


            }
            case CurseForge::File::Type::Modpack:
                logWarning(tr("Nesting modpacks in modpacks is not implemented, nothing was downloaded: %1").arg(relpath));
                break;
            case CurseForge::File::Type::Cmod2:
            case CurseForge::File::Type::Ctoc:
            case CurseForge::File::Type::Unknown:
                logWarning(tr("Unrecognized/unhandled PackageType for: %1").arg(relpath));
                break;
            }
        }
        connect(m_filesNetJob.get(), &NetJob::succeeded, this, [&]()
        {
            m_filesNetJob.reset();
            if (m_updateContext.isValid() && m_modIdResolver)
            {
                m_modIdResolver->performCleanup();
            }
            m_modIdResolver.reset();
            emitSucceeded();
        }
        );
        connect(m_filesNetJob.get(), &NetJob::failed, [&](QString reason)
        {
            m_filesNetJob.reset();
            if (m_updateContext.isValid() && m_modIdResolver)
            {
                m_modIdResolver->performRollback();
            }
            m_modIdResolver.reset();
            emitFailed(reason);
        });
        connect(m_filesNetJob.get(), &NetJob::progress, [&](qint64 current, qint64 total)
        {
            setProgress(current, total);
        });
        if (downloadCount > 0) {
            setStatus(tr("Downloading mods..."));
            m_filesNetJob->start();
        } else {
            m_filesNetJob.reset();
            emitSucceeded();
        } });
    connect(m_modIdResolver.get(), &CurseForge::FileResolvingTask::failed, [&](QString reason)
            {
        if (m_updateContext.isValid() && m_modIdResolver)
        {
            m_modIdResolver->performRollback();
        }
        m_modIdResolver.reset();
        emitFailed(tr("Unable to resolve mod IDs:\n") + reason); });
    connect(m_modIdResolver.get(), &CurseForge::FileResolvingTask::progress, [&](qint64 current, qint64 total)
            { setProgress(current, total); });
    connect(m_modIdResolver.get(), &CurseForge::FileResolvingTask::status, [&](QString status)
            { setStatus(status); });
    m_modIdResolver->start();
}

void InstanceImportTask::processTechnic()
{
    shared_qobject_ptr<Technic::TechnicPackProcessor> packProcessor = new Technic::TechnicPackProcessor();
    connect(packProcessor.get(), &Technic::TechnicPackProcessor::succeeded, this, &InstanceImportTask::emitSucceeded);
    connect(packProcessor.get(), &Technic::TechnicPackProcessor::failed, this, &InstanceImportTask::emitFailed);
    packProcessor->run(m_globalSettings, m_instName, m_instIcon, m_stagingPath);
}

void InstanceImportTask::processMultiMC()
{
    QString configPath = FS::PathCombine(m_stagingPath, "instance.cfg");
    auto instanceSettings = std::make_shared<INISettingsObject>(configPath);
    instanceSettings->registerSetting("InstanceType", "Legacy");

    NullInstance instance(m_globalSettings, instanceSettings, m_stagingPath);

    // reset time played on import... because packs.
    instance.resetTimePlayed();

    // set a new nice name
    instance.setName(m_instName);

    // if the icon was specified by user, use that. otherwise pull icon from the pack
    if (m_instIcon != "default")
    {
        instance.setIconKey(m_instIcon);
    }
    else
    {
        m_instIcon = instance.iconKey();

        const QString importedIconKey = importPackIcon(instance.instanceRoot(), iconKeyForInstanceName(m_instName, m_stagingPath));
        if (!importedIconKey.isEmpty())
        {
            instance.setIconKey(importedIconKey);
        }
        else
        {
            auto importIconPath = IconUtils::findBestIconIn(instance.instanceRoot(), m_instIcon);
            if (!importIconPath.isNull() && QFile::exists(importIconPath))
            {
                // import icon
                auto iconList = APPLICATION->icons();
                if (iconList->iconFileExists(m_instIcon))
                {
                    iconList->deleteIcon(m_instIcon);
                }
                iconList->installIcons({importIconPath});
            }
        }
    }
    emitSucceeded();
}

namespace
{
    QString instanceDirForStagingPath(const QString &stagingPath)
    {
        QDir dir(stagingPath);
        dir.cdUp();
        if (dir.dirName() == "_LAUNCHER_TEMP")
        {
            dir.cdUp();
        }
        return dir.absolutePath();
    }

    QString iconKeyForInstanceName(const QString &instanceName, const QString &stagingPath)
    {
        QString baseInstanceName = instanceName;
        baseInstanceName.remove(QRegExp("_v[^_]+$"));
        QString iconKey = FS::DirNameFromString(baseInstanceName, instanceDirForStagingPath(stagingPath));
        iconKey.replace('.', '-');
        return iconKey;
    }

    QString findImportedPackIcon(const QString &folder)
    {
        QDir dir(folder);
        const auto iconCandidates = dir.entryInfoList(QStringList() << "curseforge_*", QDir::Files, QDir::Name);
        for (const auto &candidate : iconCandidates)
        {
            if (!candidate.suffix().isEmpty())
            {
                continue;
            }
            if (!QIcon(candidate.absoluteFilePath()).isNull())
            {
                return candidate.absoluteFilePath();
            }
        }

        const QStringList gameRoots = {".minecraft", "minecraft"};
        for (const auto &gameRoot : gameRoots)
        {
            const QString iconPath = FS::PathCombine(folder, gameRoot, "icon.png");
            if (QFileInfo::exists(iconPath) && !QIcon(iconPath).isNull())
            {
                return iconPath;
            }
        }

        return QString();
    }

    QString importPackIcon(const QString &folder, const QString &instanceIconKey)
    {
        auto importIconPath = findImportedPackIcon(folder);
        if (importIconPath.isNull())
        {
            return QString();
        }

        QFileInfo importIconInfo(importIconPath);
        const bool isNamedIcon = importIconInfo.fileName() != "icon.png";
        const QString iconKey = isNamedIcon || instanceIconKey.isEmpty() ? importIconInfo.baseName() : instanceIconKey;
        const QString iconFileName = isNamedIcon ? importIconInfo.fileName() : iconKey;
        auto iconList = APPLICATION->icons();
        if (iconList->iconFileExists(iconKey))
        {
            iconList->deleteIcon(iconKey);
        }
        QFile::remove(FS::PathCombine(iconList->getDirectory(), iconFileName));
        iconList->installIcon(importIconPath, iconFileName);
        return iconKey;
    }

    bool mergeOverrides(const QString &fromDir, const QString &toDir)
    {
        QDir dir(fromDir);
        if (!dir.exists())
        {
            return true;
        }
        if (!FS::ensureFolderPathExists(toDir))
        {
            return false;
        }
        const int absSourcePathLength = dir.absoluteFilePath(fromDir).length();

        QDirIterator it(fromDir, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            it.next();
            const auto fileInfo = it.fileInfo();
            auto fileName = fileInfo.fileName();
            if (fileName == "." || fileName == "..")
            {
                continue;
            }
            const QString subPathStructure = fileInfo.absoluteFilePath().mid(absSourcePathLength);
            const QString constructedAbsolutePath = toDir + subPathStructure;

            if (fileInfo.isDir())
            {
                // Create directory in target folder
                dir.mkpath(constructedAbsolutePath);
            }
            else if (fileInfo.isFile())
            {
                QFileInfo targetFileInfo(constructedAbsolutePath);
                if (targetFileInfo.exists())
                {
                    continue;
                }
                // move
                QFile::rename(fileInfo.absoluteFilePath(), constructedAbsolutePath);
            }
        }

        dir.removeRecursively();
        return true;
    }

}

void InstanceImportTask::processModrinth()
{
    std::vector<Modrinth::File> files;
    QString minecraftVersion, fabricVersion, quiltVersion, forgeVersion, neoforgeVersion;
    try
    {
        QString indexPath = FS::PathCombine(m_stagingPath, "modrinth.index.json");
        auto doc = Json::requireDocument(indexPath);
        auto obj = Json::requireObject(doc, "modrinth.index.json");
        int formatVersion = Json::requireInteger(obj, "formatVersion", "modrinth.index.json");
        if (formatVersion == 1)
        {
            auto game = Json::requireString(obj, "game", "modrinth.index.json");
            if (game != "minecraft")
            {
                throw JSONValidationError("Unknown game: " + game);
            }

            auto jsonFiles = Json::requireIsArrayOf<QJsonObject>(obj, "files", "modrinth.index.json");
            for (auto &obj : jsonFiles)
            {
                Modrinth::File file;
                auto dirtyPath = Json::requireString(obj, "path");
                dirtyPath.replace('\\', '/');
                auto simplifiedPath = QDir::cleanPath(dirtyPath);
                QFileInfo fileInfo(simplifiedPath);
                if (simplifiedPath.startsWith("../") || simplifiedPath.contains("/../") || fileInfo.isAbsolute())
                {
                    throw JSONValidationError("Invalid path found in modpack files:\n\n" + simplifiedPath);
                }
                file.path = simplifiedPath;

                // env doesn't have to be present, in that case mod is required
                auto env = Json::ensureObject(obj, "env");
                auto clientEnv = Json::ensureString(env, "client", "required");

                if (clientEnv == "required")
                {
                    // NOOP
                }
                else if (clientEnv == "optional")
                {
                    file.path += ".disabled";
                }
                else if (clientEnv == "unsupported")
                {
                    continue;
                }

                QJsonObject hashes = Json::requireObject(obj, "hashes");
                QString hash;
                QCryptographicHash::Algorithm hashAlgorithm;
                hash = Json::ensureString(hashes, "sha256");
                hashAlgorithm = QCryptographicHash::Sha256;
                if (hash.isEmpty())
                {
                    hash = Json::ensureString(hashes, "sha512");
                    hashAlgorithm = QCryptographicHash::Sha512;
                    if (hash.isEmpty())
                    {
                        hash = Json::ensureString(hashes, "sha1");
                        hashAlgorithm = QCryptographicHash::Sha1;
                        if (hash.isEmpty())
                        {
                            throw JSONValidationError("No hash found for: " + file.path);
                        }
                    }
                }
                file.hash = QByteArray::fromHex(hash.toLatin1());
                file.hashAlgorithm = hashAlgorithm;
                // Do not use requireUrl, which uses StrictMode, instead use QUrl's default TolerantMode (as Modrinth seems to incorrectly handle spaces)
                file.download = Json::requireValueString(Json::ensureArray(obj, "downloads").first(), "Download URL for " + file.path);
                if (!file.download.isValid())
                {
                    throw JSONValidationError("Download URL for " + file.path + " is not a correctly formatted URL");
                }
                files.push_back(file);
            }

            auto dependencies = Json::requireObject(obj, "dependencies", "modrinth.index.json");
            for (auto it = dependencies.begin(), end = dependencies.end(); it != end; ++it)
            {
                QString name = it.key();
                if (name == "minecraft")
                {
                    if (!minecraftVersion.isEmpty())
                        throw JSONValidationError("Duplicate Minecraft version");
                    minecraftVersion = Json::requireValueString(*it, "Minecraft version");
                }
                else if (name == "fabric-loader")
                {
                    if (!fabricVersion.isEmpty())
                        throw JSONValidationError("Duplicate Fabric Loader version");
                    fabricVersion = Json::requireValueString(*it, "Fabric Loader version");
                }
                else if (name == "quilt-loader")
                {
                    if (!quiltVersion.isEmpty())
                        throw JSONValidationError("Duplicate Quilt Loader version");
                    quiltVersion = Json::requireValueString(*it, "Quilt Loader version");
                }
                else if (name == "forge")
                {
                    if (!forgeVersion.isEmpty())
                        throw JSONValidationError("Duplicate Forge version");
                    forgeVersion = Json::requireValueString(*it, "Forge version");
                }
                else if (name == "neoforge")
                {
                    if (!neoforgeVersion.isEmpty())
                        throw JSONValidationError("Duplicate NeoForge version");
                    neoforgeVersion = Json::requireValueString(*it, "NeoForge version");
                }
                else
                {
                    throw JSONValidationError("Unknown dependency type: " + name);
                }
            }
        }
        else
        {
            throw JSONValidationError(QStringLiteral("Unknown format version: %s").arg(formatVersion));
        }
        QFile::remove(indexPath);
    }
    catch (const JSONValidationError &e)
    {
        emitFailed(tr("Could not understand pack index:\n") + e.cause());
        return;
    }
    QString clientOverridePath = FS::PathCombine(m_stagingPath, "client-overrides");
    if (QFile::exists(clientOverridePath))
    {
        QString mcPath = FS::PathCombine(m_stagingPath, ".minecraft");
        if (!QFile::rename(clientOverridePath, mcPath))
        {
            emitFailed(tr("Could not rename the overrides folder:\n") + "overrides");
            return;
        }
    }

    // TODO: only extract things we actually want instead of everything only to just delete it afterwards ...
    if (!mergeOverrides(FS::PathCombine(m_stagingPath, "client-overrides"), FS::PathCombine(m_stagingPath, ".minecraft")))
    {
        emitFailed(tr("Could not merge the overrides folder:\n") + "client-overrides");
        return;
    }

    if (!mergeOverrides(FS::PathCombine(m_stagingPath, "overrides"), FS::PathCombine(m_stagingPath, ".minecraft")))
    {
        emitFailed(tr("Could not merge the overrides folder:\n") + "overrides");
        return;
    }

    FS::deletePath(FS::PathCombine(m_stagingPath, "server-overrides"));

    QString configPath = FS::PathCombine(m_stagingPath, "instance.cfg");
    auto instanceSettings = std::make_shared<INISettingsObject>(configPath);
    instanceSettings->registerSetting("InstanceType", "Legacy");
    instanceSettings->set("InstanceType", "OneSix");
    MinecraftInstance instance(m_globalSettings, instanceSettings, m_stagingPath);
    auto components = instance.getPackProfile();
    components->buildingFromScratch();
    components->setComponentVersion("net.minecraft", minecraftVersion, true);
    if (!fabricVersion.isEmpty())
        components->setComponentVersion("net.fabricmc.fabric-loader", fabricVersion, true);
    if (!quiltVersion.isEmpty())
        components->setComponentVersion("org.quiltmc.quilt-loader", quiltVersion, true);
    if (!forgeVersion.isEmpty())
        components->setComponentVersion("net.minecraftforge", forgeVersion, true);
    if (!neoforgeVersion.isEmpty())
        components->setComponentVersion("net.neoforged", neoforgeVersion, true);
    if (m_instIcon != "default")
    {
        instance.setIconKey(m_instIcon);
    }
    else
    {
        const QString importedIconKey = importPackIcon(instance.instanceRoot(), iconKeyForInstanceName(m_instName, m_stagingPath));
        if (!importedIconKey.isEmpty())
        {
            instance.setIconKey(importedIconKey);
        }
    }
    instance.setName(m_instName);
    instance.saveNow();

    m_filesNetJob = new NetJob(tr("Mod download"), APPLICATION->network());
    for (auto &file : files)
    {
        auto path = FS::PathCombine(m_stagingPath, ".minecraft", file.path);
        qDebug() << "Will download" << file.download << "to" << path;
        auto dl = Net::Download::makeFile(file.download, path);
        dl->addValidator(new Net::ChecksumValidator(file.hashAlgorithm, file.hash));
        m_filesNetJob->addNetAction(dl);
    }
    connect(m_filesNetJob.get(), &NetJob::succeeded, this, [&]()
            {
        m_filesNetJob.reset();
        emitSucceeded(); });
    connect(m_filesNetJob.get(), &NetJob::failed, [&](const QString &reason)
            {
        m_filesNetJob.reset();
        emitFailed(reason); });
    connect(m_filesNetJob.get(), &NetJob::progress, [&](qint64 current, qint64 total)
            { setProgress(current, total); });
    setStatus(tr("Downloading mods..."));
    m_filesNetJob->start();
}

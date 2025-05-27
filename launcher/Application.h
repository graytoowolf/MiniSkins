#pragma once

#include <QApplication>
#include <memory>
#include <QDebug>
#include <QFlag>
#include <QIcon>
#include <QDateTime>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <updater/GoUpdate.h>

#include "DownloadSource.h"
#include "net/NetJob.h"
#include <BaseInstance.h>

#include "minecraft/launch/QuickPlayTarget.h"

class LaunchController;
class LocalPeer;
class InstanceWindow;
class MainWindow;
class SetupWizard;
class GenericPageProvider;
class HttpMetaCache;
class SettingsObject;
class InstanceList;
class AccountList;
class IconList;
class QNetworkAccessManager;
class JavaInstallList;
class UpdateChecker;
class BaseProfilerFactory;
class BaseDetachedToolFactory;
class TranslationsModel;
class ITheme;
class MCEditTool;

namespace Meta
{
    class Index;
}

#if defined(APPLICATION)
#undef APPLICATION
#endif
#define APPLICATION (static_cast<Application *>(QCoreApplication::instance()))
class Application : public QApplication
{
    // friends for the purpose of limiting access to deprecated stuff
    Q_OBJECT
public:
    enum Status
    {
        StartingUp,
        Failed,
        Succeeded,
        Initialized
    };

public:
    Application(int &argc, char **argv);
    virtual ~Application();

    std::shared_ptr<SettingsObject> settings() const
    {
        return m_settings;
    }

    qint64 timeSinceStart() const
    {
        return startTime.msecsTo(QDateTime::currentDateTime());
    }

    QIcon getThemedIcon(const QString &name);

    void setIconTheme(const QString &name);

    std::vector<ITheme *> getValidApplicationThemes();

    void setApplicationTheme(const QString &name, bool initial);

    void setData(const QString &addonId, const QString &fileId, const QString &ID, const QString &splatform, const QString &downloadUrl);

    void setUpdating(bool updating);

    QString getAddonId() const;
    QString getFileId() const;
    QString getID() const;
    QString getSplatform() const;
    bool isUpdating() const;

    shared_qobject_ptr<UpdateChecker> updateChecker()
    {
        return m_updateChecker;
    }

    std::shared_ptr<TranslationsModel> translations();

    std::shared_ptr<JavaInstallList> javalist();

    std::shared_ptr<InstanceList> instances() const
    {
        return m_instances;
    }

    std::shared_ptr<IconList> icons() const
    {
        return m_icons;
    }

    MCEditTool *mcedit() const
    {
        return m_mcedit.get();
    }

    shared_qobject_ptr<AccountList> accounts() const
    {
        return m_accounts;
    }

    QString msaClientId() const;

    QString curseAPIKey() const;

    Status status() const
    {
        return m_status;
    }

    const QMap<QString, std::shared_ptr<BaseProfilerFactory>> &profilers() const
    {
        return m_profilers;
    }

    void updateProxySettings(QString proxyTypeStr, QString addr, int port, QString user, QString password);

    shared_qobject_ptr<QNetworkAccessManager> network();

    shared_qobject_ptr<HttpMetaCache> metacache();

    shared_qobject_ptr<Meta::Index> metadataIndex();

    QString getJarsPath();

    bool getconfigfile();

    // Getter methods
    const QList<DownloadSource> &getDownloadSources() const;
    const QList<YggSource> &getYggSources() const;

    // Add methods
    void addDownloadSource(const DownloadSource &source);
    void addYggSource(const YggSource &source, int position = -1);

    // Clear methods
    void clearDownloadSources();
    void clearYggSources();

    /// this is the root of the 'installation'. Used for automatic updates
    const QString &root()
    {
        return m_rootPath;
    }

    /*!
     * Opens a json file using either a system default editor, or, if not empty, the editor
     * specified in the settings
     */
    bool openJsonEditor(const QString &filename);

    InstanceWindow *showInstanceWindow(InstancePtr instance, QString page = QString());
    MainWindow *showMainWindow(bool minimized = false);

    void updateIsRunning(bool running);
    bool updatesAreAllowed();

    void ShowGlobalSettings(class QWidget *parent, QString open_page = QString());

    // MOD黑名单相关方法
    const QMap<int, QString> &getModBlacklist() const { return m_modBlacklist; } // 修改返回类型
    bool isModBlacklisted(const int &projectId) const;
    void addModToBlacklist(const int &projectId, const QString &name);         // 返回void，不再直接保存
    void removeModFromBlacklist(const int &projectId);                         // 返回void
    void updateModBlacklistName(const int &projectId, const QString &newName); // 返回void
    // 通过 projectId 获取黑名单中的 mod 名称
    QString getModNameFromBlacklist(int projectId) const;
    // 批量操作黑名单
    void addModsToBlacklist(const QMap<int, QString> &mods);
    void removeModsFromBlacklist(const QList<int> &modIds);

    // MOD白名单相关方法
    const QMap<int, QString> &getModWhitelist() const { return m_modWhitelist; }
    bool isModWhitelisted(const int &projectId) const;
    void addModToWhitelist(const int &projectId, const QString &name);         // 返回void
    void removeModFromWhitelist(const int &projectId);                         // 返回void
    void updateModWhitelistName(const int &projectId, const QString &newName); // 返回void
    QString getModNameFromWhitelist(int projectId) const;
    // 批量操作白名单
    void addModsToWhitelist(const QMap<int, QString> &mods);
    void removeModsFromWhitelist(const QList<int> &modIds);

signals:
    void updateAllowedChanged(bool status);
    void globalSettingsAboutToOpen();
    void globalSettingsClosed();

public slots:
    bool launch(
        InstancePtr instance,
        bool online = true,
        BaseProfilerFactory *profiler = nullptr,
        QuickPlayTargetPtr quickPlayTarget = nullptr,
        MinecraftAccountPtr accountToUse = nullptr,
        const QString &offlineName = QString());
    bool kill(InstancePtr instance);

private slots:
    void requestFinished();
    void authlibFinished();
    void sourceFinished();
    bool FileHash(QString srcDir, QString hash256);
    void on_windowClose();
    void messageReceived(const QByteArray &message);
    void controllerSucceeded();
    void controllerFailed(const QString &error);
    void setupWizardFinished(int status);

private:
    bool createSetupWizard();
    void performMainStartupAction();

    // sets the fatal error message and m_status to Failed.
    void showFatalErrorMessage(const QString &title, const QString &content);

private:
    void addRunningInstance();
    void subRunningInstance();
    bool shouldExitNow() const;

private:
    NetJob::Ptr m_filesNetJob;
    NetJob::Ptr authlib_filesNetJob;
    QByteArray authlib_response;
    QByteArray response;
    QDateTime startTime;

    shared_qobject_ptr<QNetworkAccessManager> m_network;

    shared_qobject_ptr<UpdateChecker> m_updateChecker;
    shared_qobject_ptr<AccountList> m_accounts;

    shared_qobject_ptr<HttpMetaCache> m_metacache;
    shared_qobject_ptr<Meta::Index> m_metadataIndex;

    std::shared_ptr<SettingsObject> m_settings;
    std::shared_ptr<InstanceList> m_instances;
    std::shared_ptr<IconList> m_icons;
    std::shared_ptr<JavaInstallList> m_javalist;
    std::shared_ptr<TranslationsModel> m_translations;
    std::shared_ptr<GenericPageProvider> m_globalSettingsProvider;
    std::map<QString, std::unique_ptr<ITheme>> m_themes;
    std::unique_ptr<MCEditTool> m_mcedit;
    QString m_jarsPath;
    QSet<QString> m_features;

    QMap<QString, std::shared_ptr<BaseProfilerFactory>> m_profilers;

    QString m_rootPath;
    Status m_status = Application::StartingUp;

    QList<DownloadSource> downloadSources;
    QList<YggSource> yggSources;
    QSet<QString> yggSourceUrls;

    QMap<int, QString> m_modBlacklist;
    QMap<int, QString> m_modWhitelist;
    bool m_modListDirty = false; // 标记mod列表是否被修改

#if defined Q_OS_WIN32
    // used on Windows to attach the standard IO streams
    bool consoleAttached = false;
#endif

    // FIXME: attach to instances instead.
    struct InstanceXtras
    {
        InstanceWindow *window = nullptr;
        shared_qobject_ptr<LaunchController> controller;
    };
    std::map<QString, InstanceXtras> m_instanceExtras;

    // main state variables
    size_t m_openWindows = 0;
    size_t m_runningInstances = 0;
    bool m_updateRunning = false;

    // main window, if any
    MainWindow *m_mainWindow = nullptr;

    // peer launcher instance connector - used to implement single instance launcher and signalling
    LocalPeer *m_peerInstance = nullptr;

    SetupWizard *m_setupWizard = nullptr;

public:
    QString m_instanceIdToLaunch;
    QString m_serverToJoin;
    QString m_worldToJoin;
    QString m_profileToUse;
    QString addonId;
    QString fileId;
    QString ID;
    QString splatform;
    QString downloadUrl;
    bool updating = false;
    bool m_offline = false;
    QString m_offlineName;
    bool m_liveCheck = false;
    QUrl m_zipToImport;
    std::unique_ptr<QFile> logFile;

private:
    void loadModList();
    bool saveModList(); // 保持bool返回，以便知道是否保存成功
};

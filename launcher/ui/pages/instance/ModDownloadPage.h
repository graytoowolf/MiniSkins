#pragma once

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QMessageBox>
#include <QLineEdit>
#include <QComboBox>
#include <QProgressBar>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QPixmap>
#include <QFileInfo>
#include <QMap>
#include <functional>
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "ui/pages/BasePage.h"
#include "Application.h"

namespace Ui
{
class ModDownloadPage;
}

class ModDownloadPage : public QMainWindow, public BasePage
{
    Q_OBJECT

public:
    struct ModDownloadInfo
    {
        QString name;
        QString author;
        QString description;
        QString downloads;
        QString updateTime;
        QString iconLetter;
        QString iconColor;
        QString category; // 添加分类字段
        QString logoUrl;  // 添加logo URL字段
        QString logoFileName;
        int modId; // 添加模组ID字段
        int fileID;
    };

    struct DownloadItem
    {
        QString downloadUrl;
        QString fileName;
        int modId;
        int fileID;
        QList<int> requiredDependencies; // 必须依赖的模组ID列表
    };

    explicit ModDownloadPage(MinecraftInstance *inst, QWidget *parent = nullptr);
    ~ModDownloadPage();
    bool apply() override;

    virtual QString displayName() const override
    {
        return tr("Download Mods");
    }
    virtual QIcon icon() const override
    {
        return APPLICATION->getThemedIcon("coremods");
    }
    virtual QString id() const override
    {
        return "mod-download";
    }
    virtual QString helpPage() const override
    {
        return "Mod-Downloads";
    }

private slots:
    void onSearch();          // 搜索按钮点击事件
    void onFilterChanged();   // 分类或排序变化事件
    void onScroll(int value); // 滚动事件，用于懒加载
    void loadInitialMods();   // 加载初始模组数据

private:
    Ui::ModDownloadPage *ui;
    std::shared_ptr<PackProfile> m_profile;
    MinecraftInstance *m_inst;
    int m_currentPage;
    bool m_isLoading;
    bool m_hasMoreMods;
    QString m_gameVersion;                      // 游戏版本
    QString m_modLoader;                        // 模组加载器类型
    QNetworkAccessManager *m_network = nullptr; // 网络管理器

    void downloadLogo(const ModDownloadInfo &modInfo, QLabel *iconLabel, MetaEntryPtr entry);
    static QMap<QString, QIcon> logoCache;

    // 初始化UI
    void initUI();
    void cleanup();

    // 获取游戏版本和模组加载器信息
    void getGameInfoByIteration();
    int getModLoaderTypeApiId();

    // 加载更多模组
    void loadMoreMods();

    // 清除模组列表
    void clearModList();

    // 检查模组是否已安装
    bool isModInstalled(int modId);

    // 将模组信息添加到mod.json文件
    void addModToJson(const ModDownloadInfo &modInfo);

    // 创建模组项目小部件
    QWidget *createModItemWidget(const ModDownloadInfo &mod);

    // 新的下载队列机制
    QList<DownloadItem> m_downloadQueue;
    QList<ModDownloadInfo> m_completedMods; // 存储下载完成的模组信息，用于批量写入
    void fetchModDownloadInfo(int modId, std::function<void(const DownloadItem &)> callback);
    void buildDownloadQueue(int modId, std::function<void()> onComplete);
    void processDownloadQueue(QProgressBar *progressBar, QHBoxLayout *statsLayout);
    void downloadSingleItem(const DownloadItem &item, QProgressBar *progressBar, QHBoxLayout *statsLayout, std::function<void()> onComplete);
    void downloadNextInQueue(QProgressBar *progressBar, QHBoxLayout *statsLayout, int index);
    void fetchModName(int modId, std::function<void(const QString &)> callback);

    // 跟踪已处理的依赖，避免重复下载
    static QSet<int> processedDependencies;
};

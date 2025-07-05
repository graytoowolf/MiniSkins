#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

class ModDownloadPageUIFactory
{
public:
    // 模组安装状态枚举
    enum ModInstallStatus
    {
        MOD_NOT_INSTALLED, //!< Mod is not installed
        MOD_INSTALLED,     //!< Mod is installed with same fileID
        MOD_NEEDS_UPDATE   //!< Mod is installed but with different fileID
    };

    struct ModDownloadInfo
    {
        QString name;
        QString author;
        QString description;
        QString downloads;
        QString updateTime;
        QString category;
        QString logoUrl;
        QString logoFileName;
        QString fileFingerprint;
        int modId;
        int fileID;
        ModInstallStatus installStatus; // 添加按钮状态字段

        // 获取默认图标的辅助方法
        QIcon getDefaultIcon() const;
    };

    // UI组件创建方法
    static QWidget *createModItemWidget();
    static QFrame *createIconFrame();
    static QLabel *createIconLabel();
    static QLabel *createTitleLabel(const QString &text);
    static QLabel *createAuthorLabel(const QString &text);
    static QLabel *createDescriptionLabel(const QString &text);
    static QLabel *createStatsLabel(const QString &text);
    static QProgressBar *createProgressBar();
    static QPushButton *createInstallButton(const QString &text);
    static QLabel *createLoadingLabel(const QString &text);

    // 布局创建方法
    static QHBoxLayout *createMainLayout(QWidget *parent);
    static QVBoxLayout *createIconLayout(QFrame *parent);
    static QVBoxLayout *createContentLayout();
    static QHBoxLayout *createTitleLayout();
    static QHBoxLayout *createStatsLayout();
    static QVBoxLayout *createButtonLayout();
};

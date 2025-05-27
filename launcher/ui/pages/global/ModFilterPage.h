#pragma once

#include <QWidget>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QTableWidget>
#include <QPushButton>
#include <QMenu> // 新增：包含 QMenu 头文件

#include "ui/pages/BasePage.h"
#include <Application.h>

#include <QMainWindow>
namespace Ui
{
    class ModFilterPage;
}

class QNetworkReply;

/**
 * @brief Mod 黑名单/白名单管理页面
 *
 * 提供拖放文件添加 Mod 到黑名单或白名单的功能
 * 支持编辑和删除已添加的 Mod 条目
 */
class ModFilterPage : public QMainWindow, public BasePage
{
    Q_OBJECT

public:
    explicit ModFilterPage(QWidget *parent = nullptr);
    ~ModFilterPage() override;

    // BasePage 接口实现
    QString displayName() const override
    {
        return tr("Mod Lists");
    }

    QIcon icon() const override
    {
        return APPLICATION->getThemedIcon("coremods");
    }

    QString id() const override
    {
        return "mod-lists";
    }

    QString helpPage() const override
    {
        return "Mod-Lists";
    }
    bool apply() override;

protected:
    // 拖放事件处理
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    // 数据加载
    void loadBlacklist();
    void loadWhitelist();
    void refreshData();

    // 黑名单表格事件处理
    void onBlacklistCellDoubleClicked(int row, int column);
    void onBlacklistCellChanged(int row, int column);

    // 白名单表格事件处理
    void onWhitelistCellDoubleClicked(int row, int column);
    void onWhitelistCellChanged(int row, int column);

private:
    // UI 初始化和配置
    void setupUi();
    void setupTableWidget(QTableWidget *tableWidget);
    void connectSignals();

    // 数据操作
    void loadList(QTableWidget *tableWidget, const QMap<int, QString> &modList);
    void addModToTable(QTableWidget *tableWidget, int projectId, const QString &name);
    void removeSelectedMods(QTableWidget *tableWidget, bool isWhitelist);

    // 事件处理辅助函数
    void handleCellDoubleClick(QTableWidget *tableWidget, int row, int column);
    void handleCellChanged(QTableWidget *tableWidget, int row, int column, bool isWhitelist);

    // 网络请求处理
    void requestModInfo(const QStringList &fingerprints, bool isWhitelist);
    void processModInfoResponse(const QByteArray &responseData, bool isWhitelist);

    // 文件处理
    bool isSupportedFile(const QString &filePath) const;

    // 用户反馈
    void showErrorMessage(const QString &title, const QString &message);
    void showInfoMessage(const QString &title, const QString &message);

    // 提示标签可见性控制
    void updateDropHintVisibility();


private:
    Ui::ModFilterPage *ui;

    // 支持的文件扩展名
    static const QStringList SUPPORTED_EXTENSIONS;

    // 析构标志
    bool m_isClosing = false;
};

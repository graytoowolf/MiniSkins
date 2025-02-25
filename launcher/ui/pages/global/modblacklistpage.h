#pragma once
#include <QWidget>
#include "ui/pages/BasePage.h"
#include <Application.h>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>

namespace Ui
{
    class ModBlacklistPage;
}

class ModBlacklistPage : public QWidget, public BasePage
{
    Q_OBJECT

public:
    explicit ModBlacklistPage(QWidget *parent = 0);
    ~ModBlacklistPage();

    QString displayName() const override
    {
        return tr("Mod blacklist");
    }
    QIcon icon() const override
    {
        return APPLICATION->getThemedIcon("coremods");
    }
    QString id() const override
    {
        return "mod-blacklist";
    }
    QString helpPage() const override
    {
        return "Mod-Blacklist";
    }

private slots:
    void loadBlacklist();
    void onCellDoubleClicked(int row, int column);
    void onCellChanged(int row, int column);

    void on_removeButton_clicked();

private:
    void setupUi();
    void refreshData();
    void requestModInfo(const QStringList &fingerprints); // 修改参数类型为QStringList
    Ui::ModBlacklistPage *ui;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool m_isClosing = false;
    void prepareForDestruction();
    const QStringList m_supportedExtensions = {".jar", ".disabled"};
    bool isSupportedFile(const QString &filePath) const;
};

#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <QTextBrowser>
#include <QNetworkReply>

class QTreeWidget;
class QTreeWidgetItem;
class QDialogButtonBox;
class QNetworkAccessManager;

class ModpackUpdateVersionSelectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ModpackUpdateVersionSelectDialog(const QList<QJsonObject> &matchingFiles, const QString &addonId, QWidget *parent = nullptr);
    ~ModpackUpdateVersionSelectDialog() override;

    QJsonObject selectedFile() const;

private:
    void setupUI();
    void populateVersionList();
    QString formatFileSize(qint64 bytes) const;
    QString detectModLoader(const QJsonObject &fileObj) const;
    QString formatReleaseType(int releaseType) const;
    QColor releaseTypeColor(int releaseType) const;
    void fetchChangelog(int fileId);

private slots:
    void onCurrentItemChanged();
    void onChangelogReplyFinished(QNetworkReply *reply);

private:
    QList<QJsonObject> m_matchingFiles;
    QString m_addonId;
    QTreeWidget *m_treeWidget = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
    QTextBrowser *m_changelogEdit = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_changelogReply = nullptr;
    int m_currentFileId = 0;
};

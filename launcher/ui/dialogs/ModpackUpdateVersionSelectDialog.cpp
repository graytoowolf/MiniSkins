#include "ModpackUpdateVersionSelectDialog.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include "Application.h"

ModpackUpdateVersionSelectDialog::ModpackUpdateVersionSelectDialog(const QList<QJsonObject> &matchingFiles, const QString &addonId, QWidget *parent)
    : QDialog(parent), m_matchingFiles(matchingFiles), m_addonId(addonId)
{
    setWindowTitle(tr("Select Modpack Version"));
    resize(800, 500);
    m_network = new QNetworkAccessManager(this);
    setupUI();
    populateVersionList();
}

ModpackUpdateVersionSelectDialog::~ModpackUpdateVersionSelectDialog()
{
    if (m_changelogReply)
    {
        m_changelogReply->abort();
        m_changelogReply->deleteLater();
    }
}

void ModpackUpdateVersionSelectDialog::setupUI()
{
    auto *layout = new QVBoxLayout(this);

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabels(QStringList()
        << tr("Version Name")
        << tr("Minecraft")
        << tr("ModLoader")
        << tr("Release Type")
        << tr("File Size")
        << tr("Upload Date"));
    m_treeWidget->setRootIsDecorated(false);
    m_treeWidget->setAlternatingRowColors(true);
    m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeWidget->header()->setStretchLastSection(false);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);

    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(m_treeWidget, &QTreeWidget::currentItemChanged, this, &ModpackUpdateVersionSelectDialog::onCurrentItemChanged);

    layout->addWidget(m_treeWidget);

    m_changelogEdit = new QTextBrowser(this);
    m_changelogEdit->setMaximumHeight(150);
    m_changelogEdit->setOpenExternalLinks(true);
    m_changelogEdit->setPlaceholderText(tr("Select a version to view changelog"));
    layout->addWidget(m_changelogEdit);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttonBox);
}

void ModpackUpdateVersionSelectDialog::populateVersionList()
{
    for (int i = 0; i < m_matchingFiles.size(); ++i)
    {
        const QJsonObject &fileObj = m_matchingFiles[i];

        QString displayName = fileObj.value("displayName").toString();

        QJsonArray gameVersions = fileObj.value("sortableGameVersions").toArray();
        QStringList mcVersions;
        for (const QJsonValue &gv : gameVersions)
        {
            QJsonObject gvObj = gv.toObject();
            QString ver = gvObj.value("gameVersion").toString();
            if (!ver.isEmpty())
            {
                mcVersions.append(ver);
            }
        }

        QString modLoader = detectModLoader(fileObj);
        int releaseType = fileObj.value("releaseType").toInt();
        QString releaseTypeStr = formatReleaseType(releaseType);
        QColor releaseColor = releaseTypeColor(releaseType);

        qint64 fileSize = fileObj.value("fileLength").toVariant().toLongLong();
        QString fileSizeStr = formatFileSize(fileSize);

        QString fileDateStr = fileObj.value("fileDate").toString();
        QString uploadDate;
        if (!fileDateStr.isEmpty())
        {
            QDateTime dt = QDateTime::fromString(fileDateStr, Qt::ISODate);
            if (dt.isValid())
            {
                uploadDate = dt.toString(Qt::DefaultLocaleShortDate);
            }
            else
            {
                uploadDate = fileDateStr;
            }
        }

        auto *item = new QTreeWidgetItem(QStringList()
            << displayName
            << mcVersions.join(", ")
            << modLoader
            << releaseTypeStr
            << fileSizeStr
            << uploadDate);

        item->setData(0, Qt::UserRole, i);
        item->setForeground(3, releaseColor);

        m_treeWidget->addTopLevelItem(item);
    }

    if (!m_matchingFiles.isEmpty())
    {
        m_treeWidget->setCurrentItem(m_treeWidget->topLevelItem(0));
    }
}

QJsonObject ModpackUpdateVersionSelectDialog::selectedFile() const
{
    QTreeWidgetItem *current = m_treeWidget->currentItem();
    if (!current)
    {
        return QJsonObject();
    }
    int index = current->data(0, Qt::UserRole).toInt();
    if (index >= 0 && index < m_matchingFiles.size())
    {
        return m_matchingFiles[index];
    }
    return QJsonObject();
}

QString ModpackUpdateVersionSelectDialog::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024)
    {
        return QString("%1 B").arg(bytes);
    }
    else if (bytes < 1024 * 1024)
    {
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    else if (bytes < 1024 * 1024 * 1024)
    {
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }
    else
    {
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
}

QString ModpackUpdateVersionSelectDialog::detectModLoader(const QJsonObject &fileObj) const
{
    QJsonArray gameVersions = fileObj.value("sortableGameVersions").toArray();
    for (const QJsonValue &gv : gameVersions)
    {
        QJsonObject gvObj = gv.toObject();
        if (gvObj.value("gameVersion").toString().isEmpty())
        {
            QString loaderName = gvObj.value("gameVersionName").toString().toLower();
            if (loaderName.contains("neoforge"))
            {
                return "NeoForge";
            }
            else if (loaderName.contains("forge"))
            {
                return "Forge";
            }
            else if (loaderName.contains("fabric"))
            {
                return "Fabric";
            }
            else if (loaderName.contains("quilt"))
            {
                return "Quilt";
            }
            else if (!loaderName.isEmpty())
            {
                return gvObj.value("gameVersionName").toString();
            }
        }
    }

    QString fileName = fileObj.value("fileName").toString().toLower();
    if (fileName.contains("neoforge"))
    {
        return "NeoForge";
    }
    else if (fileName.contains("forge") && !fileName.contains("neoforge"))
    {
        return "Forge";
    }
    else if (fileName.contains("fabric"))
    {
        return "Fabric";
    }
    else if (fileName.contains("quilt"))
    {
        return "Quilt";
    }

    return tr("Unknown");
}

QString ModpackUpdateVersionSelectDialog::formatReleaseType(int releaseType) const
{
    switch (releaseType)
    {
    case 1:
        return tr("Release");
    case 2:
        return tr("Beta");
    case 3:
        return tr("Alpha");
    default:
        return tr("Unknown");
    }
}

QColor ModpackUpdateVersionSelectDialog::releaseTypeColor(int releaseType) const
{
    switch (releaseType)
    {
    case 1:
        return QColor(0, 150, 0);
    case 2:
        return QColor(255, 165, 0);
    case 3:
        return QColor(220, 50, 50);
    default:
        return QColor(128, 128, 128);
    }
}

void ModpackUpdateVersionSelectDialog::onCurrentItemChanged()
{
    if (!m_changelogEdit) return;

    QTreeWidgetItem *current = m_treeWidget->currentItem();
    if (!current)
    {
        m_changelogEdit->clear();
        return;
    }

    int index = current->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= m_matchingFiles.size())
    {
        m_changelogEdit->clear();
        return;
    }

    const QJsonObject &fileObj = m_matchingFiles[index];
    int fileId = fileObj.value("id").toInt();
    fetchChangelog(fileId);
}

void ModpackUpdateVersionSelectDialog::fetchChangelog(int fileId)
{
    if (m_currentFileId == fileId && !m_changelogEdit->toPlainText().isEmpty())
    {
        return;
    }

    m_currentFileId = fileId;
    m_changelogEdit->setHtml(tr("<i>Loading changelog...</i>"));

    if (m_changelogReply)
    {
        m_changelogReply->abort();
        m_changelogReply->deleteLater();
        m_changelogReply = nullptr;
    }

    QUrl url(QString("https://api.curseforge.com/v1/mods/%1/files/%2/changelog").arg(m_addonId).arg(fileId));
    QNetworkRequest request(url);
    request.setRawHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
    m_changelogReply = m_network->get(request);
    connect(m_changelogReply, &QNetworkReply::finished, this, [this]() {
        onChangelogReplyFinished(m_changelogReply);
    });
}

void ModpackUpdateVersionSelectDialog::onChangelogReplyFinished(QNetworkReply *reply)
{
    if (!reply || reply != m_changelogReply)
    {
        return;
    }

    m_changelogReply = nullptr;

    if (reply->error() != QNetworkReply::NoError)
    {
        m_changelogEdit->setHtml(tr("<i>Failed to load changelog.</i>"));
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        m_changelogEdit->setHtml(tr("<i>Failed to parse changelog.</i>"));
        return;
    }

    QJsonObject root = doc.object();
    QString html = root.value("data").toString();
    if (html.isEmpty())
    {
        m_changelogEdit->setHtml(tr("<i>No changelog available.</i>"));
    }
    else
    {
        m_changelogEdit->setHtml(html);
    }
}

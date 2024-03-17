#include "CFModel.h"
#include "Application.h"
#include <Json.h>

#include <MMCStrings.h>
#include <Version.h>

#include <QtMath>
#include <QLabel>

#include <RWStorage.h>

namespace CurseForge {

ListModel::ListModel(QObject *parent) : QAbstractListModel(parent)
{
}

ListModel::~ListModel()
{
}

int ListModel::rowCount(const QModelIndex &parent) const
{
    return modpacks.size();
}

int ListModel::columnCount(const QModelIndex &parent) const
{
    return 1;
}

QVariant ListModel::data(const QModelIndex &index, int role) const
{
    int pos = index.row();
    if(pos >= modpacks.size() || pos < 0 || !index.isValid())
    {
        return QString("INVALID INDEX %1").arg(pos);
    }

    IndexedPack pack = modpacks.at(pos);
    if(role == Qt::DisplayRole)
    {
        return pack.name;
    }
    else if (role == Qt::ToolTipRole)
    {
        if(pack.summary.length() > 100)
        {
            //some magic to prevent to long tooltips and replace html linebreaks
            QString edit = pack.summary.left(97);
            edit = edit.left(edit.lastIndexOf("<br>")).left(edit.lastIndexOf(" ")).append("...");
            return edit;

        }
        return pack.summary;
    }
    else if(role == Qt::DecorationRole)
    {
        if(m_logoMap.contains(pack.logoName))
        {
            return (m_logoMap.value(pack.logoName));
        }
        QIcon icon = APPLICATION->getThemedIcon("screenshot-placeholder");
        ((ListModel *)this)->requestLogo(pack.logoName, pack.logoUrl);
        return icon;
    }
    else if(role == Qt::UserRole)
    {
        QVariant v;
        v.setValue(pack);
        return v;
    }

    return QVariant();
}

void ListModel::logoLoaded(QString logo, QIcon out)
{
    m_loadingLogos.removeAll(logo);
    m_logoMap.insert(logo, out);
    for(int i = 0; i < modpacks.size(); i++) {
        if(modpacks[i].logoName == logo) {
            emit dataChanged(createIndex(i, 0), createIndex(i, 0), {Qt::DecorationRole});
        }
    }
}

void ListModel::logoFailed(QString logo)
{
    m_failedLogos.append(logo);
    m_loadingLogos.removeAll(logo);
}

void ListModel::requestLogo(QString logo, QString url)
{
    if(m_loadingLogos.contains(logo) || m_failedLogos.contains(logo))
    {
        return;
    }

    MetaEntryPtr entry = APPLICATION->metacache()->resolveEntry("CurseForgePacks", QString("logos/%1").arg(logo.section(".", 0, 0)));
    NetJob *job = new NetJob(QString("CurseForge Icon Download %1").arg(logo), APPLICATION->network());
    job->addNetAction(Net::Download::makeCached(QUrl(url), entry));

    auto fullPath = entry->getFullPath();
    QObject::connect(job, &NetJob::succeeded, this, [this, logo, fullPath]
    {
        emit logoLoaded(logo, QIcon(fullPath));
        if(waitingCallbacks.contains(logo))
        {
            waitingCallbacks.value(logo)(fullPath);
        }
    });

    QObject::connect(job, &NetJob::failed, this, [this, logo]
    {
        emit logoFailed(logo);
    });

    job->start();

    m_loadingLogos.append(logo);
}

void ListModel::getLogo(const QString &logo, const QString &logoUrl, LogoCallback callback)
{
    if(m_logoMap.contains(logo))
    {
        callback(APPLICATION->metacache()->resolveEntry("CurseForgePacks", QString("logos/%1").arg(logo.section(".", 0, 0)))->getFullPath());
    }
    else
    {
        requestLogo(logo, logoUrl);
    }
}

Qt::ItemFlags ListModel::flags(const QModelIndex &index) const
{
    return QAbstractListModel::flags(index);
}

bool ListModel::canFetchMore(const QModelIndex& parent) const
{
    return searchState == CanPossiblyFetchMore;
}

void ListModel::fetchMore(const QModelIndex& parent)
{
    if (parent.isValid())
        return;
    if(nextSearchOffset == 0) {
        qWarning() << "fetchMore with 0 offset is wrong...";
        return;
    }
    performPaginatedSearch();
}

void ListModel::performPaginatedSearch()
{
    NetJob *netJob = new NetJob("CurseForge::Search", APPLICATION->network());
    QString textSortOrder;
    switch (currentSortOrder) {
        case SortOrder::Ascending: {
            textSortOrder = "asc";
            break;
        }
        case SortOrder::Descending: {
            textSortOrder = "desc";
            break;
        }
    }
    auto searchUrl = QString(
        "https://api.curseforge.com/v1/mods/search?"
        "gameId=432&"
        "classId=4471&"
        "index=%1&"
        "pageSize=25&"
        "searchFilter=%2&"
        "sortField=%3&"
        "sortOrder=%4"
    ).arg(nextSearchOffset).arg(currentSearchTerm).arg(currentSort + 1).arg(textSortOrder);
    auto download = Net::Download::makeByteArray(QUrl(searchUrl), &response);
    download->setExtraHeader("x-api-key", APPLICATION->curseAPIKey());
    netJob->addNetAction(download);
    jobPtr = netJob;
    jobPtr->start();
    QObject::connect(netJob, &NetJob::succeeded, this, &ListModel::searchRequestFinished);
    QObject::connect(netJob, &NetJob::failed, this, &ListModel::searchRequestFailed);
}

void ListModel::searchWithTerm(const QString& term, int sort, SortOrder sortOrder)
{
    if(currentSearchTerm == term && currentSearchTerm.isNull() == term.isNull() && currentSort == sort) {
        return;
    }
    currentSearchTerm = term.toUtf8().toPercentEncoding();
    currentSearchTerm = currentSearchTerm.toUtf8().toPercentEncoding();
    currentSort = sort;
    currentSortOrder = sortOrder;
    if(jobPtr) {
        jobPtr->abort();
        searchState = ResetRequested;
        return;
    }
    else {
        beginResetModel();
        modpacks.clear();
        endResetModel();
        searchState = None;
    }
    nextSearchOffset = 0;
    performPaginatedSearch();
}

void CurseForge::ListModel::searchRequestFinished()
{
    jobPtr.reset();

    QJsonParseError parse_error;
#ifndef NDEBUG
    qDebug() << response;
#endif
    QJsonDocument doc = QJsonDocument::fromJson(response, &parse_error);
    if(parse_error.error != QJsonParseError::NoError) {
        qWarning() << "Error while parsing JSON response from CurseForge at " << parse_error.offset << " reason: " << parse_error.errorString();
        qWarning() << response;
        return;
    }

    QList<CurseForge::IndexedPack> newList;
    auto packs = Json::ensureArray(doc.object(), "data");
    for(auto packRaw : packs) {
        auto packObj = packRaw.toObject();

        CurseForge::IndexedPack pack;
        try
        {
            CurseForge::loadIndexedPack(pack, packObj);
            newList.append(pack);
        }
        catch(const JSONValidationError &e)
        {
            qWarning() << "Error while loading pack from CurseForge: " << e.cause();
            continue;
        }
    }
    if(packs.size() < 25) {
        searchState = Finished;
    } else {
        nextSearchOffset += 25;
        searchState = CanPossiblyFetchMore;
    }
    beginInsertRows(QModelIndex(), modpacks.size(), modpacks.size() + newList.size() - 1);
    modpacks.append(newList);
    endInsertRows();
}

void CurseForge::ListModel::searchRequestFailed(QString reason)
{
    jobPtr.reset();

    if(searchState == ResetRequested) {
        beginResetModel();
        modpacks.clear();
        endResetModel();

        nextSearchOffset = 0;
        performPaginatedSearch();
    } else {
        searchState = Finished;
    }
}

}

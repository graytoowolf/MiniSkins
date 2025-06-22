/* Copyright 2013-2021 MiniSkins Contributors
 *
 * Authors: Orochimarufan <orochimarufan.x3@gmail.com>
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

#include "MinecraftAccount.h"

#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegExp>
#include <QStringList>
#include <QJsonDocument>

#include <QDebug>

#include <QPainter>

#include "flows/MSA.h"
#include "flows/Bs.h"

MinecraftAccount::MinecraftAccount(QObject *parent) : QObject(parent)
{
    data.internalId = QUuid::createUuid().toString().remove(QRegExp("[{}-]"));
}

MinecraftAccountPtr MinecraftAccount::loadFromJsonV4(const QJsonObject &json)
{
    MinecraftAccountPtr account(new MinecraftAccount());
    if (account->data.resumeStateFromV4(json))
    {
        return account;
    }
    return nullptr;
}

MinecraftAccountPtr MinecraftAccount::loadFromJsonV3(const QJsonObject &json)
{
    MinecraftAccountPtr account(new MinecraftAccount());
    if (account->data.resumeStateFromV3(json))
    {
        return account;
    }
    return nullptr;
}

MinecraftAccountPtr MinecraftAccount::createBlessings(const QString &username, const QString &yggurl, const QString &yggname)
{
    MinecraftAccountPtr account = new MinecraftAccount();
    account->data.type = AccountType::Bs;
    account->data.yggdrasilToken.extra["userName"] = username;
    account->data.yggdrasilToken.yggname = yggname;
    account->data.yggdrasilToken.yggurl = yggurl.endsWith('/') ? yggurl : yggurl + '/';
    account->data.yggdrasilToken.extra["clientToken"] = QUuid::createUuid().toString().remove(QRegExp("[{}-]"));
    return account;
}

MinecraftAccountPtr MinecraftAccount::createBlankMSA()
{
    MinecraftAccountPtr account(new MinecraftAccount());
    account->data.type = AccountType::MSA;
    return account;
}

QJsonObject MinecraftAccount::saveToJson() const
{
    return data.saveState();
}

AccountState MinecraftAccount::accountState() const
{
    return data.accountState;
}

QPixmap MinecraftAccount::getFace() const
{
    QPixmap skinTexture;
    if (!skinTexture.loadFromData(data.minecraftProfile.skin.data, "PNG"))
    {
        return QPixmap();
    }
    QPixmap skin = QPixmap(8, 8);
    QPainter painter(&skin);
    painter.drawPixmap(0, 0, skinTexture.copy(8, 8, 8, 8));
    painter.drawPixmap(0, 0, skinTexture.copy(40, 8, 8, 8));
    return skin.scaled(64, 64, Qt::KeepAspectRatio);
}

shared_qobject_ptr<AccountTask> MinecraftAccount::bslogin(QString password)
{
    Q_ASSERT(m_currentTask.get() == nullptr);

    m_currentTask.reset(new BsLogin(&data, password));
    connect(m_currentTask.get(), SIGNAL(succeeded()), SLOT(authSucceeded()));
    connect(m_currentTask.get(), SIGNAL(failed(QString)), SLOT(authFailed(QString)));
    emit activityChanged(true);
    return m_currentTask;
}

shared_qobject_ptr<AccountTask> MinecraftAccount::loginMSA()
{
    Q_ASSERT(m_currentTask.get() == nullptr);

    m_currentTask.reset(new MSAInteractive(&data));
    connect(m_currentTask.get(), SIGNAL(succeeded()), SLOT(authSucceeded()));
    connect(m_currentTask.get(), SIGNAL(failed(QString)), SLOT(authFailed(QString)));
    emit activityChanged(true);
    return m_currentTask;
}

shared_qobject_ptr<AccountTask> MinecraftAccount::refresh()
{
    if (m_currentTask)
    {
        return m_currentTask;
    }

    if (data.type == AccountType::MSA)
    {
        m_currentTask.reset(new MSASilent(&data));
    }
    else if (data.type == AccountType::Bs)
    {
        m_currentTask.reset(new BsRefresh(&data));
    }

    connect(m_currentTask.get(), SIGNAL(succeeded()), SLOT(authSucceeded()));
    connect(m_currentTask.get(), SIGNAL(failed(QString)), SLOT(authFailed(QString)));
    emit activityChanged(true);
    return m_currentTask;
}

shared_qobject_ptr<AccountTask> MinecraftAccount::currentTask()
{
    return m_currentTask;
}

void MinecraftAccount::authSucceeded()
{
    m_currentTask.reset();
    emit changed();
    emit activityChanged(false);
}

void MinecraftAccount::authFailed(QString reason)
{
    switch (m_currentTask->taskState())
    {
    case AccountTaskState::STATE_OFFLINE:
    case AccountTaskState::STATE_FAILED_MUST_MIGRATE:
    case AccountTaskState::STATE_FAILED_SOFT:
    {
        // NOTE: this doesn't do much. There was an error of some sort.
    }
    break;
    case AccountTaskState::STATE_FAILED_HARD:
    {
        if (isMSA())
        {
            data.msaToken.token = QString();
            data.msaToken.refresh_token = QString();
            data.msaToken.validity = Katabasis::Validity::None;
        }
        else
        {
            data.yggdrasilToken.token = QString();
            data.yggdrasilToken.validity = Katabasis::Validity::None;
        }
        data.validity_ = Katabasis::Validity::None;
        emit changed();
    }
    break;
    case AccountTaskState::STATE_FAILED_GONE:
    {
        data.validity_ = Katabasis::Validity::None;
        emit changed();
    }
    break;
    case AccountTaskState::STATE_CREATED:
    case AccountTaskState::STATE_WORKING:
    case AccountTaskState::STATE_SUCCEEDED:
    {
        // Not reachable here, as they are not failures.
    }
    }
    m_currentTask.reset();
    emit activityChanged(false);
}

bool MinecraftAccount::isActive() const
{
    return m_currentTask;
}

bool MinecraftAccount::shouldRefresh() const
{
    /*
     * Never refresh accounts that are being used by the game, it breaks the game session.
     * Always refresh accounts that have not been refreshed yet during this session.
     * Don't refresh broken accounts.
     * Refresh accounts that would expire in the next 12 hours (fresh token validity is 24 hours).
     */
    if (isInUse())
    {
        return false;
    }
    switch (data.validity_)
    {
    case Katabasis::Validity::Certain:
    {
        break;
    }
    case Katabasis::Validity::None:
    {
        return false;
    }
    case Katabasis::Validity::Assumed:
    {
        return true;
    }
    }
    auto now = QDateTime::currentDateTimeUtc();
    auto issuedTimestamp = data.yggdrasilToken.issueInstant;
    auto expiresTimestamp = data.yggdrasilToken.notAfter;

    if (!expiresTimestamp.isValid())
    {
        expiresTimestamp = issuedTimestamp.addSecs(24 * 3600);
    }
    if (now.secsTo(expiresTimestamp) < (12 * 3600))
    {
        return true;
    }
    return false;
}

void MinecraftAccount::fillSession(AuthSessionPtr session)
{
    if (ownsMinecraft() && !hasProfile())
    {
        session->status = AuthSession::RequiresProfileSetup;
    }
    else
    {
        if (session->wants_online)
        {
            session->status = AuthSession::PlayableOnline;
        }
        else
        {
            session->status = AuthSession::PlayableOffline;
        }
    }

    // NOTE: removed because of MSA
    session->username = "";
    // volatile auth token
    session->access_token = data.accessToken();
    // NOTE: removed because of MSA
    session->client_token = "";
    // profile name
    session->player_name = data.profileName();
    // profile ID
    session->uuid = data.profileId();
    // 'legacy' or 'mojang', depending on account type
    session->user_type = typeString();
    // yggurl
    session->yggurl = data.yggurl();

    if (!session->access_token.isEmpty())
    {
        session->session = "token:" + data.accessToken() + ":" + data.profileId();
    }
    else
    {
        session->session = "-";
    }
}

void MinecraftAccount::decrementUses()
{
    Usable::decrementUses();
    if (!isInUse())
    {
        emit changed();
        // FIXME: we now need a better way to identify accounts...
        qWarning() << "Profile" << data.profileId() << "is no longer in use.";
    }
}

void MinecraftAccount::incrementUses()
{
    bool wasInUse = isInUse();
    Usable::incrementUses();
    if (!wasInUse)
    {
        emit changed();
        // FIXME: we now need a better way to identify accounts...
        qWarning() << "Profile" << data.profileId() << "is now in use.";
    }
}

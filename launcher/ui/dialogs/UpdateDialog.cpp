#include "UpdateDialog.h"
#include "ui_UpdateDialog.h"
#include <QDebug>
#include "Application.h"
#include <settings/SettingsObject.h>
#include <Json.h>

#include "BuildConfig.h"

UpdateDialog::UpdateDialog(bool hasUpdate, QWidget *parent) : QDialog(parent), ui(new Ui::UpdateDialog)
{
    ui->setupUi(this);
    if(hasUpdate)
    {
        ui->label->setText(tr("A new update is available!"));
    }
    else
    {
        ui->label->setText(tr("No updates found. You are running the latest version."));
        ui->btnUpdateNow->setHidden(true);
        ui->btnUpdateLater->setText(tr("Close"));
    }
    ui->changelogBrowser->setHtml(tr("<center><h1>Loading changelog...</h1></center>"));
    loadChangelog();
    restoreGeometry(QByteArray::fromBase64(APPLICATION->settings()->get("UpdateDialogGeometry").toByteArray()));
}

UpdateDialog::~UpdateDialog()
{
}

void UpdateDialog::loadChangelog()
{
    dljob = new NetJob("Changelog", APPLICATION->network());
    QString url = BuildConfig.UPDATER_BASE + "update/commits.json";
    dljob->addNetAction(Net::Download::makeByteArray(QUrl(url), &changelogData));
    connect(dljob.get(), &NetJob::succeeded, this, &UpdateDialog::changelogLoaded);
    connect(dljob.get(), &NetJob::failed, this, &UpdateDialog::changelogFailed);
    dljob->start();
}

QString reprocessCommits(QByteArray json)
{
    try
    {
        auto document = Json::requireDocument(json);
        auto rootobject = Json::requireObject(document);
        auto commitarray = Json::requireArray(rootobject, "commits");

        bool foundCurrent = false;
        QString result;
        result += "<table cellspacing=0 cellpadding=2 style='border-width: 1px; border-style: solid'>";

        for(int i = 0; i < commitarray.size(); i++)
        {
            const auto & commitval = commitarray[i];
            auto commitobj = Json::requireValueObject(commitval);
            auto sha = Json::requireString(commitobj, "sha");
            auto shortSha = Json::requireString(commitobj, "shortSha");
            auto message = Json::requireString(commitobj, "message");
            auto url = Json::requireString(commitobj, "url");

            if(sha == BuildConfig.GIT_COMMIT)
            {
                foundCurrent = true;
                break;
            }

            auto lines = message.split('\n');
            result += "<tr><td>";
            result += QString("<a href=\"%1\">%2</a>").arg(url, shortSha);
            result += "</td>";
            result += "<td><p>" + lines.join("<br />") + "</p></td></tr>";
        }

        result += "</table>";

        if(foundCurrent)
        {
            result = QObject::tr("<p>Following commits were added since last update:</p>") + result;
        }
        else
        {
            result = QObject::tr("<p>Your version is too old, the changelog may be incomplete. Showing the latest %1 commits:</p>").arg(commitarray.size()) + result;
        }

        auto repo = Json::ensureString(rootobject, "repo");
        if(!repo.isEmpty())
        {
            result += QObject::tr("<p>You can <a href=\"%1\">look at the changes on github</a>.</p>").arg(repo);
        }

        return result;
    }
    catch (const JSONValidationError &e)
    {
        qWarning() << "Got an unparseable commit log:" << e.what();
        qDebug() << json;
    }
    return QString();
}

void UpdateDialog::changelogLoaded()
{
    QString result = reprocessCommits(changelogData);
    changelogData.clear();
    ui->changelogBrowser->setHtml(result);
}

void UpdateDialog::changelogFailed(QString reason)
{
    ui->changelogBrowser->setHtml(tr("<p align=\"center\" <span style=\"font-size:22pt;\">Failed to fetch changelog... Error: %1</span></p>").arg(reason));
}

void UpdateDialog::on_btnUpdateLater_clicked()
{
    reject();
}

void UpdateDialog::on_btnUpdateNow_clicked()
{
    done(UPDATE_NOW);
}

void UpdateDialog::closeEvent(QCloseEvent* evt)
{
    APPLICATION->settings()->set("UpdateDialogGeometry", saveGeometry().toBase64());
    QDialog::closeEvent(evt);
}

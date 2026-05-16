#include "AIModelPage.h"
#include "ui_AIModelPage.h"

#include <QMessageBox>
#include <QTabBar>
#include <QUrl>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalBlocker>

#include "settings/SettingsObject.h"
#include "Application.h"

static const QString SPINNER_CHARS = QString::fromUtf8("⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏");
const QString AIModelPage::CUSTOM_MODEL_TEXT = QT_TRANSLATE_NOOP("AIModelPage", "Custom Model...");

AIModelPage::AIModelPage(QWidget *parent)
    : QWidget(parent), ui(new Ui::AIModelPage), m_currentIndex(-1), m_testReply(nullptr),
      m_spinnerTimer(nullptr), m_spinnerIndex(0), m_modelsReply(nullptr), m_loadingUi(false)
{
    ui->setupUi(this);
    ui->tabWidget->tabBar()->hide();

    m_spinnerTimer = new QTimer(this);
    connect(m_spinnerTimer, &QTimer::timeout, this, &AIModelPage::updateSpinnerAnimation);

    ui->labelCustomModel->hide();
    ui->editCustomModelId->hide();

    loadSettings();
}

AIModelPage::~AIModelPage()
{
    if (m_spinnerTimer)
    {
        m_spinnerTimer->stop();
    }
    if (m_testReply)
    {
        m_testReply->abort();
        m_testReply->deleteLater();
    }
    delete ui;
}

void AIModelPage::loadSettings()
{
    m_models = AIAnalyzer::loadModels();
    populateModelList();
}

void AIModelPage::applySettings()
{
    saveCurrentModelDetail();
    AIAnalyzer::saveModels(m_models);

    QString defaultModel = ui->comboDefaultModel->currentData().toString();
    if (!defaultModel.isEmpty())
    {
        AIAnalyzer::setDefaultModel(defaultModel);
    }
}

bool AIModelPage::apply()
{
    applySettings();
    return true;
}

void AIModelPage::populateModelList()
{
    QSignalBlocker listBlocker(ui->modelListWidget);
    m_loadingUi = true;
    ui->modelListWidget->clear();
    for (const AIAnalyzer::ModelConfig &cfg : m_models)
    {
        QString displayText = cfg.modelId.isEmpty() ? tr("New Model") : cfg.modelId;
        ui->modelListWidget->addItem(displayText);
    }

    if (!m_models.isEmpty())
    {
        m_currentIndex = 0;
        ui->modelListWidget->setCurrentRow(0);
        updateDetailFields();
    }
    else
    {
        m_currentIndex = -1;
        ui->groupBoxDetails->setEnabled(false);
    }

    m_loadingUi = false;
    populateDefaultModelCombo();
}

void AIModelPage::updateDetailFields()
{
    QSignalBlocker modelComboBlocker(ui->comboModelId);
    m_loadingUi = true;
    if (m_currentIndex < 0 || m_currentIndex >= m_models.size())
    {
        ui->editApiUrl->clear();
        ui->editApiKey->clear();
        ui->comboModelId->clear();
        ui->editCustomModelId->clear();
        ui->labelCustomModel->hide();
        ui->editCustomModelId->hide();
        ui->groupBoxDetails->setEnabled(false);
        m_loadingUi = false;
        return;
    }

    ui->groupBoxDetails->setEnabled(true);
    const AIAnalyzer::ModelConfig &cfg = m_models[m_currentIndex];
    ui->editApiUrl->setText(cfg.apiUrl);
    ui->editApiKey->setText(cfg.apiKey);

    QStringList models;
    models << cfg.modelId;
    updateModelComboBox(models);
    setCurrentModelId(cfg.modelId);
    m_loadingUi = false;
}

void AIModelPage::saveCurrentModelDetail()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_models.size())
        return;

    m_models[m_currentIndex].apiUrl = ui->editApiUrl->text();
    m_models[m_currentIndex].apiKey = ui->editApiKey->text();
    m_models[m_currentIndex].modelId = getCurrentModelId();

    if (ui->modelListWidget->item(m_currentIndex))
    {
        QString displayText = m_models[m_currentIndex].modelId.isEmpty() 
            ? tr("New Model") 
            : m_models[m_currentIndex].modelId;
        ui->modelListWidget->item(m_currentIndex)->setText(displayText);
    }
}

void AIModelPage::populateDefaultModelCombo()
{
    QSignalBlocker defaultComboBlocker(ui->comboDefaultModel);
    QString currentSelection = ui->comboDefaultModel->currentData().toString();
    ui->comboDefaultModel->clear();

    QString defaultModelId = APPLICATION->settings()->get("AIDefaultModel").toString();

    for (const AIAnalyzer::ModelConfig &cfg : m_models)
    {
        if (!cfg.modelId.isEmpty())
        {
            ui->comboDefaultModel->addItem(cfg.modelId, cfg.modelId);
        }
    }

    int idx = ui->comboDefaultModel->findData(currentSelection);
    if (idx < 0)
    {
        idx = ui->comboDefaultModel->findData(defaultModelId);
    }
    if (idx < 0 && ui->comboDefaultModel->count() > 0)
    {
        idx = 0;
    }

    if (idx >= 0)
    {
        ui->comboDefaultModel->setCurrentIndex(idx);
    }
}

void AIModelPage::on_btnAddModel_clicked()
{
    AIAnalyzer::ModelConfig cfg;
    cfg.apiUrl = "";
    cfg.apiKey = "";
    cfg.modelId = "";

    m_models.append(cfg);
    ui->modelListWidget->addItem(tr("New Model"));
    ui->modelListWidget->setCurrentRow(m_models.size() - 1);
    populateDefaultModelCombo();
}

void AIModelPage::on_btnRemoveModel_clicked()
{
    int row = ui->modelListWidget->currentRow();
    if (row < 0 || row >= m_models.size())
        return;

    m_models.removeAt(row);

    m_loadingUi = true;
    delete ui->modelListWidget->takeItem(row);
    m_loadingUi = false;

    if (m_models.isEmpty())
    {
        m_currentIndex = -1;
        ui->groupBoxDetails->setEnabled(false);
    }
    else
    {
        if (row >= m_models.size())
            row = m_models.size() - 1;
        m_currentIndex = row;
        ui->modelListWidget->setCurrentRow(row);
        updateDetailFields();
    }

    populateDefaultModelCombo();
}

void AIModelPage::on_modelListWidget_currentRowChanged(int currentRow)
{
    if (m_loadingUi)
        return;

    saveCurrentModelDetail();
    m_currentIndex = currentRow;
    updateDetailFields();
}

void AIModelPage::updateSpinnerAnimation()
{
    m_spinnerIndex = (m_spinnerIndex + 1) % SPINNER_CHARS.length();
    QChar spinnerChar = SPINNER_CHARS[m_spinnerIndex];
    ui->btnTestConnection->setText(QString("%1 %2").arg(spinnerChar, tr("Testing")));
}

void AIModelPage::on_btnTestConnection_clicked()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_models.size())
        return;

    saveCurrentModelDetail();

    const AIAnalyzer::ModelConfig &cfg = m_models[m_currentIndex];
    if (cfg.apiUrl.isEmpty() || cfg.apiKey.isEmpty())
    {
        QMessageBox::warning(this, tr("Test Connection"),
                             tr("Please fill in API URL and API Key before testing."));
        return;
    }

    if (m_testReply)
    {
        m_testReply->abort();
        m_testReply->deleteLater();
        m_testReply = nullptr;
    }

    ui->btnTestConnection->setEnabled(false);
    m_spinnerIndex = 0;
    updateSpinnerAnimation();
    m_spinnerTimer->start(80);

    QString modelsUrl;
    if (cfg.apiUrl.contains("/v4/"))
    {
        modelsUrl = cfg.apiUrl.left(cfg.apiUrl.indexOf("/v4/") + 4) + "/models";
    }
    else if (cfg.apiUrl.contains("/v1/"))
    {
        int v1Index = cfg.apiUrl.indexOf("/v1/");
        modelsUrl = cfg.apiUrl.left(v1Index + 4) + "models";
    }
    else
    {
        QUrl url(cfg.apiUrl);
        QString baseUrl = QString("%1://%2").arg(url.scheme(), url.host());
        if (url.port() != -1)
        {
            baseUrl += QString(":%1").arg(url.port());
        }
        modelsUrl = baseUrl + "/v1/models";
    }

    QUrl url(modelsUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(cfg.apiKey).toUtf8());

    m_testReply = APPLICATION->network()->get(request);
    connect(m_testReply, &QNetworkReply::finished, this, &AIModelPage::onTestConnectionFinished);
}

void AIModelPage::onTestConnectionFinished()
{
    if (!m_testReply)
        return;

    m_spinnerTimer->stop();
    ui->btnTestConnection->setEnabled(true);
    ui->btnTestConnection->setText(tr("Test Connection"));

    QNetworkReply *reply = m_testReply;
    m_testReply = nullptr;
    reply->deleteLater();

    if (reply->error() == QNetworkReply::OperationCanceledError)
    {
        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        QMessageBox::critical(this, tr("Test Connection"), tr("Connection failed: %1").arg(reply->errorString()));
        return;
    }

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (statusCode == 401 || statusCode == 403)
    {
        QMessageBox::critical(this, tr("Test Connection"), tr("Authentication failed: Invalid API Key."));
        return;
    }

    if (statusCode >= 400)
    {
        QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        QString errorMsg = tr("Connection failed (HTTP %1)").arg(statusCode);
        if (!reason.isEmpty())
        {
            errorMsg += " - " + reason;
        }
        QMessageBox::critical(this, tr("Test Connection"), errorMsg);
        return;
    }

    QMessageBox::information(this, tr("Test Connection"), tr("Connection successful! API URL and Key are valid."));
}

void AIModelPage::on_editApiUrl_textEdited(const QString &text)
{
    Q_UNUSED(text);
    saveCurrentModelDetail();
}

void AIModelPage::on_editApiKey_textEdited(const QString &text)
{
    Q_UNUSED(text);
    saveCurrentModelDetail();
}

void AIModelPage::updateModelComboBox(const QStringList &models)
{
    QSignalBlocker blocker(ui->comboModelId);
    QString currentModel = getCurrentModelId();
    ui->comboModelId->clear();

    for (const QString &model : models)
    {
        QString displayText = model.isEmpty() ? tr("Enter model ID") : model;
        ui->comboModelId->addItem(displayText, model);
    }

    ui->comboModelId->addItem(tr(CUSTOM_MODEL_TEXT.toUtf8().constData()), CUSTOM_MODEL_TEXT);

    if (!currentModel.isEmpty())
    {
        int idx = ui->comboModelId->findData(currentModel);
        if (idx >= 0)
        {
            ui->comboModelId->setCurrentIndex(idx);
        }
    }
}

QString AIModelPage::getCurrentModelId()
{
    QString selectedData = ui->comboModelId->currentData().toString();

    if (selectedData == CUSTOM_MODEL_TEXT || ui->comboModelId->currentIndex() == ui->comboModelId->count() - 1)
    {
        return ui->editCustomModelId->text().trimmed();
    }

    return selectedData;
}

void AIModelPage::setCurrentModelId(const QString &modelId)
{
    if (modelId.isEmpty())
        return;

    int idx = ui->comboModelId->findData(modelId);
    if (idx >= 0)
    {
        ui->comboModelId->setCurrentIndex(idx);
        ui->labelCustomModel->hide();
        ui->editCustomModelId->hide();
    }
    else
    {
        ui->comboModelId->setCurrentIndex(ui->comboModelId->count() - 1);
        ui->editCustomModelId->setText(modelId);
        ui->labelCustomModel->show();
        ui->editCustomModelId->show();
    }
}

void AIModelPage::on_comboModelId_currentIndexChanged(int index)
{
    if (m_loadingUi)
        return;

    if (index < 0)
        return;

    QString selectedData = ui->comboModelId->itemData(index).toString();

    if (selectedData == CUSTOM_MODEL_TEXT)
    {
        ui->labelCustomModel->show();
        ui->editCustomModelId->show();
        ui->editCustomModelId->setFocus();
    }
    else
    {
        ui->labelCustomModel->hide();
        ui->editCustomModelId->hide();
        ui->editCustomModelId->clear();
    }

    saveCurrentModelDetail();
    populateDefaultModelCombo();
}

void AIModelPage::on_editCustomModelId_textEdited(const QString &text)
{
    if (m_loadingUi)
        return;

    Q_UNUSED(text);
    saveCurrentModelDetail();
    populateDefaultModelCombo();
}

void AIModelPage::on_btnFetchModels_clicked()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_models.size())
        return;

    QString apiUrl = ui->editApiUrl->text().trimmed();
    QString apiKey = ui->editApiKey->text().trimmed();

    if (apiUrl.isEmpty() || apiKey.isEmpty())
    {
        QMessageBox::warning(this, tr("Fetch Models"),
                             tr("Please fill in API URL and API Key first."));
        return;
    }

    if (m_modelsReply)
    {
        m_modelsReply->abort();
        m_modelsReply->deleteLater();
        m_modelsReply = nullptr;
    }

    ui->btnFetchModels->setEnabled(false);
    ui->btnFetchModels->setText(tr("Fetching..."));

    QString modelsUrl;
    if (apiUrl.contains("/v4/"))
    {
        modelsUrl = apiUrl.left(apiUrl.indexOf("/v4/") + 4) + "/models";
    }
    else if (apiUrl.contains("/v1/"))
    {
        int v1Index = apiUrl.indexOf("/v1/");
        modelsUrl = apiUrl.left(v1Index + 4) + "models";
    }
    else
    {
        QUrl url(apiUrl);
        QString baseUrl = QString("%1://%2").arg(url.scheme(), url.host());
        if (url.port() != -1)
        {
            baseUrl += QString(":%1").arg(url.port());
        }
        modelsUrl = baseUrl + "/v1/models";
    }

    QUrl url(modelsUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());

    m_modelsReply = APPLICATION->network()->get(request);
    connect(m_modelsReply, &QNetworkReply::finished, this, &AIModelPage::onModelsFetched);
    connect(m_modelsReply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(onModelsFetchError(QNetworkReply::NetworkError)));
}

void AIModelPage::onModelsFetched()
{
    if (!m_modelsReply)
        return;

    ui->btnFetchModels->setEnabled(true);
    ui->btnFetchModels->setText(tr("Fetch Models"));

    QNetworkReply *reply = m_modelsReply;
    m_modelsReply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        QMessageBox::critical(this, tr("Fetch Models"),
                              tr("Failed to fetch models: %1").arg(reply->errorString()));
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseErr);

    if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
    {
        QMessageBox::critical(this, tr("Fetch Models"),
                              tr("Failed to parse response: %1").arg(parseErr.errorString()));
        return;
    }

    QJsonObject root = doc.object();
    QStringList models;

    if (root.contains("data") && root["data"].isArray())
    {
        QJsonArray dataArray = root["data"].toArray();
        for (const QJsonValue &val : dataArray)
        {
            if (val.isObject())
            {
                QString modelId = val.toObject()["id"].toString();
                if (!modelId.isEmpty())
                {
                    models.append(modelId);
                }
            }
        }
    }
    else if (root.contains("result") && root["result"].isObject())
    {
        QJsonObject result = root["result"].toObject();
        if (result.contains("model_list") && result["model_list"].isArray())
        {
            QJsonArray modelList = result["model_list"].toArray();
            for (const QJsonValue &val : modelList)
            {
                if (val.isObject())
                {
                    QString modelId = val.toObject()["model"].toString();
                    if (!modelId.isEmpty())
                    {
                        models.append(modelId);
                    }
                }
            }
        }
    }

    if (models.isEmpty())
    {
        QMessageBox::information(this, tr("Fetch Models"),
                                 tr("No models found. You can enter a custom model ID."));
        models << "";
    }

    updateModelComboBox(models);
    QMessageBox::information(this, tr("Fetch Models"),
                             tr("Successfully fetched %1 model(s).").arg(models.size()));
}

void AIModelPage::onModelsFetchError(QNetworkReply::NetworkError error)
{
    Q_UNUSED(error);

    if (!m_modelsReply)
        return;

    ui->btnFetchModels->setEnabled(true);
    ui->btnFetchModels->setText(tr("Fetch Models"));

    QString errorMsg = m_modelsReply->errorString();
    QMessageBox::critical(this, tr("Fetch Models"),
                          tr("Network error: %1").arg(errorMsg));
}



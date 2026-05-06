#include "AIModelPage.h"
#include "ui_AIModelPage.h"

#include <QMessageBox>
#include <QTabBar>
#include <QDesktopServices>
#include <QUrl>

#include "settings/SettingsObject.h"
#include "Application.h"

static const QString SPINNER_CHARS = QString::fromUtf8("⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏");

AIModelPage::AIModelPage(QWidget *parent)
    : QWidget(parent), ui(new Ui::AIModelPage), m_currentIndex(-1), m_testAnalyzer(nullptr),
      m_spinnerTimer(nullptr), m_spinnerIndex(0)
{
    ui->setupUi(this);
    ui->tabWidget->tabBar()->hide();

    m_spinnerTimer = new QTimer(this);
    connect(m_spinnerTimer, &QTimer::timeout, this, &AIModelPage::updateSpinnerAnimation);

    loadSettings();
}

AIModelPage::~AIModelPage()
{
    if (m_spinnerTimer)
    {
        m_spinnerTimer->stop();
    }
    delete ui;
}

void AIModelPage::loadSettings()
{
    m_models = AIAnalyzer::loadModels();
    populateModelList();
    populateDefaultModelCombo();
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
    ui->modelListWidget->clear();
    for (const AIAnalyzer::ModelConfig &cfg : m_models)
    {
        ui->modelListWidget->addItem(QString("%1 (%2)").arg(cfg.name, cfg.modelId));
    }

    if (!m_models.isEmpty())
    {
        ui->modelListWidget->setCurrentRow(0);
    }
    else
    {
        m_currentIndex = -1;
        ui->groupBoxDetails->setEnabled(false);
    }
}

void AIModelPage::updateDetailFields()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_models.size())
    {
        ui->editName->clear();
        ui->editApiUrl->clear();
        ui->editApiKey->clear();
        ui->editModelId->clear();
        ui->groupBoxDetails->setEnabled(false);
        return;
    }

    ui->groupBoxDetails->setEnabled(true);
    const AIAnalyzer::ModelConfig &cfg = m_models[m_currentIndex];
    ui->editName->setText(cfg.name);
    ui->editApiUrl->setText(cfg.apiUrl);
    ui->editApiKey->setText(cfg.apiKey);
    ui->editModelId->setText(cfg.modelId);
}

void AIModelPage::saveCurrentModelDetail()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_models.size())
        return;

    m_models[m_currentIndex].name = ui->editName->text();
    m_models[m_currentIndex].apiUrl = ui->editApiUrl->text();
    m_models[m_currentIndex].apiKey = ui->editApiKey->text();
    m_models[m_currentIndex].modelId = ui->editModelId->text();

    if (ui->modelListWidget->item(m_currentIndex))
    {
        ui->modelListWidget->item(m_currentIndex)->setText(
            QString("%1 (%2)").arg(m_models[m_currentIndex].name, m_models[m_currentIndex].modelId));
    }
}

void AIModelPage::populateDefaultModelCombo()
{
    QString currentSelection = ui->comboDefaultModel->currentData().toString();
    ui->comboDefaultModel->clear();

    QString defaultModelId = APPLICATION->settings()->get("AIDefaultModel").toString();

    for (const AIAnalyzer::ModelConfig &cfg : m_models)
    {
        ui->comboDefaultModel->addItem(QString("%1 (%2)").arg(cfg.name, cfg.modelId), cfg.modelId);
    }

    int idx = ui->comboDefaultModel->findData(currentSelection);
    if (idx < 0)
    {
        idx = ui->comboDefaultModel->findData(defaultModelId);
    }
    if (idx < 0 && !m_models.isEmpty())
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
    cfg.name = tr("New Model");
    cfg.apiUrl = "";
    cfg.apiKey = "";
    cfg.modelId = "";

    m_models.append(cfg);
    ui->modelListWidget->addItem(QString("%1 (%2)").arg(cfg.name, cfg.modelId));
    ui->modelListWidget->setCurrentRow(m_models.size() - 1);
    populateDefaultModelCombo();
}

void AIModelPage::on_btnRemoveModel_clicked()
{
    int row = ui->modelListWidget->currentRow();
    if (row < 0 || row >= m_models.size())
        return;

    m_models.removeAt(row);
    delete ui->modelListWidget->takeItem(row);

    if (m_models.isEmpty())
    {
        m_currentIndex = -1;
        ui->groupBoxDetails->setEnabled(false);
    }
    else
    {
        if (row >= m_models.size())
            row = m_models.size() - 1;
        ui->modelListWidget->setCurrentRow(row);
    }

    populateDefaultModelCombo();
}

void AIModelPage::on_modelListWidget_currentRowChanged(int currentRow)
{
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
    if (cfg.apiUrl.isEmpty() || cfg.modelId.isEmpty() || cfg.apiKey.isEmpty())
    {
        QMessageBox::warning(this, tr("Test Connection"),
                             tr("Please fill in API URL, Model ID, and API Key before testing."));
        return;
    }

    if (m_testAnalyzer)
    {
        m_testAnalyzer->cancel();
        m_testAnalyzer->deleteLater();
        m_testAnalyzer = nullptr;
    }

    ui->btnTestConnection->setEnabled(false);
    m_spinnerIndex = 0;
    updateSpinnerAnimation();
    m_spinnerTimer->start(80);

    m_testAnalyzer = new AIAnalyzer(this);
    connect(m_testAnalyzer, &AIAnalyzer::analysisFinished, this, &AIModelPage::onTestFinished);
    connect(m_testAnalyzer, &AIAnalyzer::analysisError, this, &AIModelPage::onTestError);

    m_testAnalyzer->analyze("Hello, this is a connection test.", cfg);
}

void AIModelPage::onTestFinished(const QString &result)
{
    m_spinnerTimer->stop();
    ui->btnTestConnection->setEnabled(true);
    ui->btnTestConnection->setText(tr("Test Connection"));

    Q_UNUSED(result);
    QMessageBox::information(this, tr("Test Connection"), tr("Connection successful! The AI model is working."));

    if (m_testAnalyzer)
    {
        m_testAnalyzer->deleteLater();
        m_testAnalyzer = nullptr;
    }
}

void AIModelPage::onTestError(const QString &error)
{
    m_spinnerTimer->stop();
    ui->btnTestConnection->setEnabled(true);
    ui->btnTestConnection->setText(tr("Test Connection"));

    QMessageBox::critical(this, tr("Test Connection"), tr("Connection failed: %1").arg(error));

    if (m_testAnalyzer)
    {
        m_testAnalyzer->deleteLater();
        m_testAnalyzer = nullptr;
    }
}

void AIModelPage::on_editName_textEdited(const QString &text)
{
    Q_UNUSED(text);
    saveCurrentModelDetail();
    populateDefaultModelCombo();
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

void AIModelPage::on_editModelId_textEdited(const QString &text)
{
    Q_UNUSED(text);
    saveCurrentModelDetail();
    populateDefaultModelCombo();
}

void AIModelPage::on_btnGetApiKey_clicked()
{
    QString url = "https://open.bigmodel.cn/";
    QDesktopServices::openUrl(QUrl(url));
}

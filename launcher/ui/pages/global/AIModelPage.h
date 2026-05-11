#pragma once

#include <QWidget>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "ui/pages/BasePage.h"
#include <Application.h>
#include "ai/AIAnalyzer.h"

namespace Ui
{
    class AIModelPage;
}

class AIModelPage : public QWidget, public BasePage
{
    Q_OBJECT

public:
    explicit AIModelPage(QWidget *parent = 0);
    ~AIModelPage();

    QString displayName() const override
    {
        return tr("AI Analysis");
    }
    QIcon icon() const override
    {
        return APPLICATION->getThemedIcon("log");
    }
    QString id() const override
    {
        return "ai-model";
    }
    QString helpPage() const override
    {
        return "AI-Analysis";
    }
    virtual bool apply() override;

private:
    void loadSettings();
    void applySettings();

    void populateModelList();
    void updateDetailFields();
    void saveCurrentModelDetail();
    void populateDefaultModelCombo();
    void updateModelComboBox(const QStringList &models);
    QString getCurrentModelId();
    void setCurrentModelId(const QString &modelId);

private slots:
    void on_btnAddModel_clicked();
    void on_btnRemoveModel_clicked();
    void on_modelListWidget_currentRowChanged(int currentRow);
    void on_btnTestConnection_clicked();
    void on_btnFetchModels_clicked();
    void on_editApiUrl_textEdited(const QString &text);
    void on_editApiKey_textEdited(const QString &text);
    void on_comboModelId_currentIndexChanged(int index);
    void on_editCustomModelId_textEdited(const QString &text);
    void onTestConnectionFinished();
    void updateSpinnerAnimation();
    void onModelsFetched();
    void onModelsFetchError(QNetworkReply::NetworkError error);

private:
    Ui::AIModelPage *ui;
    QList<AIAnalyzer::ModelConfig> m_models;
    int m_currentIndex;
    QNetworkReply *m_testReply;
    QTimer *m_spinnerTimer;
    int m_spinnerIndex;
    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_modelsReply;
    bool m_loadingUi;
    static const QString CUSTOM_MODEL_TEXT;
};

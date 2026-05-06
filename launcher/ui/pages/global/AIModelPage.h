#pragma once

#include <QWidget>
#include <QTimer>

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

private slots:
    void on_btnAddModel_clicked();
    void on_btnRemoveModel_clicked();
    void on_modelListWidget_currentRowChanged(int currentRow);
    void on_btnTestConnection_clicked();
    void on_btnGetApiKey_clicked();
    void on_editName_textEdited(const QString &text);
    void on_editApiUrl_textEdited(const QString &text);
    void on_editApiKey_textEdited(const QString &text);
    void on_editModelId_textEdited(const QString &text);
    void onTestFinished(const QString &result);
    void onTestError(const QString &error);
    void updateSpinnerAnimation();

private:
    Ui::AIModelPage *ui;
    QList<AIAnalyzer::ModelConfig> m_models;
    int m_currentIndex;
    AIAnalyzer *m_testAnalyzer;
    QTimer *m_spinnerTimer;
    int m_spinnerIndex;
};

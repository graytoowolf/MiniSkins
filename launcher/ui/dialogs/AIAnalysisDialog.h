#pragma once

#include <QDialog>

namespace Ui
{
    class AIAnalysisDialog;
}

class AIAnalysisDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AIAnalysisDialog(const QString &analysisResult, QWidget *parent = nullptr);
    ~AIAnalysisDialog();

private slots:
    void on_btnCopy_clicked();
    void on_btnClose_clicked();

private:
    QString formatResult(const QString &result);
    QString formatInlineContent(const QString &text);

private:
    Ui::AIAnalysisDialog *ui;
    QString m_originalResult;
};

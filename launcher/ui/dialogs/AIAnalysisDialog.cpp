#include "AIAnalysisDialog.h"
#include "ui_AIAnalysisDialog.h"

#include <QClipboard>
#include <QApplication>
#include <QRegularExpression>

AIAnalysisDialog::AIAnalysisDialog(const QString &analysisResult, QWidget *parent)
    : QDialog(parent), ui(new Ui::AIAnalysisDialog), m_originalResult(analysisResult)
{
    ui->setupUi(this);
    setWindowTitle(tr("AI Crash Log Analysis"));
    setMinimumSize(650, 550);

    QString formattedHtml = formatResult(analysisResult);
    ui->textBrowser->setHtml(formattedHtml);
}

AIAnalysisDialog::~AIAnalysisDialog()
{
    delete ui;
}

QString AIAnalysisDialog::formatResult(const QString &result)
{
    QStringList lines = result.split('\n');
    QStringList htmlParts;
    int listCounter = 0;
    bool isOrderedList = false;

    for (int i = 0; i < lines.size(); ++i)
    {
        QString line = lines[i];
        QString trimmed = line.trimmed();

        if (trimmed.isEmpty())
        {
            if (listCounter > 0)
            {
                listCounter = 0;
                isOrderedList = false;
            }
            htmlParts.append("<p></p>");
            continue;
        }

        if (trimmed.startsWith("## [") && trimmed.endsWith("]"))
        {
            if (listCounter > 0)
            {
                listCounter = 0;
                isOrderedList = false;
            }
            QString title = trimmed.mid(4, trimmed.length() - 5);
            htmlParts.append(QString("<table width='100%%' cellpadding='4' cellspacing='0' bgcolor='#E3F2FD'><tr><td><b><font color='#1976D2' size='4' face='Microsoft YaHei, sans-serif'>%1</font></b></td></tr></table>").arg(title));
            continue;
        }

        if (trimmed.startsWith("- ") || trimmed.startsWith("* "))
        {
            if (listCounter == 0 || isOrderedList)
            {
                listCounter = 0;
                isOrderedList = false;
            }
            QString content = trimmed.mid(2);
            content = formatInlineContent(content);
            htmlParts.append(QString("<p><font color='#424242'>&nbsp;&nbsp;&nbsp;&nbsp;• %1</font></p>").arg(content));
            continue;
        }

        QRegularExpression numRegex("^\\d+\\.\\s");
        if (numRegex.match(trimmed).hasMatch())
        {
            if (listCounter == 0 && !isOrderedList)
            {
                listCounter = 0;
                isOrderedList = true;
            }
            else if (!isOrderedList)
            {
                listCounter = 0;
            }
            isOrderedList = true;
            listCounter++;
            int dotPos = trimmed.indexOf(". ");
            QString content = trimmed.mid(dotPos + 2);
            content = formatInlineContent(content);
            htmlParts.append(QString("<p><font color='#424242'>&nbsp;&nbsp;&nbsp;&nbsp;%1. %2</font></p>").arg(listCounter).arg(content));
            continue;
        }

        if (listCounter > 0)
        {
            listCounter = 0;
            isOrderedList = false;
        }

        QString formattedLine = formatInlineContent(trimmed);
        htmlParts.append(QString("<p><font color='#333333'>%1</font></p>").arg(formattedLine));
    }

    QString bodyContent = htmlParts.join("");

    return QString(
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"UTF-8\">"
        "</head>"
        "<body>"
        "%1"
        "</body>"
        "</html>"
    ).arg(bodyContent);
}

QString AIAnalysisDialog::formatInlineContent(const QString &text)
{
    QString result = text;

    result.replace("&", "&amp;");
    result.replace("<", "&lt;");
    result.replace(">", "&gt;");

    result.replace(QRegularExpression("\\*\\*([^*]+)\\*\\*"), "<b>\\1</b>");

    result.replace(QRegularExpression("`([^`]+)`"), "<code>\\1</code>");

    result.replace(QRegularExpression("\\b(java\\.[a-zA-Z0-9_.]+(?:Exception|Error))\\b"), "<font color='#c62828'><b>\\1</b></font>");

    result.replace(QRegularExpression("\\b([a-zA-Z][a-zA-Z0-9_-]*(?:Mod|mod))\\b"), "<font color='#6a1b9a'><b>\\1</b></font>");

    return result;
}

void AIAnalysisDialog::on_btnCopy_clicked()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(m_originalResult);
}

void AIAnalysisDialog::on_btnClose_clicked()
{
    accept();
}

#include "AIProgressDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

AIProgressDialog::AIProgressDialog(QWidget *parent)
    : QDialog(parent), m_dots(0)
{
    setWindowTitle(tr("AI Analysis"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedSize(350, 150);
    setModal(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    QHBoxLayout *contentLayout = new QHBoxLayout();

    m_animationLabel = new QLabel(this);
    m_animationLabel->setFixedSize(48, 48);
    m_animationLabel->setAlignment(Qt::AlignCenter);
    m_animationLabel->setText(QString::fromUtf8("\xF0\x9F\xA4\x96"));
    m_animationLabel->setStyleSheet("font-size: 36px;");
    contentLayout->addWidget(m_animationLabel);

    m_statusLabel = new QLabel(tr("Analyzing crash log"), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    contentLayout->addWidget(m_statusLabel, 1);

    mainLayout->addLayout(contentLayout);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setStyleSheet(
        "QProgressBar {"
        "   border: none;"
        "   background-color: #e0e0e0;"
        "   border-radius: 3px;"
        "}"
        "QProgressBar::chunk {"
        "   background-color: #2196F3;"
        "   border-radius: 3px;"
        "}"
    );
    mainLayout->addWidget(m_progressBar);

    m_animationTimer = new QTimer(this);
    connect(m_animationTimer, &QTimer::timeout, this, &AIProgressDialog::updateAnimation);
    m_animationTimer->start(500);
}

AIProgressDialog::~AIProgressDialog()
{
    if (m_animationTimer)
    {
        m_animationTimer->stop();
    }
}

void AIProgressDialog::setStatusText(const QString &text)
{
    m_statusLabel->setText(text);
}

void AIProgressDialog::updateAnimation()
{
    m_dots = (m_dots + 1) % 4;
    QString dots = QString(".").repeated(m_dots);
    m_statusLabel->setText(tr("Analyzing crash log") + dots);
}

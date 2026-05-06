#pragma once

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>

class AIProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AIProgressDialog(QWidget *parent = nullptr);
    ~AIProgressDialog();

    void setStatusText(const QString &text);

private slots:
    void updateAnimation();

private:
    QLabel *m_statusLabel;
    QLabel *m_animationLabel;
    QProgressBar *m_progressBar;
    QTimer *m_animationTimer;
    int m_dots;
};

#pragma once
#include <QRunnable>
#include <QObject>
#include <QList>
#include "fingerprint.h"

class ModFingerprintTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    explicit ModFingerprintTask(const QList<fingerprint::ModInfo> &mods);
    void run() override;

signals:
    void succeeded(QList<fingerprint::ModInfo> result);

private:
    QList<fingerprint::ModInfo> m_mods;
};

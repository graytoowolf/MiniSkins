#pragma once

#include <QList>
#include <QString>

namespace fingerprint
{
struct ModInfo
{
    QString filePath;
    QString fileFingerprint;
    int projectId;
    int fileId;
    QString name;
    bool isValid;

    ModInfo() : projectId(0), fileId(0), isValid(false) {}
    explicit ModInfo(const QString &path) : filePath(path), projectId(0), fileId(0), isValid(false) {}
};

QString getJarFingerprint(const QString &jarPath);

ModInfo processModInfo(const ModInfo &modInfo);
QList<ModInfo> processModInfoList(const QList<ModInfo> &modInfoList);
}

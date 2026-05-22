#include "fingerprint.h"

#include <QDebug>
#include <QFile>
#include <QtGlobal>

namespace
{
bool isWhitespaceCharacter(uchar b)
{
    return b == 9 || b == 10 || b == 13 || b == 32;
}

quint32 computeNormalizedLength(const QByteArray &buffer)
{
    quint32 length = 0;
    for (int i = 0; i < buffer.size(); ++i)
    {
        if (!isWhitespaceCharacter(static_cast<uchar>(buffer.at(i))))
        {
            ++length;
        }
    }
    return length;
}

quint32 computeHash(const QByteArray &buffer)
{
    const quint32 multiplex = 1540483477;
    const quint32 normalizedLength = computeNormalizedLength(buffer);
    quint32 hash = quint32(1) ^ normalizedLength;
    quint32 chunk = 0;
    quint32 shift = 0;

    for (int i = 0; i < buffer.size(); ++i)
    {
        const uchar byte = static_cast<uchar>(buffer.at(i));
        if (isWhitespaceCharacter(byte))
        {
            continue;
        }

        chunk |= quint32(byte) << shift;
        shift += 8;
        if (shift == 32)
        {
            const quint32 mixed = chunk * multiplex;
            const quint32 folded = (mixed ^ (mixed >> 24)) * multiplex;
            hash = (hash * multiplex) ^ folded;
            chunk = 0;
            shift = 0;
        }
    }

    if (shift > 0)
    {
        hash = (hash ^ chunk) * multiplex;
    }

    const quint32 mixed = (hash ^ (hash >> 13)) * multiplex;
    return mixed ^ (mixed >> 15);
}

}

namespace fingerprint
{
QString getJarFingerprint(const QString &jarPath)
{
    QFile file(jarPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "Failed to open mod file for fingerprinting:" << jarPath << file.errorString();
        return QString();
    }

    const QByteArray contents = file.readAll();
    if (contents.isEmpty())
    {
        return QString();
    }

    return QString::number(static_cast<qulonglong>(computeHash(contents)));
}

ModInfo processModInfo(const ModInfo &modInfo)
{
    QList<ModInfo> list;
    list.append(modInfo);
    list = processModInfoList(list);
    return list.isEmpty() ? modInfo : list.first();
}

QList<ModInfo> processModInfoList(const QList<ModInfo> &modInfoList)
{
    if (modInfoList.isEmpty())
    {
        return QList<ModInfo>();
    }
    if (qEnvironmentVariableIsSet("DISABLE_FINGERPRINT"))
    {
        return modInfoList;
    }

    QList<ModInfo> results;

    for (int i = 0; i < modInfoList.size(); ++i)
    {
        ModInfo modInfo = modInfoList.at(i);
        modInfo.fileFingerprint = getJarFingerprint(modInfo.filePath);
        results.append(modInfo);
    }

    return results;
}
}

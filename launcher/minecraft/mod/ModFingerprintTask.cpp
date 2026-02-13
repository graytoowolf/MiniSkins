#include "ModFingerprintTask.h"
#include <QDebug>

using fingerprint::ModInfo;

ModFingerprintTask::ModFingerprintTask(const QList<ModInfo> &mods) : m_mods(mods)
{
}

void ModFingerprintTask::run()
{
    // 在后台线程中处理指纹计算和API请求
    QList<ModInfo> results = fingerprint::processModInfoList(m_mods);
    emit succeeded(results);
}

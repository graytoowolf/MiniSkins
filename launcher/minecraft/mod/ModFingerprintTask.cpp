#include "ModFingerprintTask.h"

using fingerprint::ModInfo;

ModFingerprintTask::ModFingerprintTask(const QList<ModInfo> &mods) : m_mods(mods)
{
}

void ModFingerprintTask::run()
{
    QList<ModInfo> results = fingerprint::processModInfoList(m_mods);
    emit succeeded(results);
}

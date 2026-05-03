#pragma once

#include <QString>

struct ModpackUpdateContext
{
    QString addonId;
    QString fileId;
    QString instanceId;
    QString platform;
    QString downloadUrl;

    bool isValid() const
    {
        return !instanceId.isEmpty();
    }

    void clear()
    {
        addonId.clear();
        fileId.clear();
        instanceId.clear();
        platform.clear();
        downloadUrl.clear();
    }
};

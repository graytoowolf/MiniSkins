/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <QDir>
#include <QString>

#include "Mod.h"
#include <QDebug>
#include <FileSystem.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace
{

    ModDetails invalidDetails;

}

// 静态成员初始化
QMap<QString, QJsonDocument> Mod::s_modsJsonMap;

Mod::Mod(const QFileInfo &file)
{
    repath(file);
    m_changedDateTime = file.lastModified();
}

void Mod::repath(const QFileInfo &file)
{
    m_file = file;
    QString name_base = file.fileName();

    m_type = Mod::MOD_UNKNOWN;

    m_mmc_id = name_base;

    if (m_file.isDir())
    {
        m_type = MOD_FOLDER;
        m_name = name_base;
    }
    else if (m_file.isFile())
    {
        if (name_base.endsWith(".disabled"))
        {
            m_enabled = false;
            name_base.chop(9);
        }
        else
        {
            m_enabled = true;
        }
        if (name_base.endsWith(".zip") || name_base.endsWith(".jar"))
        {
            m_type = MOD_ZIPFILE;
            name_base.chop(4);
        }
        else if (name_base.endsWith(".litemod"))
        {
            m_type = MOD_LITEMOD;
            name_base.chop(8);
        }
        else
        {
            m_type = MOD_SINGLEFILE;
        }
        m_name = name_base;
    }
}

void Mod::loadModsJson(const QString &jsonPath)
{
    if (!s_modsJsonMap.contains(jsonPath))
    {
        // 如果是新的json文件，先清空现有缓存
        if (!s_modsJsonMap.isEmpty())
        {
            s_modsJsonMap.clear();
        }

        QFile jsonFile(jsonPath);
        if (jsonFile.open(QIODevice::ReadOnly))
        {
            s_modsJsonMap[jsonPath] = QJsonDocument::fromJson(jsonFile.readAll());
            jsonFile.close();
        }
    }
}

bool Mod::saveModsJson(const QString &jsonPath)
{
    if (!s_modsJsonMap.contains(jsonPath))
    {
        return false;
    }

    QFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::WriteOnly))
    {
        return false;
    }

    jsonFile.write(s_modsJsonMap[jsonPath].toJson());
    jsonFile.close();
    return true;
}

bool Mod::enable(bool value)
{
    if (m_type == Mod::MOD_UNKNOWN || m_type == Mod::MOD_FOLDER)
        return false;

    if (m_enabled == value)
        return false;

    QString path = m_file.absoluteFilePath();
    QString oldName = m_file.fileName();
    QString newName;

    // 处理文件重命名
    if (value)
    {
        QFile foo(path);
        if (!path.endsWith(".disabled"))
            return false;
        path.chop(9);
        newName = oldName.left(oldName.length() - 9);
        if (!foo.rename(path))
            return false;
    }
    else
    {
        QFile foo(path);
        path += ".disabled";
        newName = oldName + ".disabled";
        if (!foo.rename(path))
            return false;
    }

    // 处理JSON文件
    QString newJsonPath = QDir::cleanPath(QDir(QFileInfo(path).dir().absolutePath()).filePath("../../mod.json"));

    if (QFile::exists(newJsonPath))
    {
        loadModsJson(newJsonPath);

        if (s_modsJsonMap[newJsonPath].isArray())
        {
            QJsonArray modsArray = s_modsJsonMap[newJsonPath].array();
            bool found = false;

            for (int i = 0; i < modsArray.size(); i++)
            {
                QJsonObject mod = modsArray[i].toObject();
                if (mod["name"].toString() == oldName)
                {
                    mod["name"] = newName;
                    modsArray[i] = mod;
                    found = true;
                    break;
                }
            }

            if (found)
            {
                s_modsJsonMap[newJsonPath].setArray(modsArray);
                saveModsJson(newJsonPath);
            }
        }
    }

    repath(QFileInfo(path));
    m_enabled = value;
    return true;
}

bool Mod::destroy()
{
    m_type = MOD_UNKNOWN;
    return FS::deletePath(m_file.filePath());
}

const ModDetails &Mod::details() const
{
    if (!m_localDetails)
        return invalidDetails;
    return *m_localDetails;
}

QString Mod::version() const
{
    return details().version;
}

QString Mod::name() const
{
    auto &d = details();
    if (!d.name.isEmpty())
    {
        return d.name;
    }
    return m_name;
}

QString Mod::homeurl() const
{
    return details().homeurl;
}

QString Mod::description() const
{
    return details().description;
}

QStringList Mod::authors() const
{
    return details().authors;
}

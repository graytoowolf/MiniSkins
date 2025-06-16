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

using fingerprint::ModInfo;

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
    // 每次都重新加载JSON文件，确保数据是最新的
    QFile jsonFile(jsonPath);
    if (jsonFile.open(QIODevice::ReadOnly))
    {
        s_modsJsonMap[jsonPath] = QJsonDocument::fromJson(jsonFile.readAll());
        jsonFile.close();
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

// 获取MOD对应的JSON文件路径
QString Mod::getModJsonPath() const
{
    return QDir::cleanPath(m_file.absoluteDir().absoluteFilePath("../../mod.json"));
}

// 在JSON数组中查找指定名称的MOD，返回索引，未找到返回-1
int Mod::findModInJson(const QJsonArray &modsArray, const QString &modName)
{
    for (int i = 0; i < modsArray.size(); i++)
    {
        QJsonObject mod = modsArray[i].toObject();
        if (mod["name"].toString() == modName)
        {
            return i;
        }
    }
    return -1;
}

// 更新JSON中的MOD条目
bool Mod::updateModInJson(const QString &jsonPath, const QString &oldName, const QString &newName, bool required)
{
    if (!QFile::exists(jsonPath))
    {
        return false;
    }

    loadModsJson(jsonPath);

    if (!s_modsJsonMap[jsonPath].isArray())
    {
        return false;
    }

    QJsonArray modsArray = s_modsJsonMap[jsonPath].array();
    int index = findModInJson(modsArray, oldName);

    if (index != -1)
    {
        QJsonObject mod = modsArray[index].toObject();
        mod["name"] = newName;
        mod["required"] = required;
        modsArray[index] = mod;
        s_modsJsonMap[jsonPath].setArray(modsArray);
        return saveModsJson(jsonPath);
    }

    return false;
}

// 批量从JSON中删除MOD条目
bool Mod::removeModsFromJson(const QString &jsonPath, const QStringList &modNames)
{
    if (!QFile::exists(jsonPath) || modNames.isEmpty())
    {
        return false;
    }

    loadModsJson(jsonPath);

    if (!s_modsJsonMap[jsonPath].isArray())
    {
        return false;
    }

    QJsonArray modsArray = s_modsJsonMap[jsonPath].array();
    bool hasChanges = false;

    // 从后往前删除，避免索引变化问题
    for (int i = modsArray.size() - 1; i >= 0; --i)
    {
        QJsonObject mod = modsArray[i].toObject();
        QString modName = mod["name"].toString();
        if (modNames.contains(modName))
        {
            modsArray.removeAt(i);
            hasChanges = true;
        }
    }

    if (hasChanges)
    {
        s_modsJsonMap[jsonPath].setArray(modsArray);
        return saveModsJson(jsonPath);
    }

    return false;
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

    // 使用通用函数处理JSON文件
    QString jsonPath = getModJsonPath();

    updateModInJson(jsonPath, oldName, newName, value);

    repath(QFileInfo(path));
    m_enabled = value;
    return true;
}

bool Mod::destroy()
{
    m_type = MOD_UNKNOWN;

    // 使用通用函数处理JSON文件，删除对应的MOD数据
    QString jsonPath = getModJsonPath();
    QString modFileName = m_file.fileName();

    removeModsFromJson(jsonPath, QStringList{modFileName});

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

// 用于ModDownloadPage的静态函数实现
bool Mod::isModInstalled(const QString &jsonPath, int projectId, const QString &modsRoot)
{
    // 加载mod.json文件
    loadModsJson(jsonPath);

    // 获取JSON文档
    if (!s_modsJsonMap.contains(jsonPath))
    {
        return false;
    }

    QJsonArray modsArray = s_modsJsonMap[jsonPath].array();
    for (const QJsonValue &value : modsArray)
    {
        QJsonObject modObj = value.toObject();
        if (modObj["projectID"].toInt() == projectId)
        {
            // 检查MOD文件是否实际存在
            QString modFileName = modObj["name"].toString();
            QString modFilePath = QDir(modsRoot).absoluteFilePath(modFileName);
            if (!QFile::exists(modFilePath))
            {
                return false;
            }
            return true;
        }
    }
    return false;
}

// 批量添加模组到JSON
bool Mod::addModsToJson(const QString &jsonPath, const QList<ModInfo> &modInfos, bool required)
{
    if (modInfos.isEmpty())
    {
        return false;
    }

    // 加载mod.json文件
    loadModsJson(jsonPath);

    QJsonArray modsArray;
    if (s_modsJsonMap.contains(jsonPath))
    {
        modsArray = s_modsJsonMap[jsonPath].array();
    }

    bool hasChanges = false;

    for (const ModInfo &modInfo : modInfos)
        {
            // 检查是否已存在该projectID的模组
            bool found = false;
            for (int i = 0; i < modsArray.size(); ++i)
            {
                QJsonObject modObj = modsArray[i].toObject();
                if (modObj["projectID"].toInt() == modInfo.projectId)
                {
                    // 更新现有条目
                    modObj["fileID"] = modInfo.fileId;
                    modObj["name"] = modInfo.name;
                    modObj["required"] = required;
                    modsArray[i] = modObj;
                    found = true;
                    hasChanges = true;
                    break;
                }
            }
            if (!found && !modInfo.name.isEmpty())
            {
                // 添加新模组
                QJsonObject newMod;
                newMod["fileID"] = modInfo.fileId;
                newMod["name"] = modInfo.name;
                newMod["projectID"] = modInfo.projectId;
                newMod["required"] = required;

                modsArray.append(newMod);
                hasChanges = true;
            }
        }

    if (hasChanges)
    {
        s_modsJsonMap[jsonPath] = QJsonDocument(modsArray);
        return saveModsJson(jsonPath);
    }

    return false;
}

// 通过mod名字检查mod.json中是否存在该mod
bool Mod::isModExistsByName(const QString &jsonPath, const QString &modName)
{
    if (!QFile::exists(jsonPath) || modName.isEmpty())
    {
        return false;
    }

    // 加载mod.json文件
    loadModsJson(jsonPath);

    if (!s_modsJsonMap.contains(jsonPath))
    {
        return false;
    }

    if (!s_modsJsonMap[jsonPath].isArray())
    {
        return false;
    }

    QJsonArray modsArray = s_modsJsonMap[jsonPath].array();

    // 使用现有的findModInJson函数查找mod
    int index = findModInJson(modsArray, modName);

    return index != -1;
}

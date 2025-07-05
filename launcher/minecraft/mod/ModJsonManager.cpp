/* Copyright 2013-2021 MiniSkins Contributors
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

#include "ModJsonManager.h"
#include <QFile>
#include <QDir>
#include <QDebug>

using fingerprint::ModInfo;

bool ModJsonManager::initialize(const QString &jsonPath)
{
    if (m_initialized)
    {
        clear();
    }

    m_jsonPath = jsonPath;
    m_hasChanges = false;

    if (!loadJsonFile())
    {
        // 如果文件不存在，创建空的JSON数组
        m_jsonDocument = QJsonDocument(QJsonArray());
    }

    m_initialized = true;
    return true;
}

void ModJsonManager::clear()
{
    m_jsonPath.clear();
    m_jsonDocument = QJsonDocument();
    m_initialized = false;
    m_hasChanges = false;
}

bool ModJsonManager::save()
{
    if (!m_initialized || !m_hasChanges)
    {
        return true; // 没有变化，不需要保存
    }

    QFile jsonFile(m_jsonPath);
    if (!jsonFile.open(QIODevice::WriteOnly))
    {
        qDebug() << "Failed to open file for writing:" << m_jsonPath;
        return false;
    }

    jsonFile.write(m_jsonDocument.toJson());
    jsonFile.close();
    m_hasChanges = false;
    return true;
}

bool ModJsonManager::isModExistsByName(const QString &modName) const
{
    if (!m_initialized || modName.isEmpty())
    {
        return false;
    }

    return findModByName(modName) != -1;
}

QString ModJsonManager::getModNameByProjectId(int projectId) const
{
    if (!m_initialized || projectId <= 0 || !validateJsonStructure())
    {
        return QString();
    }

    QJsonArray modsArray = m_jsonDocument.array();
    for (const QJsonValue &value : modsArray)
    {
        QJsonObject modObj = value.toObject();
        if (modObj["projectID"].toInt() == projectId)
        {
            return modObj["name"].toString();
        }
    }

    return QString();
}

int ModJsonManager::getModFileIdByProjectId(int projectId) const
{
    if (!m_initialized || projectId <= 0 || !validateJsonStructure())
    {
        return -1;
    }

    QJsonArray modsArray = m_jsonDocument.array();
    for (const QJsonValue &value : modsArray)
    {
        QJsonObject modObj = value.toObject();
        if (modObj["projectID"].toInt() == projectId)
        {
            return modObj["fileID"].toInt();
        }
    }

    return -1;
}

bool ModJsonManager::isModFileExistsByProjectId(int projectId, const QString &modsRoot) const
{
    if (!m_initialized || projectId <= 0 || !validateJsonStructure())
    {
        return false;
    }

    QString modFileName = getModNameByProjectId(projectId);
    if (modFileName.isEmpty())
    {
        return false;
    }

    QString modFilePath = QDir(modsRoot).absoluteFilePath(modFileName);
    return QFile::exists(modFilePath);
}

int ModJsonManager::findModByName(const QString &modName) const
{
    if (!m_initialized || modName.isEmpty() || !validateJsonStructure())
    {
        return -1;
    }

    QJsonArray modsArray = m_jsonDocument.array();
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

bool ModJsonManager::isModInstalledByProjectId(int projectId, int fileId, const QString &modsRoot) const
{
    if (!m_initialized || projectId <= 0 || !validateJsonStructure())
    {
        return false;
    }

    QJsonArray modsArray = m_jsonDocument.array();
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

            // 比较fileID - 只有当文件存在且fileID完全匹配时才返回true
            int existingFileId = modObj["fileID"].toInt();
            return existingFileId == fileId;
        }
    }
    return false;
}

fingerprint::ModInfo ModJsonManager::getModInfoByFingerprint(const QString &fileFingerprint) const
{
    fingerprint::ModInfo result;

    if (!m_initialized || fileFingerprint.isEmpty() || !validateJsonStructure())
    {
        return result;
    }

    QJsonArray modsArray = m_jsonDocument.array();
    for (const QJsonValue &value : modsArray)
    {
        QJsonObject modObj = value.toObject();
        if (modObj["fileFingerprint"].toString() == fileFingerprint)
        {
            result.projectId = modObj["projectID"].toInt();
            result.fileId = modObj["fileID"].toInt();
            result.name = modObj["name"].toString();
            result.fileFingerprint = fileFingerprint;
            result.isValid = true;
            break;
        }
    }

    return result;
}

bool ModJsonManager::addMod(const ModInfo &modInfo, bool required)
{
    if (!m_initialized || modInfo.name.isEmpty())
    {
        return false;
    }

    if (!validateJsonStructure())
    {
        return false;
    }

    QJsonArray modsArray = m_jsonDocument.array();

    // 检查是否已存在该projectID的模组
    for (int i = 0; i < modsArray.size(); ++i)
    {
        QJsonObject modObj = modsArray[i].toObject();
        if (modObj["projectID"].toInt() == modInfo.projectId)
        {
            // 更新现有条目
            modObj["fileID"] = modInfo.fileId;
            modObj["name"] = modInfo.name;
            modObj["required"] = required;
            if (!modInfo.fileFingerprint.isEmpty())
    {
        qDebug() << "Adding fileFingerprint to existing mod:" << modInfo.name << "fingerprint:" << modInfo.fileFingerprint;
        modObj["fileFingerprint"] = modInfo.fileFingerprint;
    }
    else
    {
        qDebug() << "fileFingerprint is empty for existing mod:" << modInfo.name;
    }
            modsArray[i] = modObj;
            m_jsonDocument.setArray(modsArray);
            markAsChanged();
            return true;
        }
    }

    // 添加新模组
    QJsonObject newMod;
    newMod["fileID"] = modInfo.fileId;
    newMod["name"] = modInfo.name;
    newMod["projectID"] = modInfo.projectId;
    newMod["required"] = required;
    if (!modInfo.fileFingerprint.isEmpty())
    {
        newMod["fileFingerprint"] = modInfo.fileFingerprint;
    }
    else
    {
        qDebug() << "fileFingerprint is empty for new mod:" << modInfo.name;
    }

    modsArray.append(newMod);
    m_jsonDocument.setArray(modsArray);
    markAsChanged();
    return true;
}

bool ModJsonManager::addMods(const QList<ModInfo> &modInfos, bool required)
{
    if (!m_initialized || modInfos.isEmpty())
    {
        return false;
    }

    bool hasChanges = false;
    for (const ModInfo &modInfo : modInfos)
    {
        if (addMod(modInfo, required))
        {
            hasChanges = true;
        }
    }

    return hasChanges;
}

bool ModJsonManager::updateMod(const QString &oldName, const QString &newName, bool required)
{
    if (!m_initialized || oldName.isEmpty() || newName.isEmpty() || !validateJsonStructure())
    {
        return false;
    }

    QJsonArray modsArray = m_jsonDocument.array();
    int index = findModByName(oldName);

    if (index != -1)
    {
        QJsonObject mod = modsArray[index].toObject();
        mod["name"] = newName;
        mod["required"] = required;
        modsArray[index] = mod;
        m_jsonDocument.setArray(modsArray);
        markAsChanged();
        return true;
    }

    return false;
}

bool ModJsonManager::removeMod(const QString &modName)
{
    if (!m_initialized || modName.isEmpty() || !validateJsonStructure())
    {
        return false;
    }

    QJsonArray modsArray = m_jsonDocument.array();
    int index = findModByName(modName);

    if (index != -1)
    {
        modsArray.removeAt(index);
        m_jsonDocument.setArray(modsArray);
        markAsChanged();
        return true;
    }

    return false;
}

bool ModJsonManager::removeMods(const QStringList &modNames)
{
    if (!m_initialized || modNames.isEmpty() || !validateJsonStructure())
    {
        return false;
    }

    QJsonArray modsArray = m_jsonDocument.array();
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
        m_jsonDocument.setArray(modsArray);
        markAsChanged();
        return true;
    }

    return false;
}

bool ModJsonManager::loadJsonFile()
{
    if (!QFile::exists(m_jsonPath))
    {
        return false;
    }

    QFile jsonFile(m_jsonPath);
    if (!jsonFile.open(QIODevice::ReadOnly))
    {
        qDebug() << "Failed to open file for reading:" << m_jsonPath;
        return false;
    }

    QByteArray jsonData = jsonFile.readAll();
    jsonFile.close();

    QJsonParseError parseError;
    m_jsonDocument = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        qDebug() << "JSON parse error:" << parseError.errorString();
        return false;
    }

    return true;
}

bool ModJsonManager::validateJsonStructure() const
{
    return m_jsonDocument.isArray();
}
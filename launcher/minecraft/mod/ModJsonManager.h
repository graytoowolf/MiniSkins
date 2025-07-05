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

#pragma once

#include <QString>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QList>
#include "fingerprint.h"

/**
 * ModJsonManager - 简化的mod.json文件管理器
 * 
 * 特点：
 * - 单文件管理，调用前初始化，调用后清除数据
 * - 不支持文件监控和并发访问
 * - 提供基本的CRUD操作
 */
class ModJsonManager
{
public:
    ModJsonManager() = default;
    ~ModJsonManager() = default;

    // 禁用拷贝和赋值
    ModJsonManager(const ModJsonManager&) = delete;
    ModJsonManager& operator=(const ModJsonManager&) = delete;

    // 生命周期管理
    bool initialize(const QString &jsonPath);
    void clear();
    bool save();

    // 查询操作
    bool isModExistsByName(const QString &modName) const;
    QString getModNameByProjectId(int projectId) const;
    int getModFileIdByProjectId(int projectId) const;
    bool isModFileExistsByProjectId(int projectId, const QString &modsRoot) const;
    int findModByName(const QString &modName) const;
    bool isModInstalledByProjectId(int projectId, int fileId, const QString &modsRoot) const;
    fingerprint::ModInfo getModInfoByFingerprint(const QString &fileFingerprint) const;

    // 修改操作
    bool addMod(const fingerprint::ModInfo &modInfo, bool required = true);
    bool addMods(const QList<fingerprint::ModInfo> &modInfos, bool required = true);
    bool updateMod(const QString &oldName, const QString &newName, bool required);
    bool removeMod(const QString &modName);
    bool removeMods(const QStringList &modNames);

    // 状态检查
    bool isInitialized() const { return m_initialized; }
    bool hasUnsavedChanges() const { return m_hasChanges; }
    QString getJsonPath() const { return m_jsonPath; }

private:
    // 内部辅助函数
    bool loadJsonFile();
    bool validateJsonStructure() const;
    void markAsChanged() { m_hasChanges = true; }

    // 成员变量
    QString m_jsonPath;
    QJsonDocument m_jsonDocument;
    bool m_initialized = false;
    bool m_hasChanges = false;
};
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

#include <QWidget>

#include "BaseInstance.h"
#include "QObjectPtr.h"
#include "java/JavaChecker.h"
#include "java/JavaInstall.h"
#include "minecraft/MinecraftInstance.h"
#include "tasks/Task.h"

// Task that runs before any LaunchTask is created: it computes the Java major
// version required by the instance's Minecraft component, scans the locally
// installed Java installations and adapts the instance's JavaPath setting:
//  - current Java already compatible -> keep it, no user interaction
//  - exactly one compatible candidate -> apply it automatically
//  - multiple compatible candidates -> modal selection dialog
//  - no compatible candidate -> information dialog with download guidance, launch aborted
class JavaMatchTask : public Task
{
    Q_OBJECT
public:
    explicit JavaMatchTask(InstancePtr instance, QWidget *parentWidget);

    void executeTask() override;

private slots:
    void currentJavaCheckFinished(JavaCheckResult result);
    void listLoadSucceeded();
    void listLoadFailed(QString reason);

private:
    void beginCandidateScan();
    void proceedWithCandidates();
    void applyJavaPath(const QString &path);
    void showNoMatchDialog();

    InstancePtr m_instance;
    QWidget *m_parentWidget = nullptr;
    MinecraftInstancePtr m_inst;
    int m_requiredMajor = 0;
    JavaCheckerPtr m_currentJavaChecker;
};

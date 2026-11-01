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

#include "JavaMatchTask.h"

#include "Application.h"
#include "FileSystem.h"

#include "java/JavaInstallList.h"
#include "java/JavaMatch.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VersionFilterData.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/JavaMatchSelectDialog.h"

JavaMatchTask::JavaMatchTask(InstancePtr instance, QWidget *parentWidget)
    : m_instance(instance), m_parentWidget(parentWidget)
{
}

void JavaMatchTask::executeTask()
{
    m_inst = std::dynamic_pointer_cast<MinecraftInstance>(m_instance);
    if (!m_inst)
    {
        // Not a Minecraft instance, nothing to match
        emitSucceeded();
        return;
    }

    auto minecraftComponent = m_inst->getPackProfile()->getComponent("net.minecraft");
    if (!minecraftComponent)
    {
        emitSucceeded();
        return;
    }

    m_requiredMajor = requiredJavaMajor(minecraftComponent->getReleaseDateTime());
    if (m_requiredMajor <= 0)
    {
        // This Minecraft version has no minimum Java requirement
        emitSucceeded();
        return;
    }

    setStatus(tr("Checking Java compatibility..."));

    // First probe the current JavaPath: if it already satisfies the requirement
    // we keep the user's settings untouched.
    QString currentPath = FS::ResolveExecutable(m_inst->settings()->get("JavaPath").toString());
    if (currentPath.isEmpty())
    {
        logWarning(tr("The current Java path is not valid."));
        beginCandidateScan();
        return;
    }

    m_currentJavaChecker = new JavaChecker();
    m_currentJavaChecker->m_path = currentPath;
    connect(m_currentJavaChecker.get(), &JavaChecker::checkFinished, this, &JavaMatchTask::currentJavaCheckFinished);
    m_currentJavaChecker->performCheck();
}

void JavaMatchTask::currentJavaCheckFinished(JavaCheckResult result)
{
    if (result.validity == JavaCheckResult::Validity::Valid)
    {
        if (result.javaVersion.major() >= m_requiredMajor)
        {
            // Current Java is compatible, keep it as is
            emitSucceeded();
            return;
        }
        logWarning(tr("The current Java (%1) does not satisfy the requirement of Java %2 or newer.")
                       .arg(result.javaVersion.toString())
                       .arg(m_requiredMajor));
    }
    else
    {
        // Could not read version info (permissions, broken install, ...) - skip
        // it and continue with the candidate scan instead of failing hard.
        logWarning(tr("Failed to determine the version of the current Java installation."));
    }
    beginCandidateScan();
}

void JavaMatchTask::beginCandidateScan()
{
    setStatus(tr("Scanning for compatible Java installations..."));

    auto javaList = APPLICATION->javalist();
    if (javaList->isLoaded())
    {
        proceedWithCandidates();
        return;
    }

    auto loadTask = javaList->getLoadTask();
    if (!loadTask)
    {
        // No task and not loaded: treat as an empty candidate list
        proceedWithCandidates();
        return;
    }
    connect(loadTask.get(), &Task::succeeded, this, &JavaMatchTask::listLoadSucceeded);
    connect(loadTask.get(), &Task::failed, this, &JavaMatchTask::listLoadFailed);
}

void JavaMatchTask::listLoadSucceeded()
{
    proceedWithCandidates();
}

void JavaMatchTask::listLoadFailed(QString reason)
{
    logWarning(tr("The Java installation scan failed: %1").arg(reason));
    // A failed scan is handled like "no compatible Java found"
    showNoMatchDialog();
}

void JavaMatchTask::proceedWithCandidates()
{
    auto javaList = APPLICATION->javalist();
    QList<JavaInstallPtr> candidates;
    for (int i = 0; i < javaList->count(); ++i)
    {
        auto install = std::dynamic_pointer_cast<JavaInstall>(javaList->at(i));
        if (install)
        {
            candidates.append(install);
        }
    }

    QList<JavaInstallPtr> matched = JavaMatch::filterCompatible(m_requiredMajor, candidates);
    if (matched.isEmpty())
    {
        showNoMatchDialog();
        return;
    }

    if (matched.size() == 1)
    {
        applyJavaPath(matched.first()->path);
        emitSucceeded();
        return;
    }

    // Multiple matches: let the user pick one
    JavaMatchSelectDialog dialog(matched, m_parentWidget);
    if (dialog.exec() == QDialog::Accepted && dialog.selectedJava())
    {
        applyJavaPath(dialog.selectedJava()->path);
        emitSucceeded();
    }
    else
    {
        emitFailed(tr("Launch cancelled: no Java installation was selected."));
    }
}

void JavaMatchTask::applyJavaPath(const QString &path)
{
    auto settings = m_inst->settings();
    settings->set("JavaPath", path);
    settings->set("OverrideJavaLocation", true);
    // Invalidate the cached Java info so CheckJava probes the new path again
    settings->set("JavaTimestamp", 0);
    settings->set("JavaVersion", "");
    settings->set("JavaArchitecture", "");
    settings->set("JavaVendor", "");
}

void JavaMatchTask::showNoMatchDialog()
{
    QString requirementMessage;
    switch (m_requiredMajor)
    {
        case 25:
            requirementMessage = tr("Minecraft 26.1 Snapshot 1 and above require the use of Java 25");
            break;
        case 21:
            requirementMessage = tr("Minecraft 24w14a and above require the use of Java 21");
            break;
        case 17:
            requirementMessage = tr("Minecraft 1.18 Pre Release 2 up to 24w13a require the use of Java 17");
            break;
        case 16:
            requirementMessage = tr("Minecraft 21w19a up to 1.18 Pre Release 1 require the use of Java 16");
            break;
        case 8:
        default:
            requirementMessage = tr("Minecraft 17w13a up to 21w18a require the use of Java 8");
            break;
    }

    QString text = tr("No compatible Java installation was found on this system.") + "<br/><br/>" +
                   requirementMessage + "<br/><br/>" +
                   tr("Please download and install Java %1 or newer from one of the following official sources:")
                       .arg(m_requiredMajor) +
                   "<br>" + tr("Adoptium: <a href=\"https://adoptium.net\">https://adoptium.net</a>") +
                   "<br>" + tr("Azul: <a href=\"https://www.azul.com/downloads\">https://www.azul.com/downloads</a>") +
                   "<br>" + tr("Oracle: <a href=\"https://www.oracle.com/java/technologies/downloads\">https://www.oracle.com/java/technologies/downloads</a>");

    auto messageBox = CustomMessageBox::selectable(
        m_parentWidget,
        tr("Java version requirement"),
        text,
        QMessageBox::Warning,
        QMessageBox::Ok
    );
    // Rich text enables the clickable links above; QMessageBox opens them
    // in the default browser automatically.
    messageBox->setTextFormat(Qt::RichText);
    messageBox->exec();

    emitFailed(tr("No compatible Java installation found. Java %1 or newer is required.").arg(m_requiredMajor));
}

/*
 * Copyright 2021 Jamie Mansfield <jmansfield@cadixdev.org>
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

#include "VerifyJavaInstall.h"

#include <launch/LaunchTask.h>
#include <minecraft/MinecraftInstance.h>
#include <minecraft/PackProfile.h>
#include <minecraft/VersionFilterData.h>

#ifdef major
    #undef major
#endif
#ifdef minor
    #undef minor
#endif

void VerifyJavaInstall::executeTask() {
    auto m_inst = std::dynamic_pointer_cast<MinecraftInstance>(m_parent->instance());

    auto javaVersion = m_inst->getJavaVersion();
    auto minecraftComponent = m_inst->getPackProfile()->getComponent("net.minecraft");

    int requiredMajor = requiredJavaMajor(minecraftComponent->getReleaseDateTime());
    if (requiredMajor > 0 && javaVersion.major() < requiredMajor) {
        QString message;
        switch (requiredMajor) {
            case 25:
                message = tr("Minecraft 26.1 Snapshot 1 and above require the use of Java 25");
                break;
            case 21:
                message = tr("Minecraft 24w14a and above require the use of Java 21");
                break;
            case 17:
                message = tr("Minecraft 1.18 Pre Release 2 up to 24w13a require the use of Java 17");
                break;
            case 16:
                message = tr("Minecraft 21w19a up to 1.18 Pre Release 1 require the use of Java 16");
                break;
            case 8:
            default:
                message = tr("Minecraft 17w13a up to 21w18a require the use of Java 8");
                break;
        }
        emit logLine(message, MessageLevel::Fatal);
        emitFailed(message);
        return;
    }

    emitSucceeded();
}

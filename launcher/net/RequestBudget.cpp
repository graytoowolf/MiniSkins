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

#include "RequestBudget.h"

#include <QDebug>

namespace Net
{
    RequestBudget::RequestBudget(QObject *parent) : QObject(parent)
    {
    }

    RequestBudget &RequestBudget::instance()
    {
        static RequestBudget budget;
        return budget;
    }

    void RequestBudget::setCapacity(int capacity)
    {
        const int clamped = qBound(1, capacity, 32);
        if (clamped == m_capacity)
        {
            return;
        }
        m_capacity = clamped;
        qDebug() << "RequestBudget: capacity" << m_capacity << "active" << m_active;
        grantWaiters();
    }

    void RequestBudget::acquire(const QObject *context, std::function<void()> onGranted)
    {
        if (!onGranted)
        {
            return;
        }
        if (m_active < m_capacity)
        {
            ++m_active;
            qDebug() << "RequestBudget: granted, active" << m_active << "capacity" << m_capacity;
            onGranted();
            return;
        }
        Waiter waiter;
        // stored only to detect destruction of the owner, so casting the const away is fine
        waiter.context = const_cast<QObject *>(context);
        waiter.onGranted = onGranted;
        m_waiting.enqueue(waiter);
        qDebug() << "RequestBudget: queued, active" << m_active << "capacity" << m_capacity << "waiting"
                 << m_waiting.size();
    }

    void RequestBudget::cancel(const QObject *context)
    {
        if (!context)
        {
            return;
        }
        const int before = m_waiting.size();
        for (int i = 0; i < m_waiting.size();)
        {
            const Waiter &waiter = m_waiting.at(i);
            if (waiter.context.isNull() || waiter.context.data() == context)
            {
                m_waiting.removeAt(i);
            }
            else
            {
                ++i;
            }
        }
        const int removed = before - m_waiting.size();
        if (removed > 0)
        {
            qDebug() << "RequestBudget: cancelled" << removed << "waiters, active" << m_active << "waiting"
                     << m_waiting.size();
        }
    }

    void RequestBudget::release()
    {
        if (m_active <= 0)
        {
            qDebug() << "RequestBudget: release without an active request";
            return;
        }
        --m_active;
        qDebug() << "RequestBudget: released, active" << m_active << "capacity" << m_capacity << "waiting"
                 << m_waiting.size();
        grantWaiters();
    }

    void RequestBudget::grantWaiters()
    {
        // A granted callback may release or acquire again; let the outermost loop do the granting
        // so the queue isn't drained recursively.
        if (m_granting)
        {
            return;
        }
        m_granting = true;
        while (m_active < m_capacity && !m_waiting.isEmpty())
        {
            const Waiter waiter = m_waiting.dequeue();
            if (waiter.context.isNull())
            {
                // the owner went away while it was waiting
                continue;
            }
            ++m_active;
            qDebug() << "RequestBudget: granted waiter, active" << m_active << "capacity" << m_capacity
                     << "waiting" << m_waiting.size();
            if (waiter.onGranted)
            {
                waiter.onGranted();
            }
        }
        m_granting = false;
    }
}

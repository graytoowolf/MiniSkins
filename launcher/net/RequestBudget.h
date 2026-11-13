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

#include <QObject>
#include <QPointer>
#include <QQueue>

#include <functional>

namespace Net
{
    /*
     * Process-wide request budget shared by regular downloads and ranged segment requests, so
     * both kinds of request draw from the same concurrency limit. Only used from the main thread.
     */
    class RequestBudget : public QObject
    {
        Q_OBJECT

    public:
        explicit RequestBudget(QObject *parent = nullptr);

        /// The process-wide budget, with the default capacity of 6 (NetJob's default Threads).
        static RequestBudget &instance();

        /// Clamped to 1..32. Shrinking the capacity doesn't interrupt active requests, it only
        /// limits new grants.
        void setCapacity(int capacity);
        int capacity() const
        {
            return m_capacity;
        }
        int activeCount() const
        {
            return m_active;
        }

        /// Runs onGranted as soon as a slot is free, or queues it (FIFO) until one is.
        void acquire(const QObject *context, std::function<void()> onGranted);
        /// Drops the callbacks of context that haven't been granted yet. Call it when the object
        /// dies or the request is cancelled.
        void cancel(const QObject *context);
        /// Frees one slot and grants the oldest waiters that fit the current capacity.
        void release();

    private:
        struct Waiter
        {
            // only kept to tell whether the object is still alive; never dereferenced
            QPointer<QObject> context;
            std::function<void()> onGranted;
        };

        void grantWaiters();

        int m_capacity = 6;
        int m_active = 0;
        bool m_granting = false;
        QQueue<Waiter> m_waiting;
    };
}

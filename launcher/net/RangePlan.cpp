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

#include "RangePlan.h"

#include <limits>

namespace
{
    bool parseNonNegative(const QByteArray &text, qint64 &value)
    {
        if (text.isEmpty())
        {
            return false;
        }
        qint64 result = 0;
        for (int i = 0; i < text.size(); ++i)
        {
            const char c = text.at(i);
            if (c < '0' || c > '9')
            {
                return false;
            }
            const int digit = c - '0';
            if (result > (std::numeric_limits<qint64>::max() - digit) / 10)
            {
                return false;
            }
            result = result * 10 + digit;
        }
        value = result;
        return true;
    }
}

namespace Net
{
    QVector<ByteRange> planSegments(qint64 totalSize, int segmentCount)
    {
        QVector<ByteRange> plan;
        if (totalSize <= 0 || segmentCount <= 0)
        {
            return plan;
        }
        const int count = segmentCount > totalSize ? static_cast<int>(totalSize) : segmentCount;
        const qint64 base = totalSize / count;
        const int remainder = static_cast<int>(totalSize % count);

        plan.reserve(count);
        qint64 start = 0;
        for (int i = 0; i < count; ++i)
        {
            const qint64 length = base + (i < remainder ? 1 : 0);
            ByteRange range;
            range.start = start;
            range.end = start + length - 1;
            plan.append(range);
            start += length;
        }
        return plan;
    }

    bool validatePlan(const QVector<ByteRange> &plan, qint64 totalSize)
    {
        if (plan.isEmpty() || totalSize <= 0)
        {
            return false;
        }
        qint64 expected = 0;
        for (const ByteRange &range : plan)
        {
            if (range.start < 0 || range.end < range.start)
            {
                return false;
            }
            // no gaps and no overlaps: each range has to start where the previous one ended
            if (range.start != expected)
            {
                return false;
            }
            if (range.end >= totalSize)
            {
                return false;
            }
            expected = range.end + 1;
        }
        return expected == totalSize;
    }

    ContentRange parseContentRange(const QByteArray &value)
    {
        ContentRange range;
        const QByteArray normalized = value.simplified();
        const int space = normalized.indexOf(' ');
        if (space <= 0 || normalized.left(space).toLower() != "bytes")
        {
            return range;
        }
        const QByteArray rest = normalized.mid(space + 1);
        const int slash = rest.indexOf('/');
        const int dash = rest.indexOf('-');
        if (slash <= 0 || dash <= 0 || dash > slash)
        {
            return range;
        }

        qint64 start = 0;
        qint64 end = 0;
        qint64 total = 0;
        if (!parseNonNegative(rest.left(dash).trimmed(), start))
        {
            return range;
        }
        if (!parseNonNegative(rest.mid(dash + 1, slash - dash - 1).trimmed(), end))
        {
            return range;
        }
        if (!parseNonNegative(rest.mid(slash + 1).trimmed(), total))
        {
            return range;
        }
        if (start > end || total <= 0)
        {
            return range;
        }

        range.start = start;
        range.end = end;
        range.total = total;
        range.valid = true;
        return range;
    }
}

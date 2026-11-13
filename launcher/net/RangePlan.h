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

#include <QByteArray>
#include <QVector>

namespace Net
{
    /// A closed byte range, as used in an HTTP Range header.
    struct ByteRange
    {
        qint64 start = 0;
        qint64 end = 0;

        qint64 length() const
        {
            return end - start + 1;
        }
    };

    /// Splits [0, totalSize) into segmentCount contiguous, non-overlapping ranges that cover all of
    /// it. Returns an empty vector for a non-positive size or count. The count is capped at the
    /// size, and the first totalSize % count ranges get one extra byte.
    QVector<ByteRange> planSegments(qint64 totalSize, int segmentCount);

    /// True when plan covers [0, totalSize) exactly once, in order, with no gaps or empty ranges.
    bool validatePlan(const QVector<ByteRange> &plan, qint64 totalSize);

    /// The parsed payload of a Content-Range response header. valid is false when parsing failed.
    struct ContentRange
    {
        qint64 start = -1;
        qint64 end = -1;
        qint64 total = -1;
        bool valid = false;
    };

    /// Parses "bytes 0-4194303/12582912". The unit is case-insensitive, whitespace around the
    /// value is allowed, and an unknown total ("*") or a malformed header gives valid == false.
    ContentRange parseContentRange(const QByteArray &value);
}

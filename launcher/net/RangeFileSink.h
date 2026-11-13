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

#include "FileSink.h"

#include <QFile>
#include <QString>

#include <memory>

namespace Net
{
    /*
     * File sink that can also fill a single target file out of order, one byte range at a time.
     *
     * Segmented writes go into a unique temporary file next to the target. Once every segment has
     * arrived, the temporary file is validated as a whole and atomically moved over the target.
     * Regular, sequential downloads keep using FileSink's QSaveFile path unchanged.
     */
    class RangeFileSink : public FileSink
    {
    public: /* con/des */
        explicit RangeFileSink(QString filename);
        ~RangeFileSink() override;

    public: /* methods */
        /// Creates the temporary file for totalSize bytes and starts accepting writes at offsets.
        JobStatus beginSegmentedWrite(qint64 totalSize);
        /// Writes data at offset inside the temporary file. Sets wroteAnyData on success.
        JobStatus writeAt(qint64 offset, QByteArray &data);
        /// Closes and removes the temporary file. Safe to call repeatedly and while inactive.
        void discardSegmentedWrite();
        bool isSegmentedWriteActive() const;

        JobStatus finalize(QNetworkReply &reply) override;
        JobStatus abort() override;

    private: /* methods */
        bool closeSegmentedWrite();
        bool replaySegmentedWrite();
        bool commitSegmentedWrite();

    private: /* data */
        std::unique_ptr<QFile> m_segment_file;
        QString m_segment_path;
        qint64 m_segment_size = 0;
        bool m_segment_active = false;
    };
}

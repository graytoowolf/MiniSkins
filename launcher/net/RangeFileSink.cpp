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

#include "RangeFileSink.h"

#include <QDebug>
#include <QFileInfo>
#include <QUuid>

#include "FileSystem.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cstdio>
#endif

namespace
{
    // Replace target with source. Both must live in the same directory so the switch is atomic.
    bool replaceTargetWith(const QString &source, const QString &target)
    {
#ifdef Q_OS_WIN
        if (!MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()),
                         reinterpret_cast<LPCWSTR>(target.utf16()),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            qCritical() << "Could not replace" << target << "with" << source << ", error"
                        << int(GetLastError());
            return false;
        }
        return true;
#else
        // Q_OS_UNIX and other POSIX-like platforms: rename() replaces the target atomically
        const QByteArray sourceName = QFile::encodeName(source);
        const QByteArray targetName = QFile::encodeName(target);
        if (::rename(sourceName.constData(), targetName.constData()) != 0)
        {
            qCritical() << "Could not replace" << target << "with" << source;
            return false;
        }
        return true;
#endif
    }
}

namespace Net
{
    RangeFileSink::RangeFileSink(QString filename) : FileSink(filename)
    {
        // nil
    }

    RangeFileSink::~RangeFileSink()
    {
        discardSegmentedWrite();
    }

    JobStatus RangeFileSink::beginSegmentedWrite(qint64 totalSize)
    {
        if (m_segment_active)
        {
            qCritical() << "Segmented write already in progress for" << m_filename;
            return Job_Failed;
        }
        if (totalSize < 0)
        {
            qCritical() << "Refusing segmented write of" << totalSize << "bytes for" << m_filename;
            return Job_Failed;
        }
        if (!FS::ensureFilePathExists(m_filename))
        {
            qCritical() << "Could not create folder for" << m_filename;
            return Job_Failed;
        }

        const QFileInfo targetInfo(m_filename);
        const QString uniqueSuffix =
            QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'));
        m_segment_path = FS::PathCombine(targetInfo.absolutePath(),
                                         targetInfo.fileName() + QStringLiteral(".part-") + uniqueSuffix);

        std::unique_ptr<QFile> segmentFile(new QFile(m_segment_path));
        if (!segmentFile->open(QIODevice::ReadWrite))
        {
            qCritical() << "Could not open" << m_segment_path << "for writing";
            QFile::remove(m_segment_path);
            m_segment_path.clear();
            return Job_Failed;
        }
        if (!segmentFile->resize(totalSize))
        {
            qCritical() << "Could not resize" << m_segment_path << "to" << totalSize << "bytes";
            segmentFile->close();
            segmentFile.reset();
            QFile::remove(m_segment_path);
            m_segment_path.clear();
            return Job_Failed;
        }

        m_segment_file = std::move(segmentFile);
        m_segment_size = totalSize;
        m_segment_active = true;
        return Job_InProgress;
    }

    JobStatus RangeFileSink::writeAt(qint64 offset, QByteArray &data)
    {
        if (!m_segment_active || !m_segment_file || !m_segment_file->isOpen())
        {
            qCritical() << "No segmented write in progress for" << m_filename;
            return Job_Failed;
        }
        if (offset < 0 || offset > m_segment_size || data.size() > m_segment_size - offset)
        {
            qCritical() << "Segmented write out of bounds for" << m_filename << "at" << offset << "with"
                        << data.size() << "bytes";
            return Job_Failed;
        }
        if (data.isEmpty())
        {
            return Job_InProgress;
        }
        if (!m_segment_file->seek(offset) || m_segment_file->write(data) != data.size())
        {
            qCritical() << "Failed writing segment into" << m_segment_path;
            return Job_Failed;
        }
        wroteAnyData = true;
        return Job_InProgress;
    }

    void RangeFileSink::discardSegmentedWrite()
    {
        if (m_segment_file)
        {
            if (m_segment_file->isOpen())
            {
                m_segment_file->close();
            }
            m_segment_file.reset();
        }
        if (!m_segment_path.isEmpty())
        {
            if (QFile::exists(m_segment_path) && !QFile::remove(m_segment_path))
            {
                qWarning() << "Could not remove temporary file" << m_segment_path;
            }
            m_segment_path.clear();
        }
        m_segment_size = 0;
        m_segment_active = false;
    }

    bool RangeFileSink::isSegmentedWriteActive() const
    {
        return m_segment_active;
    }

    bool RangeFileSink::closeSegmentedWrite()
    {
        if (!m_segment_file || !m_segment_file->isOpen())
        {
            return true;
        }
        if (!m_segment_file->flush())
        {
            qCritical() << "Could not flush" << m_segment_path;
            return false;
        }
        m_segment_file->close();

        const qint64 actualSize = QFileInfo(m_segment_path).size();
        if (actualSize != m_segment_size)
        {
            qCritical() << "Segmented file" << m_segment_path << "is" << actualSize << "bytes, expected"
                        << m_segment_size;
            return false;
        }
        return true;
    }

    bool RangeFileSink::replaySegmentedWrite()
    {
        QFile input(m_segment_path);
        if (!input.open(QIODevice::ReadOnly))
        {
            qCritical() << "Could not reopen" << m_segment_path << "for validation";
            return false;
        }

        // feed the whole file through the validators again, in chunks, so checksums and size limits
        // are computed over the assembled file instead of the individual response bodies
        const qint64 chunkSize = 1024 * 1024;
        qint64 remaining = m_segment_size;
        while (remaining > 0)
        {
            QByteArray chunk = input.read(qMin(chunkSize, remaining));
            if (chunk.isEmpty())
            {
                qCritical() << "Unexpected end of" << m_segment_path << "while validating";
                return false;
            }
            if (!writeAllValidators(chunk))
            {
                qCritical() << "Validator refused" << m_segment_path;
                return false;
            }
            remaining -= chunk.size();
        }
        return true;
    }

    bool RangeFileSink::commitSegmentedWrite()
    {
        if (!replaceTargetWith(m_segment_path, m_filename))
        {
            return false;
        }
        return true;
    }

    JobStatus RangeFileSink::finalize(QNetworkReply &reply)
    {
        if (!m_segment_active)
        {
            return FileSink::finalize(reply);
        }

        if (!closeSegmentedWrite())
        {
            discardSegmentedWrite();
            return Job_Failed;
        }
        if (!replaySegmentedWrite())
        {
            discardSegmentedWrite();
            return Job_Failed;
        }
        if (!finalizeAllValidators(reply))
        {
            qCritical() << "Segmented download failed validation for" << m_filename;
            discardSegmentedWrite();
            return Job_Failed;
        }
        if (!commitSegmentedWrite())
        {
            discardSegmentedWrite();
            return Job_Failed;
        }

        // the temporary file has become the target file, so it must not be discarded anymore
        m_segment_file.reset();
        m_segment_path.clear();
        m_segment_size = 0;
        m_segment_active = false;

        return finalizeCache(reply);
    }

    JobStatus RangeFileSink::abort()
    {
        discardSegmentedWrite();
        return FileSink::abort();
    }
}

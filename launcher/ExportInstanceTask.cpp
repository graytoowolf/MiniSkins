#include "ExportInstanceTask.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <quazip.h>
#include <quazipfile.h>
#include <JlCompress.h>
#include <QtConcurrentRun>

ExportInstanceTask::ExportInstanceTask(InstancePtr instance, const QString &output,
                                       const QString &prefix,
                                       const SeparatorPrefixTree<'/'> &blocked)
    : m_instance(instance), m_output(output), m_prefix(prefix), m_blocked(blocked), m_aborted(false)
{
}

void ExportInstanceTask::executeTask()
{
    setStatus(tr("Scanning files..."));

    m_compressFuture = QtConcurrent::run(QThreadPool::globalInstance(), [this]() -> bool {
        return compressDir();
    });

    connect(&m_compressFutureWatcher, &QFutureWatcher<bool>::finished, this, &ExportInstanceTask::compressFinished);
    m_compressFutureWatcher.setFuture(m_compressFuture);
}

bool ExportInstanceTask::abort()
{
    m_aborted = true;
    if (m_compressFuture.isRunning())
    {
        m_compressFuture.cancel();
    }
    return true;
}

void ExportInstanceTask::compressFinished()
{
    if (m_aborted)
    {
        emitAborted();
        return;
    }

    auto result = m_compressFuture.result();
    if (!result)
    {
        emitFailed(tr("Failed to export instance"));
        return;
    }
    emitSucceeded();
}

bool ExportInstanceTask::compressDir()
{
    QDir dir(m_instance->instanceRoot());

    QFileInfoList files;
    QDirIterator it(dir.absolutePath(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        if (m_aborted)
        {
            return false;
        }
        it.next();
        QFileInfo info = it.fileInfo();
        if (info.absoluteFilePath() == m_output)
        {
            continue;
        }
        QString relPath = dir.relativeFilePath(info.absoluteFilePath());
        if (m_blocked.covers(relPath))
        {
            continue;
        }
        files.append(info);
    }

    if (files.isEmpty())
    {
        return true;
    }

    QuaZip zip(m_output);
    zip.setFileNameCodec("UTF-8");
    QDir().mkpath(QFileInfo(m_output).absolutePath());
    if (!zip.open(QuaZip::mdCreate))
    {
        QFile::remove(m_output);
        return false;
    }

    int total = files.size();
    int current = 0;

    QDirIterator dirIt(dir.absolutePath(), QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (dirIt.hasNext())
    {
        if (m_aborted)
        {
            zip.close();
            QFile::remove(m_output);
            return false;
        }
        dirIt.next();
        QString relPath = dir.relativeFilePath(dirIt.filePath());
        if (m_blocked.covers(relPath))
        {
            continue;
        }
        QString dirEntry;
        if (m_prefix.isEmpty())
        {
            dirEntry = relPath + "/";
        }
        else
        {
            dirEntry = m_prefix + "/" + relPath + "/";
        }
        QuaZipFile dirZipFile(&zip);
        if (!dirZipFile.open(QIODevice::WriteOnly, QuaZipNewInfo(dirEntry, dirIt.filePath()), 0, 0, 0))
        {
            zip.close();
            QFile::remove(m_output);
            return false;
        }
        dirZipFile.close();
    }

    for (const auto &fileInfo : files)
    {
        if (m_aborted)
        {
            zip.close();
            QFile::remove(m_output);
            return false;
        }

        QString filename = dir.relativeFilePath(fileInfo.absoluteFilePath());
        if (m_prefix.size())
        {
            filename = m_prefix + "/" + filename;
        }

        QFile inFile;
        inFile.setFileName(fileInfo.absoluteFilePath());
        if (!inFile.open(QIODevice::ReadOnly))
        {
            zip.close();
            QFile::remove(m_output);
            return false;
        }

        QuaZipFile outFile(&zip);
        if (!outFile.open(QIODevice::WriteOnly, QuaZipNewInfo(filename, inFile.fileName())))
        {
            inFile.close();
            zip.close();
            QFile::remove(m_output);
            return false;
        }

        if (!JlCompress::copyData(inFile, outFile) || outFile.getZipError() != UNZ_OK)
        {
            inFile.close();
            outFile.close();
            zip.close();
            QFile::remove(m_output);
            return false;
        }

        outFile.close();
        if (outFile.getZipError() != UNZ_OK)
        {
            inFile.close();
            zip.close();
            QFile::remove(m_output);
            return false;
        }
        inFile.close();

        current++;
        setProgress(current, total);
        setStatus(tr("Compressing %1").arg(fileInfo.fileName()));
    }

    zip.close();
    if (zip.getZipError() != 0)
    {
        QFile::remove(m_output);
        return false;
    }
    return true;
}

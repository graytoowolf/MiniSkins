#include "FileSink.h"
#include <QFile>
#include <QFileInfo>
#include "FileSystem.h"

namespace Net {

FileSink::FileSink(QString filename)
    :m_filename(filename)
{
    // nil
}

FileSink::~FileSink()
{
    // nil
}

JobStatus FileSink::init(QNetworkRequest& request)
{
    auto result = initCache(request);
    if(result != Job_InProgress)
    {
        return result;
    }
    wroteAnyData = false;

    if(initAllValidators(request))
        return Job_InProgress;
    return Job_Failed;
}

JobStatus FileSink::initCache(QNetworkRequest &)
{
    return Job_InProgress;
}

bool FileSink::openOutputFile()
{
    if (m_output_file)
    {
        return true;
    }
    if (!FS::ensureFilePathExists(m_filename))
    {
        qCritical() << "Could not create folder for " + m_filename;
        return false;
    }
    m_output_file.reset(new QSaveFile(m_filename));
    if (!m_output_file->open(QIODevice::WriteOnly))
    {
        qCritical() << "Could not open " + m_filename + " for writing";
        m_output_file.reset();
        return false;
    }
    return true;
}

JobStatus FileSink::write(QByteArray& data)
{
    if (!openOutputFile() || !writeAllValidators(data) || m_output_file->write(data) != data.size())
    {
        qCritical() << "Failed writing into " + m_filename;
        if (m_output_file)
        {
            m_output_file->cancelWriting();
        }
        m_output_file.reset();
        wroteAnyData = false;
        return Job_Failed;
    }
    wroteAnyData = true;
    return Job_InProgress;
}

JobStatus FileSink::abort()
{
    if (m_output_file)
    {
        m_output_file->cancelWriting();
        m_output_file.reset();
    }
    failAllValidators();
    return Job_Failed;
}

JobStatus FileSink::finalize(QNetworkReply& reply)
{
    bool gotFile = false;
    QVariant statusCodeV = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute);
    bool validStatus = false;
    int statusCode = statusCodeV.toInt(&validStatus);
    if(validStatus)
    {
        // this leaves out 304 Not Modified
        gotFile = statusCode == 200 || statusCode == 203;
    }
    // if we wrote any data to the save file, we try to commit the data to the real file.
    // if it actually got a proper file, we write it even if it was empty
    if (gotFile || wroteAnyData)
    {
        if (!m_output_file && !openOutputFile())
            return Job_Failed;
        // ask validators for data consistency
        // we only do this for actual downloads, not 'your data is still the same' cache hits
        if(!finalizeAllValidators(reply))
        {
            m_output_file->cancelWriting();
            m_output_file.reset();
            return Job_Failed;
        }
        // nothing went wrong...
        if (!m_output_file->commit())
        {
            qCritical() << "Failed to commit changes to " << m_filename;
            m_output_file->cancelWriting();
            m_output_file.reset();
            return Job_Failed;
        }
    }
    // then get rid of the save file
    m_output_file.reset();

    return finalizeCache(reply);
}

JobStatus FileSink::finalizeCache(QNetworkReply &)
{
    return Job_Finished;
}

bool FileSink::hasLocalData()
{
    QFileInfo info(m_filename);
    return info.exists() && info.size() != 0;
}
}

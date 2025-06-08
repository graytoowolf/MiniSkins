#include <iostream>
#include <vector>
#include "fingerprint.h"
#include <QString>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <QMap>
#include "Application.h"

namespace fingerprint
{

  Buffer get_jar_contents(const char *jar_file_path)
  {
    int result;
    FILE *jar_file = fopen(jar_file_path, "rb");
    if (jar_file == nullptr)
    {
      return Buffer();
    }

    long buffer_size = get_file_size(jar_file);

    Buffer buffer(buffer_size);
    result = fread(buffer.data(), 1, buffer_size, jar_file);

    if (result != buffer_size)
    {
      std::cout << "Failed to load " << jar_file_path << std::endl;
    }

    fclose(jar_file);

    return buffer;
  }

  long get_file_size(FILE *file)
  {
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    return size;
  }

  uint32_t compute_hash(Buffer &buffer)
  {
    const uint32_t multiplex = 1540483477;
    const uint32_t length = buffer.size();
    uint32_t num1 = length;

    num1 = compute_normalized_length(buffer);

    uint32_t num2 = (uint32_t)1 ^ num1;
    uint32_t num3 = 0;
    uint32_t num4 = 0;

    for (uint32_t index = 0; index < length; ++index)
    {

      unsigned char b = buffer[index];

      if (!is_whitespace_character(b))
      {
        num3 |= (uint32_t)b << num4;
        num4 += 8;
        if (num4 == 32)
        {
          uint32_t num6 = num3 * multiplex;

          uint32_t num7 = (num6 ^ num6 >> 24) * multiplex;

          num2 = num2 * multiplex ^ num7;
          num3 = 0;
          num4 = 0;
        }
      }
    }

    if (num4 > 0)
    {
      num2 = (num2 ^ num3) * multiplex;
    }

    uint32_t num6 = (num2 ^ num2 >> 13) * multiplex;

    return num6 ^ num6 >> 15;
  }

  uint32_t compute_normalized_length(Buffer &buffer)
  {
    int32_t num1 = 0;
    const uint32_t length = buffer.size();

    for (uint32_t index = 0; index < length; ++index)
    {
      if (!is_whitespace_character(buffer[index]))
      {
        ++num1;
      }
    }

    return num1;
  }

  bool is_whitespace_character(char b)
  {
    return b == 9 || b == 10 || b == 13 || b == 32;
  }

  QString getJarFingerprint(const QString &jarPath)
  {
    Buffer contents = get_jar_contents(jarPath.toStdString().c_str());
    if (!contents.empty())
    {
      uint32_t hash = compute_hash(contents);
      return QString::number(hash);
    }
    return QString();
  }

  ModInfo processModInfo(const ModInfo &modInfo)
  {
    ModInfo result = modInfo;

    // 获取文件指纹
    result.fileFingerprint = getJarFingerprint(result.filePath);
    if (result.fileFingerprint.isEmpty())
    {
      return result; // 返回带有文件路径但无效的ModInfo
    }

    // 构建请求 JSON
    QJsonObject requestObj;
    QJsonArray fingerprintArray;

    bool ok;
    qlonglong fingerprintValue = result.fileFingerprint.toLongLong(&ok);
    if (!ok)
    {
      return result;
    }

    fingerprintArray.append(QJsonValue(fingerprintValue));
    requestObj["fingerprints"] = fingerprintArray;

    QJsonDocument doc{requestObj};
    QByteArray data = doc.toJson();

    // 创建网络请求
    const QString CURSEFORGE_API_URL = "https://api.curseforge.com/v1/fingerprints";
    QNetworkRequest request{QUrl(CURSEFORGE_API_URL)};
    request.setRawHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // 发送同步请求
    auto *reply = APPLICATION->network()->post(request, data);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(10000); // 10秒超时

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start();
    loop.exec();

    if (timer.isActive())
    {
      timer.stop();

      if (reply->error() == QNetworkReply::NoError)
      {
        QByteArray responseData = reply->readAll();
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData);
        QJsonObject root = responseDoc.object();

        if (root.contains("data"))
        {
          QJsonObject data = root["data"].toObject();
          if (data.contains("exactMatches"))
          {
            QJsonArray matches = data["exactMatches"].toArray();
            if (!matches.isEmpty())
            {
              QJsonObject match = matches[0].toObject();
              result.projectId = match["id"].toInt();

              // 获取文件信息
              if (match.contains("file"))
              {
                QJsonObject fileObj = match["file"].toObject();
                result.fileId = fileObj["id"].toInt();
                result.name = fileObj["fileName"].toString();

                // 从响应中获取文件指纹（如果存在）
                if (fileObj.contains("fileFingerprint"))
                {
                  result.fileFingerprint = QString::number(fileObj["fileFingerprint"].toVariant().toLongLong());
                }

                result.isValid = true;
              }
            }
          }
        }
      }
    }

    reply->deleteLater();
    return result;
  }

  QList<ModInfo> processModInfoList(const QList<ModInfo> &modInfoList)
  {
    QList<ModInfo> results;
    if (modInfoList.isEmpty())
    {
      return results;
    }

    // 首先为所有ModInfo获取文件指纹
    QList<ModInfo> processedList;
    QJsonArray fingerprintArray;
    QMap<qlonglong, int> fingerprintToIndex; // 用于映射指纹到结果列表索引

    for (int i = 0; i < modInfoList.size(); ++i)
    {
      ModInfo modInfo = modInfoList[i];
      modInfo.fileFingerprint = getJarFingerprint(modInfo.filePath);
      processedList.append(modInfo);

      if (!modInfo.fileFingerprint.isEmpty())
      {
        bool ok;
        qlonglong fingerprintValue = modInfo.fileFingerprint.toLongLong(&ok);
        if (ok)
        {
          fingerprintArray.append(QJsonValue(fingerprintValue));
          fingerprintToIndex[fingerprintValue] = i;
        }
      }
    }

    // 如果没有有效的指纹，返回只包含文件路径和指纹的列表
    if (fingerprintArray.isEmpty())
    {
      return processedList;
    }

    // 构建请求 JSON
    QJsonObject requestObj;
    requestObj["fingerprints"] = fingerprintArray;

    QJsonDocument doc{requestObj};
    QByteArray data = doc.toJson();

    // 创建网络请求
    const QString CURSEFORGE_API_URL = "https://api.curseforge.com/v1/fingerprints";
    QNetworkRequest request{QUrl(CURSEFORGE_API_URL)};
    request.setRawHeader("x-api-key", APPLICATION->curseAPIKey().toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // 发送同步请求
    auto *reply = APPLICATION->network()->post(request, data);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(15000); // 15秒超时，因为处理多个文件可能需要更长时间

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start();
    loop.exec();

    results = processedList; // 先复制所有已处理的ModInfo

    if (timer.isActive())
    {
      timer.stop();

      if (reply->error() == QNetworkReply::NoError)
      {
        QByteArray responseData = reply->readAll();
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData);
        QJsonObject root = responseDoc.object();

        if (root.contains("data"))
        {
          QJsonObject data = root["data"].toObject();
          if (data.contains("exactMatches"))
          {
            QJsonArray matches = data["exactMatches"].toArray();

            for (const QJsonValue &matchValue : matches)
            {
              QJsonObject match = matchValue.toObject();

              // 获取文件信息
              if (match.contains("file"))
              {
                QJsonObject fileObj = match["file"].toObject();

                // 通过文件指纹找到对应的ModInfo
                if (fileObj.contains("fileFingerprint"))
                {
                  qlonglong responseFingerprint = fileObj["fileFingerprint"].toVariant().toLongLong();

                  if (fingerprintToIndex.contains(responseFingerprint))
                  {
                    int index = fingerprintToIndex[responseFingerprint];

                    // 更新对应的ModInfo
                    results[index].projectId = match["id"].toInt();
                    results[index].fileId = fileObj["id"].toInt();
                    results[index].name = fileObj["fileName"].toString();
                    results[index].fileFingerprint = QString::number(responseFingerprint);
                    results[index].isValid = true;
                  }
                }
              }
            }
          }
        }
      }
    }

    reply->deleteLater();
    return results;
  }

 }

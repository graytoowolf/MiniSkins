#ifndef DOWNLOADSOURCE_H
#define DOWNLOADSOURCE_H

#include <QString>

namespace DownloadSourceDefaults
{
    // 内置 BMCLAPI 源地址：集中定义，避免同一域名在多个文件中重复硬编码
    inline QString bmclapiHost()
    {
        return QStringLiteral("bmclapi2.bangbang93.com");
    }
}

class DownloadSource
{
public:
    QString name;
    QString url;
    QString type;
    bool proxy;

    DownloadSource();
    DownloadSource(const QString &name, const QString &url, const QString &type, bool proxy);

    QString getName() const;
    QString getUrl() const;
    QString getType() const;
    bool isProxy() const;

    void setName(const QString &newName);
    void setUrl(const QString &newUrl);
    void setType(const QString &newType);
    void setProxy(bool newProxy);
};

class YggSource
{
public:
    QString name;
    QString url;

    YggSource();
    YggSource(const QString &name, const QString &url);

    QString getName() const;
    QString getUrl() const;

    void setName(const QString &newName);
    void setUrl(const QString &newUrl);
};

#endif // DOWNLOADSOURCE_H

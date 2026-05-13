#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariant>
#include <QTimer>

#include <Application.h>

class AIAnalyzer : public QObject
{
    Q_OBJECT
public:
    struct ModelConfig
    {
        QString apiUrl;
        QString apiKey;
        QString modelId;

        QJsonObject toJson() const
        {
            QJsonObject obj;
            obj["apiUrl"] = apiUrl;
            obj["apiKey"] = obfuscateKey(apiKey);
            obj["modelId"] = modelId;
            return obj;
        }

        static ModelConfig fromJson(const QJsonObject &obj)
        {
            ModelConfig cfg;
            cfg.apiUrl = obj["apiUrl"].toString();
            cfg.apiKey = deobfuscateKey(obj["apiKey"].toString());
            cfg.modelId = obj["modelId"].toString();
            return cfg;
        }
    };

    explicit AIAnalyzer(QObject *parent = nullptr);
    ~AIAnalyzer();

    static QList<ModelConfig> loadModels();
    static void saveModels(const QList<ModelConfig> &models);
    static ModelConfig getDefaultModel();
    static void setDefaultModel(const QString &modelId);

    static QString obfuscateKey(const QString &key);
    static QString deobfuscateKey(const QString &obfuscated);

    void analyze(const QString &logContent, const ModelConfig &model);
    void cancel();
    bool isAnalyzing() const;

signals:
    void analysisFinished(const QString &result);
    void analysisError(const QString &error);

private slots:
    void onReplyFinished();
    void onRetryTimeout();

private:
    bool isImportantLine(const QString &line);
    QString compressStackTrace(const QStringList &lines);
    QString preprocessLogContent(const QString &logContent);
    QString buildSystemPrompt() const;
    void sendRequest();

private:
    QNetworkReply *m_reply;
    QString m_logContent;
    ModelConfig m_currentModel;
    int m_retryCount;
    QTimer *m_retryTimer;
};

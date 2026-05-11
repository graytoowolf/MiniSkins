#include "AIAnalyzer.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QByteArray>
#include <QDateTime>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>

static const char *OBfuscATION_SEED = "MiniSkinsAIKeyObfuscation2026";
static const int MAX_RETRIES = 3;
static const int RETRY_DELAY_MS = 5000;

QString AIAnalyzer::obfuscateKey(const QString &key)
{
    if (key.isEmpty())
        return QString();

    QByteArray keyBytes = key.toUtf8();
    QByteArray seed = QByteArray(OBfuscATION_SEED);
    QByteArray result(keyBytes.size(), 0);

    for (int i = 0; i < keyBytes.size(); ++i)
    {
        result[i] = keyBytes[i] ^ seed[i % seed.size()];
    }

    return QString::fromLatin1(result.toBase64());
}

QString AIAnalyzer::deobfuscateKey(const QString &obfuscated)
{
    if (obfuscated.isEmpty())
        return QString();

    QByteArray decoded = QByteArray::fromBase64(obfuscated.toLatin1());
    QByteArray seed = QByteArray(OBfuscATION_SEED);
    QByteArray result(decoded.size(), 0);

    for (int i = 0; i < decoded.size(); ++i)
    {
        result[i] = decoded[i] ^ seed[i % seed.size()];
    }

    return QString::fromUtf8(result);
}

QList<AIAnalyzer::ModelConfig> AIAnalyzer::loadModels()
{
    auto s = APPLICATION->settings();
    QString raw = s->get("AIModels").toString();

    QByteArray jsonBytes = QByteArray::fromBase64(raw.toLatin1());
    if (jsonBytes.isEmpty())
    {
        jsonBytes = raw.toUtf8();
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
    {
        QList<ModelConfig> defaults;
        defaults.append(ModelConfig::createDefault());
        return defaults;
    }

    QJsonArray arr = doc.array();
    QList<ModelConfig> models;
    for (const QJsonValue &val : arr)
    {
        if (val.isObject())
        {
            ModelConfig cfg = ModelConfig::fromJson(val.toObject());
            if (!cfg.modelId.isEmpty())
            {
                models.append(cfg);
            }
        }
    }

    if (models.isEmpty())
    {
        models.append(ModelConfig::createDefault());
    }

    return models;
}

void AIAnalyzer::saveModels(const QList<ModelConfig> &models)
{
    QJsonArray arr;
    for (const ModelConfig &cfg : models)
    {
        arr.append(cfg.toJson());
    }
    QJsonDocument doc(arr);
    auto s = APPLICATION->settings();
    s->set("AIModels", QString::fromLatin1(doc.toJson(QJsonDocument::Compact).toBase64()));
}

AIAnalyzer::ModelConfig AIAnalyzer::getDefaultModel()
{
    auto s = APPLICATION->settings();
    QString defaultId = s->get("AIDefaultModel").toString();

    QList<ModelConfig> models = loadModels();
    for (const ModelConfig &cfg : models)
    {
        if (cfg.modelId == defaultId)
        {
            return cfg;
        }
    }

    if (!models.isEmpty())
    {
        return models.first();
    }

    return ModelConfig::createDefault();
}

void AIAnalyzer::setDefaultModel(const QString &modelId)
{
    auto s = APPLICATION->settings();
    s->set("AIDefaultModel", modelId);
}

AIAnalyzer::AIAnalyzer(QObject *parent)
    : QObject(parent), m_reply(nullptr), m_retryCount(0), m_retryTimer(nullptr)
{
}

AIAnalyzer::~AIAnalyzer()
{
    cancel();
}

bool AIAnalyzer::isAnalyzing() const
{
    return m_reply != nullptr || (m_retryTimer && m_retryTimer->isActive());
}

bool AIAnalyzer::isImportantLine(const QString &line)
{
    if (line.contains("Exception", Qt::CaseInsensitive) ||
        line.contains("Error", Qt::CaseInsensitive) ||
        line.contains("FATAL", Qt::CaseInsensitive) ||
        line.contains("Caused by", Qt::CaseInsensitive) ||
        line.contains("at ", Qt::CaseInsensitive) ||
        line.contains("... ", Qt::CaseInsensitive) ||
        line.startsWith("java.", Qt::CaseInsensitive) ||
        line.startsWith("net.minecraft", Qt::CaseInsensitive) ||
        line.startsWith("org.lwjgl", Qt::CaseInsensitive) ||
        line.startsWith("cpw.mods", Qt::CaseInsensitive) ||
        line.startsWith("net.minecraftforge", Qt::CaseInsensitive) ||
        line.startsWith("fabric", Qt::CaseInsensitive) ||
        line.startsWith("com.mojang", Qt::CaseInsensitive) ||
        line.contains("crash", Qt::CaseInsensitive) ||
        line.contains("failed", Qt::CaseInsensitive) ||
        line.contains("failed to", Qt::CaseInsensitive) ||
        line.contains("cannot", Qt::CaseInsensitive) ||
        line.contains("could not", Qt::CaseInsensitive) ||
        line.contains("Stacktrace", Qt::CaseInsensitive) ||
        line.contains("---- Minecraft Crash Report", Qt::CaseInsensitive) ||
        line.contains("Description:", Qt::CaseInsensitive) ||
        line.contains("-- ") ||
        line.contains("Details:", Qt::CaseInsensitive) ||
        line.contains("System Details", Qt::CaseInsensitive) ||
        line.contains("Mod List", Qt::CaseInsensitive) ||
        line.contains("Launcher", Qt::CaseInsensitive) ||
        line.contains("Java Version", Qt::CaseInsensitive) ||
        line.contains("Memory", Qt::CaseInsensitive) ||
        line.contains("JVM Flags", Qt::CaseInsensitive) ||
        line.contains("Operating System", Qt::CaseInsensitive))
    {
        return true;
    }
    return false;
}

QString AIAnalyzer::compressStackTrace(const QStringList &lines)
{
    QStringList result;
    QString currentPackage;
    int packageCount = 0;
    const int maxPackageLines = 3;

    for (const QString &line : lines)
    {
        QString trimmed = line.trimmed();

        if (!trimmed.startsWith("at ") && !trimmed.startsWith("..."))
        {
            if (packageCount > maxPackageLines)
            {
                result.append(QString("    ... %1 more frames from %2").arg(packageCount - maxPackageLines).arg(currentPackage));
            }
            result.append(line);
            currentPackage.clear();
            packageCount = 0;
            continue;
        }

        static const QRegularExpression packageRegex("at ([a-zA-Z0-9_.]+)");
        QRegularExpressionMatch match = packageRegex.match(trimmed);
        QString package;

        if (match.hasMatch())
        {
            QString fullPackage = match.captured(1);
            int lastDot = fullPackage.lastIndexOf('.');
            if (lastDot > 0)
            {
                int secondLastDot = fullPackage.lastIndexOf('.', lastDot - 1);
                if (secondLastDot > 0)
                {
                    package = fullPackage.left(secondLastDot);
                }
                else
                {
                    package = fullPackage.left(lastDot);
                }
            }
        }

        if (package != currentPackage)
        {
            if (packageCount > maxPackageLines)
            {
                result.append(QString("    ... %1 more frames from %2").arg(packageCount - maxPackageLines).arg(currentPackage));
            }
            currentPackage = package;
            packageCount = 0;
        }

        packageCount++;
        if (packageCount <= maxPackageLines)
        {
            result.append(line);
        }
    }

    if (packageCount > maxPackageLines)
    {
        result.append(QString("    ... %1 more frames from %2").arg(packageCount - maxPackageLines).arg(currentPackage));
    }

    return result.join('\n');
}

QString AIAnalyzer::preprocessLogContent(const QString &logContent)
{
    QStringList lines = logContent.split('\n');
    QStringList result;
    QSet<QString> seenLines;
    bool inStackTrace = false;
    QStringList stackTraceBuffer;

    static const QRegularExpression timestampRegex("^(\\[\\d{2}:\\d{2}:\\d{2}\\]\\s*)?(\\[[\\w/]+\\]\\s*)?(\\[(?:FATAL|ERROR|WARN|INFO|DEBUG|TRACE)\\]\\s*)?");
    static const QRegularExpression duplicateSpacesRegex(" {2,}");

    for (const QString &originalLine : lines)
    {
        QString line = originalLine;
        line.replace(timestampRegex, "");
        line.replace(duplicateSpacesRegex, " ");
        line = line.trimmed();

        if (line.isEmpty())
        {
            if (!result.isEmpty() && !result.last().isEmpty())
            {
                result.append(QString());
            }
            continue;
        }

        bool isStackTraceLine = line.startsWith("at ") || line.startsWith("... ") || line.contains("Stacktrace");

        if (isStackTraceLine)
        {
            if (!inStackTrace)
            {
                inStackTrace = true;
                stackTraceBuffer.clear();
            }
            stackTraceBuffer.append(originalLine.trimmed());
            continue;
        }
        else
        {
            if (inStackTrace)
            {
                QString compressed = compressStackTrace(stackTraceBuffer);
                result.append(compressed);
                stackTraceBuffer.clear();
                inStackTrace = false;
            }
        }

        if (!isImportantLine(originalLine))
        {
            continue;
        }

        QString simplified = line;
        simplified.replace(QRegularExpression("\\s+"), " ");

        if (seenLines.contains(simplified) && !line.contains("Caused by", Qt::CaseInsensitive))
        {
            continue;
        }
        seenLines.insert(simplified);

        result.append(originalLine.trimmed());
    }

    if (inStackTrace && !stackTraceBuffer.isEmpty())
    {
        QString compressed = compressStackTrace(stackTraceBuffer);
        result.append(compressed);
    }

    QString finalResult = result.join('\n');

    static const QRegularExpression multiBlankRegex("\n{3,}");
    finalResult.replace(multiBlankRegex, "\n\n");

    const int maxChars = 50000;
    if (finalResult.size() > maxChars)
    {
        int firstPartEnd = finalResult.indexOf('\n', maxChars / 2);
        if (firstPartEnd == -1) firstPartEnd = maxChars / 2;

        int lastPartStart = finalResult.lastIndexOf('\n', finalResult.size() - maxChars / 2);
        if (lastPartStart == -1) lastPartStart = finalResult.size() - maxChars / 2;

        finalResult = finalResult.left(firstPartEnd) +
                      "\n\n... [Log compressed and truncated] ...\n\n" +
                      finalResult.mid(lastPartStart);
    }

    return finalResult.trimmed();
}

void AIAnalyzer::analyze(const QString &logContent, const ModelConfig &model)
{
    if (m_reply)
    {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    if (m_retryTimer)
    {
        m_retryTimer->stop();
        delete m_retryTimer;
        m_retryTimer = nullptr;
    }

    if (model.apiKey.isEmpty())
    {
        emit analysisError(tr("Please configure API key in Settings > AI Analysis"));
        return;
    }

    if (model.apiUrl.isEmpty() || model.modelId.isEmpty())
    {
        emit analysisError(tr("Please configure AI model in Settings > AI Analysis"));
        return;
    }

    m_logContent = preprocessLogContent(logContent);
    m_currentModel = model;
    m_retryCount = 0;

    sendRequest();
}

void AIAnalyzer::sendRequest()
{
    QString systemPrompt = buildSystemPrompt();

    QJsonObject requestBody;
    requestBody["model"] = m_currentModel.modelId;

    QJsonArray messages;

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = systemPrompt + "\n\n---\n\nCrash Log:\n" + m_logContent;
    messages.append(userMsg);

    requestBody["messages"] = messages;

    QJsonDocument doc(requestBody);
    QByteArray postData = doc.toJson(QJsonDocument::Compact);

    QUrl url(m_currentModel.apiUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_currentModel.apiKey).toUtf8());

    auto network = APPLICATION->network();
    m_reply = network->post(request, postData);

    connect(m_reply, SIGNAL(finished()), this, SLOT(onReplyFinished()));
}

void AIAnalyzer::cancel()
{
    if (m_reply)
    {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    if (m_retryTimer)
    {
        m_retryTimer->stop();
        delete m_retryTimer;
        m_retryTimer = nullptr;
    }

    m_retryCount = 0;
}

void AIAnalyzer::onRetryTimeout()
{
    if (m_retryTimer)
    {
        m_retryTimer->stop();
        delete m_retryTimer;
        m_retryTimer = nullptr;
    }

    sendRequest();
}

void AIAnalyzer::onReplyFinished()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    reply->deleteLater();

    if (reply->error() == QNetworkReply::OperationCanceledError)
    {
        emit analysisError(tr("Request was canceled"));
        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        emit analysisError(reply->errorString());
        return;
    }

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (statusCode == 429)
    {
        if (m_retryCount < MAX_RETRIES)
        {
            m_retryCount++;
            if (!m_retryTimer)
            {
                m_retryTimer = new QTimer(this);
                connect(m_retryTimer, &QTimer::timeout, this, &AIAnalyzer::onRetryTimeout);
            }
            m_retryTimer->start(RETRY_DELAY_MS * m_retryCount);
            return;
        }
        emit analysisError(tr("API rate limit exceeded. Please try again later."));
        return;
    }

    if (statusCode != 200)
    {
        QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        QByteArray body = reply->readAll();
        QString errorMsg = tr("API request failed (HTTP %1)").arg(statusCode);
        if (!reason.isEmpty())
        {
            errorMsg += " - " + reason;
        }

        QJsonParseError parseErr;
        QJsonDocument errDoc = QJsonDocument::fromJson(body, &parseErr);
        if (parseErr.error == QJsonParseError::NoError && errDoc.isObject())
        {
            QJsonObject errObj = errDoc.object();
            if (errObj.contains("error") && errObj["error"].isObject())
            {
                QString errMsg = errObj["error"].toObject()["message"].toString();
                if (!errMsg.isEmpty())
                {
                    errorMsg += "\n" + errMsg;
                }
            }
        }

        emit analysisError(errorMsg);
        return;
    }

    QByteArray responseData = reply->readAll();

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
    {
        emit analysisError(tr("Failed to parse AI response"));
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty())
    {
        emit analysisError(tr("AI returned empty response"));
        return;
    }

    QJsonObject firstChoice = choices[0].toObject();
    QJsonObject message = firstChoice["message"].toObject();
    QString content = message["content"].toString();

    if (content.isEmpty())
    {
        emit analysisError(tr("AI returned empty content"));
        return;
    }

    m_retryCount = 0;
    emit analysisFinished(content);
}

QString AIAnalyzer::buildSystemPrompt() const
{
    QString promptTemplate =
        "You are a Minecraft crash log helper for beginner users.\n"
        "Analyze the crash log and explain it in a simple, short, and practical way.\n\n"

        "%1\n\n"

        "Output rules:\n"
        "- Use the user's language.\n"
        "- Keep the answer short.\n"
        "- Do not explain technical details unless necessary.\n"
        "- Do not list too many possibilities.\n"
        "- Do not invent causes not shown in the log.\n"
        "- Prefer clear actions over long analysis.\n\n"

        "Strictly follow this structure, but translate the section titles into the user's language:\n\n"

        "## Problem Cause\n"
        "Use 1 short sentence to explain the main reason of the crash.\n\n"

        "## How to Fix\n"
        "List 1 to 3 concrete steps. Put the most likely fix first.\n\n"

        "## Related Information\n"
        "Only list important mod name, Java version, Minecraft version, loader version, or missing dependency if found.\n"
        "Omit this section if nothing important is found.\n\n"

        "Analysis focus:\n"
        "- Look for FATAL, ERROR, Exception, and Caused by.\n"
        "- The deepest useful Caused by is usually the real cause.\n"
        "- If it is a mod conflict, name the mod if visible.\n"
        "- If a dependency is missing, tell the user which mod/library is missing.\n"
        "- If Java version is wrong, tell the user which Java version to use.\n"
        "- If memory is not enough, suggest increasing allocated memory.";

    QString lang = APPLICATION->settings()->get("Language").toString();

    return promptTemplate.arg(lang);
}
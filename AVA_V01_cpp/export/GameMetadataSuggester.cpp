#include "GameMetadataSuggester.h"

#include "FfmpegLocator.h"
#include "XaiConfig.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
#include <optional>

namespace {

constexpr char kChatCompletionsUrl[] = "https://api.x.ai/v1/chat/completions";
constexpr int kChatTimeoutMs = 60000;
constexpr int kPreferredSeekSeconds = 20;
constexpr int kFallbackSeekSeconds = 2;

QJsonObject nullableStringProperty() {
    QJsonObject property;
    QJsonArray type;
    type.append(QStringLiteral("string"));
    type.append(QStringLiteral("null"));
    property.insert(QStringLiteral("type"), type);
    return property;
}

QJsonObject jsonSchemaResponseFormat(const QString& schemaName, const QJsonObject& schema) {
    QJsonObject jsonSchema;
    jsonSchema.insert(QStringLiteral("name"), schemaName);
    jsonSchema.insert(QStringLiteral("strict"), true);
    jsonSchema.insert(QStringLiteral("schema"), schema);

    QJsonObject responseFormat;
    responseFormat.insert(QStringLiteral("type"), QStringLiteral("json_schema"));
    responseFormat.insert(QStringLiteral("json_schema"), jsonSchema);
    return responseFormat;
}

QJsonObject strictObjectSchema(const QJsonObject& properties, const QStringList& requiredPropertyNames) {
    QJsonArray required;
    for (const QString& propertyName : requiredPropertyNames) {
        required.append(propertyName);
    }

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    schema.insert(QStringLiteral("required"), required);
    schema.insert(QStringLiteral("additionalProperties"), false);
    return schema;
}

QJsonObject nameDateResponseFormat() {
    QJsonObject properties;
    properties.insert(QStringLiteral("home_team_name"), nullableStringProperty());
    properties.insert(QStringLiteral("away_team_name"), nullableStringProperty());
    properties.insert(QStringLiteral("game_date"), nullableStringProperty());
    return jsonSchemaResponseFormat(
        QStringLiteral("game_filename_metadata"),
        strictObjectSchema(properties,
                           {QStringLiteral("home_team_name"),
                            QStringLiteral("away_team_name"),
                            QStringLiteral("game_date")}));
}

QJsonObject colorResponseFormat() {
    QJsonObject properties;
    properties.insert(QStringLiteral("home_color_hex"), nullableStringProperty());
    properties.insert(QStringLiteral("away_color_hex"), nullableStringProperty());
    return jsonSchemaResponseFormat(
        QStringLiteral("game_kit_colors"),
        strictObjectSchema(properties,
                           {QStringLiteral("home_color_hex"),
                            QStringLiteral("away_color_hex")}));
}

QJsonObject textChatMessage(const QString& role, const QString& text) {
    QJsonObject message;
    message.insert(QStringLiteral("role"), role);
    message.insert(QStringLiteral("content"), text);
    return message;
}

QString optionalJsonString(const QJsonValue& value) {
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    if (!value.isString()) {
        return {};
    }
    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return {};
    }
    if (text.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0) {
        return {};
    }
    return text;
}

QString stripJsonFence(QString content) {
    content = content.trimmed();
    if (!content.startsWith(QLatin1String("```"))) {
        return content;
    }
    const int firstNewline = content.indexOf(QLatin1Char('\n'));
    if (firstNewline < 0) {
        return content;
    }
    content = content.mid(firstNewline + 1);
    if (content.endsWith(QLatin1String("```"))) {
        content.chop(3);
    }
    return content.trimmed();
}

std::optional<QJsonObject> tryParseJsonObject(const QByteArray& jsonText) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonText, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    return document.object();
}

QJsonObject parseJsonObjectFromModelText(QString text) {
    text = text.trimmed();
    if (text.isEmpty()) {
        return {};
    }

    if (const std::optional<QJsonObject> object = tryParseJsonObject(text.toUtf8())) {
        return *object;
    }

    const QString unfenced = stripJsonFence(std::move(text));
    if (const std::optional<QJsonObject> object = tryParseJsonObject(unfenced.toUtf8())) {
        return *object;
    }
    return {};
}

QString textFromMessageContent(const QJsonValue& contentValue) {
    if (contentValue.isString()) {
        return contentValue.toString();
    }
    if (!contentValue.isArray()) {
        return {};
    }

    QStringList parts;
    const QJsonArray contentParts = contentValue.toArray();
    for (const QJsonValue& part : contentParts) {
        if (part.isString()) {
            parts.append(part.toString());
            continue;
        }
        if (!part.isObject()) {
            continue;
        }
        const QString text = part.toObject().value(QStringLiteral("text")).toString();
        if (!text.isEmpty()) {
            parts.append(text);
        }
    }
    return parts.join(QString());
}

QJsonObject parseStructuredChatCompletion(const QByteArray& responseBody) {
    QJsonParseError parseError;
    const QJsonDocument envelope = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !envelope.isObject()) {
        return {};
    }

    const QJsonObject root = envelope.object();
    if (root.contains(QStringLiteral("error"))) {
        return {};
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        return {};
    }

    const QJsonValue contentValue = choices.at(0)
        .toObject()
        .value(QStringLiteral("message"))
        .toObject()
        .value(QStringLiteral("content"));
    if (contentValue.isObject()) {
        return contentValue.toObject();
    }

    return parseJsonObjectFromModelText(textFromMessageContent(contentValue));
}

QString normalizeHexColor(const QString& text) {
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    QColor parsed(trimmed);
    if (!parsed.isValid() && !trimmed.startsWith(QLatin1Char('#'))) {
        parsed = QColor(QLatin1Char('#') + trimmed);
    }
    if (!parsed.isValid() || parsed.alpha() != 255) {
        return {};
    }
    return parsed.name(QColor::HexRgb).toUpper();
}

QStringList uniqueFileNames(const QStringList& sourceVideoPaths) {
    QStringList fileNames;
    QSet<QString> seen;
    std::for_each(sourceVideoPaths.cbegin(), sourceVideoPaths.cend(), [&](const QString& path) {
        const QString fileName = QFileInfo(path).fileName().trimmed();
        if (fileName.isEmpty() || seen.contains(fileName)) {
            return;
        }
        seen.insert(fileName);
        fileNames.append(fileName);
    });
    return fileNames;
}

QString firstExistingVideoPath(const QStringList& sourceVideoPaths) {
    const auto iterator = std::find_if(sourceVideoPaths.cbegin(), sourceVideoPaths.cend(),
                                     [](const QString& path) {
                                         return !path.trimmed().isEmpty() && QFileInfo::exists(path);
                                     });
    return iterator != sourceVideoPaths.cend() ? *iterator : QString();
}

QString nameDateSystemPrompt() {
    return QStringLiteral(
        "You extract sports game metadata from video file names. "
        "The first team in patterns like \"X vs Y\" or \"X v Y\" is home; the second is away. "
        "Infer a calendar date when the file name contains one, as yyyy-MM-dd. "
        "Use null for any field you cannot infer with reasonable confidence. "
        "Do not invent teams that are not suggested by the file name.");
}

QString nameDateUserPrompt(const QStringList& fileNames) {
    return QStringLiteral("Video file name(s):\n%1").arg(fileNames.join(QLatin1Char('\n')));
}

QString colorSystemPrompt() {
    return QStringLiteral(
        "You identify the two teams' primary jersey/kit colors from a sports video frame. "
        "Ignore the field, crowd, referee, and goalkeeper if they use a clearly different kit. "
        "Map colors onto the given home and away team names when those names are provided. "
        "Return each color as #RRGGBB. Use null when you cannot tell.");
}

QString colorUserPrompt(const QString& homeName, const QString& awayName) {
    if (homeName.isEmpty() && awayName.isEmpty()) {
        return QStringLiteral(
            "Identify the two distinct outfield jersey colors. "
            "Assign the first distinct kit color to home_color_hex and the other to away_color_hex.");
    }
    return QStringLiteral(
        "Home team: %1\nAway team: %2\n"
        "What are the primary outfield jersey colors for each team?")
        .arg(homeName.isEmpty() ? QStringLiteral("(unknown)") : homeName,
             awayName.isEmpty() ? QStringLiteral("(unknown)") : awayName);
}

} // namespace

GameMetadataSuggester::GameMetadataSuggester(QObject* parent) : QObject(parent) {
    networkManager_ = new QNetworkAccessManager(this);
}

GameMetadataSuggester::~GameMetadataSuggester() {
    abortActiveWork();
}

void GameMetadataSuggester::startSuggestionFromVideoPaths(const QStringList& sourceVideoPaths) {
    abortActiveWork();
    thumbnailDir_.reset();
    ++generation_;
    running_ = true;
    aborted_ = false;
    namesDone_ = false;
    thumbnailDone_ = false;
    colorStatusEmitted_ = false;
    colorsRequested_ = false;
    finishedEmitted_ = false;
    suggestedHomeName_.clear();
    suggestedAwayName_.clear();
    thumbnailJpeg_.clear();
    thumbnailSourcePath_.clear();

    if (!XaiConfig::isConfigured()) {
        finishQuietly();
        return;
    }

    const QStringList fileNames = uniqueFileNames(sourceVideoPaths);
    if (fileNames.isEmpty()) {
        namesDone_ = true;
    } else {
        startNameDateRequest(fileNames);
    }

    const QString sourceVideoPath = firstExistingVideoPath(sourceVideoPaths);
    if (sourceVideoPath.isEmpty() || FfmpegLocator::findFfmpeg().isEmpty()) {
        thumbnailDone_ = true;
    } else {
        startThumbnailExtraction(sourceVideoPath);
    }

    if (namesDone_ && thumbnailDone_) {
        startColorsOrFinish();
    }
}

void GameMetadataSuggester::abort() {
    const bool wasRunning = running_;
    abortActiveWork();
    if (wasRunning) {
        finishQuietly();
    }
}

void GameMetadataSuggester::abortActiveWork() {
    aborted_ = true;

    if (activeReply_) {
        QNetworkReply* reply = activeReply_;
        activeReply_ = nullptr;
        reply->abort();
        reply->deleteLater();
    }

    // Non-blocking: kill() + deleteLater(); do not waitForFinished() or remove the temp dir here.
    stopAndDiscardThumbnailProcess();
}

void GameMetadataSuggester::stopAndDiscardThumbnailProcess() {
    if (!thumbnailProcess_) {
        return;
    }
    QProcess* dyingProcess = thumbnailProcess_.release();
    dyingProcess->disconnect();
    if (dyingProcess->state() != QProcess::NotRunning) {
        dyingProcess->kill();
    }
    dyingProcess->deleteLater();
}

void GameMetadataSuggester::finishQuietly() {
    if (finishedEmitted_) {
        return;
    }
    finishedEmitted_ = true;
    running_ = false;
    emit finished();
}

void GameMetadataSuggester::maybeNotifyColorDetectionStarted() {
    if (aborted_ || !running_ || colorStatusEmitted_ || !namesDone_) {
        return;
    }
    colorStatusEmitted_ = true;
    emit colorDetectionStarted();
}

void GameMetadataSuggester::startNameDateRequest(const QStringList& fileNames) {
    const QString apiKey = XaiConfig::apiKey();
    if (apiKey.isEmpty()) {
        namesDone_ = true;
        startColorsOrFinish();
        return;
    }

    QJsonArray messages;
    messages.append(textChatMessage(QStringLiteral("system"), nameDateSystemPrompt()));
    messages.append(textChatMessage(QStringLiteral("user"), nameDateUserPrompt(fileNames)));

    QJsonObject body;
    body.insert(QStringLiteral("model"), XaiConfig::chatModelName());
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), 0);
    body.insert(QStringLiteral("max_completion_tokens"), 256);
    body.insert(QStringLiteral("response_format"), nameDateResponseFormat());

    QNetworkRequest request(QUrl(QString::fromLatin1(kChatCompletionsUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kChatTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    activeReply_ = networkManager_->post(request, payload);
    const int generation = generation_;
    connect(activeReply_, &QNetworkReply::finished, this, [this, generation]() {
        onChatReplyFinished(generation, ChatKind::Names);
    });
}

void GameMetadataSuggester::startThumbnailExtraction(const QString& sourceVideoPath) {
    thumbnailSourcePath_ = sourceVideoPath;
    startThumbnailFfmpeg(kPreferredSeekSeconds, false);
}

void GameMetadataSuggester::startThumbnailFfmpeg(int seekSeconds, bool isRetry) {
    if (aborted_) {
        return;
    }

    const QString ffmpegPath = FfmpegLocator::findFfmpeg();
    if (ffmpegPath.isEmpty() || thumbnailSourcePath_.isEmpty()) {
        thumbnailDone_ = true;
        startColorsOrFinish();
        return;
    }

    if (!thumbnailDir_) {
        thumbnailDir_ = std::make_unique<QTemporaryDir>();
        if (!thumbnailDir_->isValid()) {
            thumbnailDir_.reset();
            thumbnailDone_ = true;
            startColorsOrFinish();
            return;
        }
    }

    const QString outputPath = thumbnailDir_->filePath(QStringLiteral("frame.jpg"));
    QFile::remove(outputPath);

    if (thumbnailProcess_) {
        stopAndDiscardThumbnailProcess();
    }

    thumbnailProcess_ = std::make_unique<QProcess>();
    const int generation = generation_;
    connect(thumbnailProcess_.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, generation, isRetry](int, QProcess::ExitStatus) {
                onThumbnailProcessFinished(generation, isRetry);
            });

    QStringList arguments;
    arguments << QStringLiteral("-hide_banner")
              << QStringLiteral("-loglevel") << QStringLiteral("error")
              << QStringLiteral("-y")
              << QStringLiteral("-ss") << QString::number(seekSeconds)
              << QStringLiteral("-i") << thumbnailSourcePath_
              << QStringLiteral("-frames:v") << QStringLiteral("1")
              << QStringLiteral("-q:v") << QStringLiteral("4")
              << QStringLiteral("-vf") << QStringLiteral("scale=1280:-2")
              << outputPath;
    thumbnailProcess_->start(ffmpegPath, arguments);
}

void GameMetadataSuggester::onThumbnailProcessFinished(int generation, bool isRetry) {
    if (aborted_ || generation != generation_) {
        return;
    }

    QProcess* process = thumbnailProcess_.release();
    const bool succeeded = process
        && process->exitStatus() == QProcess::NormalExit
        && process->exitCode() == 0;
    if (process) {
        process->disconnect();
        process->deleteLater();
    }

    const QString outputPath = thumbnailDir_
        ? thumbnailDir_->filePath(QStringLiteral("frame.jpg"))
        : QString();
    QByteArray jpegBytes;
    if (succeeded && !outputPath.isEmpty()) {
        QFile file(outputPath);
        if (file.open(QIODevice::ReadOnly)) {
            jpegBytes = file.readAll();
        }
    }

    if (jpegBytes.isEmpty() && !isRetry) {
        startThumbnailFfmpeg(kFallbackSeekSeconds, true);
        return;
    }

    if (!jpegBytes.isEmpty() && jpegBytes.size() <= XaiConfig::maxMetadataThumbnailBytes()) {
        thumbnailJpeg_ = jpegBytes;
    }
    thumbnailDone_ = true;
    startColorsOrFinish();
}

void GameMetadataSuggester::onChatReplyFinished(int generation, ChatKind kind) {
    QNetworkReply* reply = activeReply_;
    activeReply_ = nullptr;

    if (!reply) {
        return;
    }

    const QByteArray responseBody = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();

    if (aborted_ || generation != generation_) {
        return;
    }

    if (kind == ChatKind::Names) {
        if (ok) {
            handleNameDateResponse(responseBody);
        }
        namesDone_ = true;
        maybeNotifyColorDetectionStarted();
        startColorsOrFinish();
        return;
    }

    if (kind == ChatKind::Colors) {
        if (ok) {
            handleColorResponse(responseBody);
        }
        finishQuietly();
    }
}

void GameMetadataSuggester::handleNameDateResponse(const QByteArray& responseBody) {
    const QJsonObject object = parseStructuredChatCompletion(responseBody);
    suggestedHomeName_ = optionalJsonString(object.value(QStringLiteral("home_team_name")));
    suggestedAwayName_ = optionalJsonString(object.value(QStringLiteral("away_team_name")));

    QDate gameDate;
    const QString dateText = optionalJsonString(object.value(QStringLiteral("game_date")));
    if (!dateText.isEmpty()) {
        gameDate = QDate::fromString(dateText, Qt::ISODate);
        if (!gameDate.isValid()) {
            gameDate = QDate::fromString(dateText, QStringLiteral("yyyy-MM-dd"));
        }
    }

    if (aborted_
        || (suggestedHomeName_.isEmpty() && suggestedAwayName_.isEmpty() && !gameDate.isValid())) {
        return;
    }
    emit nameDateSuggested(suggestedHomeName_, suggestedAwayName_, gameDate);
}

void GameMetadataSuggester::handleColorResponse(const QByteArray& responseBody) {
    const QJsonObject object = parseStructuredChatCompletion(responseBody);
    const QString homeColor = normalizeHexColor(optionalJsonString(object.value(QStringLiteral("home_color_hex"))));
    const QString awayColor = normalizeHexColor(optionalJsonString(object.value(QStringLiteral("away_color_hex"))));
    if (aborted_ || (homeColor.isEmpty() && awayColor.isEmpty())) {
        return;
    }
    emit colorsSuggested(homeColor, awayColor);
}

void GameMetadataSuggester::startColorsOrFinish() {
    if (aborted_ || !running_ || finishedEmitted_) {
        return;
    }
    if (!namesDone_ || !thumbnailDone_) {
        return;
    }
    if (thumbnailJpeg_.isEmpty() || !XaiConfig::isConfigured()) {
        finishQuietly();
        return;
    }
    if (colorsRequested_) {
        return;
    }
    maybeNotifyColorDetectionStarted();
    startColorRequest();
}

void GameMetadataSuggester::startColorRequest() {
    colorsRequested_ = true;

    const QString apiKey = XaiConfig::apiKey();
    if (apiKey.isEmpty() || thumbnailJpeg_.isEmpty()) {
        finishQuietly();
        return;
    }

    const QString dataUrl = QStringLiteral("data:image/jpeg;base64,")
        + QString::fromLatin1(thumbnailJpeg_.toBase64());

    QJsonObject imageUrl;
    imageUrl.insert(QStringLiteral("url"), dataUrl);
    imageUrl.insert(QStringLiteral("detail"), QStringLiteral("low"));

    QJsonObject imagePart;
    imagePart.insert(QStringLiteral("type"), QStringLiteral("image_url"));
    imagePart.insert(QStringLiteral("image_url"), imageUrl);

    QJsonObject textPart;
    textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
    textPart.insert(QStringLiteral("text"), colorUserPrompt(suggestedHomeName_, suggestedAwayName_));

    QJsonArray userContent;
    userContent.append(imagePart);
    userContent.append(textPart);

    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), userContent);

    QJsonArray messages;
    messages.append(textChatMessage(QStringLiteral("system"), colorSystemPrompt()));
    messages.append(userMessage);

    QJsonObject body;
    body.insert(QStringLiteral("model"), XaiConfig::chatModelName());
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), 0);
    body.insert(QStringLiteral("max_completion_tokens"), 256);
    body.insert(QStringLiteral("response_format"), colorResponseFormat());

    QNetworkRequest request(QUrl(QString::fromLatin1(kChatCompletionsUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kChatTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    activeReply_ = networkManager_->post(request, payload);
    const int generation = generation_;
    connect(activeReply_, &QNetworkReply::finished, this, [this, generation]() {
        onChatReplyFinished(generation, ChatKind::Colors);
    });
}

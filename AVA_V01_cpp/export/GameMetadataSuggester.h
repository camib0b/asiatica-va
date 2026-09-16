#pragma once

#include <QByteArray>
#include <QDate>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTemporaryDir;

/// Calls xAI chat completions to infer game metadata from a video file name
/// and jersey colors from a still frame. Failures are silent.
class GameMetadataSuggester final : public QObject {
    Q_OBJECT

public:
    explicit GameMetadataSuggester(QObject* parent = nullptr);
    ~GameMetadataSuggester() override;

    /// Starts filename parsing and, when possible, jersey-color detection.
    /// \p sourceVideoPaths are the original imported files (not a concat temp path).
    void startSuggestionFromVideoPaths(const QStringList& sourceVideoPaths);
    void abort();
    bool isRunning() const { return running_; }

signals:
    void nameDateSuggested(const QString& homeTeamName,
                           const QString& awayTeamName,
                           const QDate& gameDate);
    void colorDetectionStarted();
    void colorsSuggested(const QString& homeColorHex, const QString& awayColorHex);
    void finished();

private:
    enum class ChatKind { None, Names, Colors };

    void startNameDateRequest(const QStringList& fileNames);
    void startThumbnailExtraction(const QString& sourceVideoPath);
    void startThumbnailFfmpeg(int seekSeconds, bool isRetry);
    void startColorRequest();
    void startColorsOrFinish();
    void notifyColorDetectionStarted();
    void onChatReplyFinished(int generation, ChatKind kind);
    void onThumbnailProcessFinished(int generation, bool isRetry);
    void handleNameDateResponse(const QByteArray& responseBody);
    void handleColorResponse(const QByteArray& responseBody);
    void finishQuietly();
    void abortActiveWork();

    QNetworkAccessManager* networkManager_ = nullptr;
    QNetworkReply* activeReply_ = nullptr;
    QProcess* thumbnailProcess_ = nullptr;
    QTemporaryDir* thumbnailDir_ = nullptr;
    QString thumbnailSourcePath_;

    bool running_ = false;
    bool aborted_ = false;
    bool namesDone_ = false;
    bool thumbnailDone_ = false;
    bool colorStatusEmitted_ = false;
    bool finishedEmitted_ = false;
    int generation_ = 0;

    QString suggestedHomeName_;
    QString suggestedAwayName_;
    QByteArray thumbnailJpeg_;
};

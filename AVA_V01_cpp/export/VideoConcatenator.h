#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <memory>

class QWidget;

struct VideoConcatenationResult {
    bool succeeded = false;
    QString outputPath;
    QString errorMessage;
};

class VideoConcatenator : public QObject {
    Q_OBJECT

public:
    explicit VideoConcatenator(QObject* parent = nullptr);
    ~VideoConcatenator() override;

    /// Runs file selection validation, FFmpeg concat, and progress UI in one call.
    /// Returns a snapshot of the outcome and resets internal state for reuse.
    VideoConcatenationResult runWithProgress(const QStringList& inputPaths,
                                             const QString& outputDir,
                                             QWidget* parentWidget);

    void startConcatenation(const QStringList& inputPaths, const QString& outputDir);
    void cancel();

    bool succeeded() const;
    QString outputPath() const;
    QString errorMessage() const;

    bool waitWithProgress(QWidget* parentWidget);

    static QStringList selectVideoFiles(QWidget* parentWidget);
    static bool showFileOrderDialog(QStringList& filePaths, QWidget* parentWidget);

signals:
    void concatenationFinished(bool success);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    enum class JobState {
        Idle,
        Running,
        Succeeded,
        Failed,
        Cancelled,
    };

    void beginNewJob();
    void stopAndDiscardProcess();
    void failWith(const QString& message);
    void settle(JobState nextState, const QString& message);
    bool isTerminal() const;
    void discardConcatList();
    void removePartialOutput();
    void resetToIdle();

    /// Unparented QObject; this unique_ptr is the only owner. Replace via
    /// stopAndDiscardProcess() (release + deleteLater) so finished() cannot
    /// delete the process on its own stack.
    std::unique_ptr<QProcess> process_;
    QString concatListPath_;
    QString outputPath_;
    QString errorMessage_;
    JobState state_ = JobState::Idle;
};

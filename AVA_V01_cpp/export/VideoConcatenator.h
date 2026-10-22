#pragma once

#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QProgressDialog;
class QWidget;

struct VideoConcatenationResult {
    bool succeeded = false;
    QString outputPath;
    QString errorMessage;
};

Q_DECLARE_METATYPE(VideoConcatenationResult)

class VideoConcatenator : public QObject {
    Q_OBJECT

public:
    explicit VideoConcatenator(QObject* parent = nullptr);
    ~VideoConcatenator() override;

    /// Starts FFmpeg concat and a window-modal progress dialog without blocking
    /// the UI thread. Emits runFinished with a snapshot, then resets to idle.
    /// Connect runFinished before calling.
    void runWithProgress(const QStringList& inputPaths,
                         const QString& outputDir,
                         QWidget* parentWidget);

    void startConcatenation(const QStringList& inputPaths, const QString& outputDir);
    void cancel();

    bool succeeded() const;
    QString outputPath() const;
    QString errorMessage() const;

    /// Progress UI for a concatenation that is already running (team setup).
    /// Calls onFinished later on the UI thread and does not reset the job, so
    /// getters are still valid inside onFinished.
    void waitWithProgress(QWidget* parentWidget, std::function<void(bool succeeded)> onFinished);

    static QStringList selectVideoFiles(QWidget* parentWidget);
    static bool showFileOrderDialog(QStringList& filePaths, QWidget* parentWidget);

signals:
    void concatenationFinished(bool success);
    void runFinished(const VideoConcatenationResult& result);

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
    void openProgressDialog(QWidget* parentWidget);
    void closeProgressDialog();
    void publishRunResult();
    void publishProgressCompletion();

    /// Unparented QObject; this unique_ptr is the only owner. Replace via
    /// stopAndDiscardProcess() (release + deleteLater) so finished() cannot
    /// delete the process on its own stack.
    std::unique_ptr<QProcess> process_;
    QString concatListPath_;
    QString outputPath_;
    QString errorMessage_;
    int sourceCount_ = 0;
    JobState state_ = JobState::Idle;
    QPointer<QProgressDialog> progressDialog_;
    QPointer<QWidget> progressParent_;
    std::function<void(bool succeeded)> progressCompletion_;
    bool runWithProgressActive_ = false;
    bool runResultPublished_ = false;
};

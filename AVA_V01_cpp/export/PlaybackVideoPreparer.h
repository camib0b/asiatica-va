#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>

#include <functional>
#include <memory>

class QProgressDialog;
class QWidget;

/// Prepares source videos for in-app playback via QMediaPlayer (AVFoundation on macOS).
/// YouTube-style MKV/WebM (VP9/Opus, etc.) is transcoded to H.264/AAC MP4 for AVFoundation playback.
/// Clip export continues to use the original file path with FFmpeg.
class PlaybackVideoPreparer final : public QObject {
  Q_OBJECT

public:
  explicit PlaybackVideoPreparer(QObject* parent = nullptr);
  ~PlaybackVideoPreparer() override;

  /// True for MKV/WebM containers, which QMediaPlayer/AVFoundation typically cannot decode.
  static bool requiresTranscodeForPlayback(const QString& filePath);

  void startPreparation(const QString& inputPath, const QString& outputDir);
  void cancel();

  /// Shows a window-modal progress dialog and returns immediately. Calls
  /// onFinished on the UI thread when preparation is already terminal or
  /// after preparationFinished. Does not block on QProcess.
  void waitWithProgress(QWidget* parentWidget, std::function<void(bool succeeded)> onFinished);

  QString outputPath() const { return outputPath_; }
  QString errorMessage() const { return errorMessage_; }
  bool succeeded() const { return succeeded_; }

signals:
  void preparationFinished(bool success);

private slots:
  void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void onProcessError(QProcess::ProcessError error);

private:
  void stopAndDiscardProcess();
  void openProgressDialog(QWidget* parentWidget);
  void closeProgressDialog();
  void deliverProgressWait();

  /// Unparented QObject; this unique_ptr is the only owner. Replace via
  /// stopAndDiscardProcess() (release + deleteLater) so finished() cannot
  /// delete the process on its own stack.
  std::unique_ptr<QProcess> process_;
  QString outputPath_;
  QString errorMessage_;
  bool finished_ = false;
  bool succeeded_ = false;
  bool cancelled_ = false;
  QPointer<QProgressDialog> progressDialog_;
  QPointer<QWidget> progressParent_;
  std::function<void(bool succeeded)> progressCompletion_;
  bool progressWaitDelivered_ = false;
};

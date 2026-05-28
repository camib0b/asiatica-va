#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class QWidget;

/// Prepares source videos for in-app playback via QMediaPlayer (AVFoundation on macOS).
/// YouTube-style MKV/WebM (VP9/Opus, etc.) is transcoded to H.264/AAC MP4 for AVFoundation playback.
/// Clip export continues to use the original file path with FFmpeg.
class PlaybackVideoPreparer final : public QObject {
  Q_OBJECT

public:
  explicit PlaybackVideoPreparer(QObject* parent = nullptr);
  ~PlaybackVideoPreparer() override;

  /// Returns true when Qt's media stack is unlikely to decode the file without conversion.
  static bool needsPreparation(const QString& filePath);

  void startPreparation(const QString& inputPath, const QString& outputDir);
  void cancel();

  bool isRunning() const;
  bool waitWithProgress(QWidget* parentWidget);

  QString outputPath() const { return outputPath_; }
  QString errorMessage() const { return errorMessage_; }
  bool succeeded() const { return succeeded_; }

signals:
  void preparationFinished(bool success);

private slots:
  void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
  QProcess* process_ = nullptr;
  QString outputPath_;
  QString errorMessage_;
  bool finished_ = false;
  bool succeeded_ = false;
  bool cancelled_ = false;
};

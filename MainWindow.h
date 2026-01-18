#pragma once

#include <QMainWindow>

class QLabel;
class QPushButton;
class QVideoWidget;
class QMediaPlayer;
class QAudioOutput;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override = default;

private slots:
  void onImportVideoClicked();

private:
  void buildUi();
  void wireSignals();

  QLabel* headerLabel_ = nullptr;
  QLabel* subtitleLabel_ = nullptr;
  QPushButton* importButton_ = nullptr;

  QVideoWidget* videoWidget_ = nullptr;
  QMediaPlayer* player_ = nullptr;
  QAudioOutput* audioOutput_ = nullptr;
};

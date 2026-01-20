#pragma once

#include <QMainWindow>
#include <QString>

class WelcomeWindow;
class WorkWindow;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override = default;

private slots:
  void onVideoImportRequested();
  void onVideoClosed();

private:
  void showWelcomeWindow();
  void showWorkWindow(const QString& filePath);
  QString promptForVideoFile();

  WelcomeWindow* welcomeWindow_ = nullptr;
  WorkWindow* workWindow_ = nullptr;
};

#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <memory>

class WelcomeWindow;
class WorkWindow;
class TagSession;
class QStackedWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

private slots:
  void onVideoImportRequested();
  void onVideoClosed();

private:
  void showWelcomeWindow();
  void showWorkWindowWithSetup(const QString& filePath, const QStringList& sourceVideoPaths);
  void resetTagSession();

  QStackedWidget* stack_ = nullptr;
  WelcomeWindow* welcomeWindow_ = nullptr;
  WorkWindow* workWindow_ = nullptr;
  /// Unparented QObject; this unique_ptr is the only owner.
  std::unique_ptr<TagSession> tagSession_;
};

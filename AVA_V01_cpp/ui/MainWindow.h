#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <memory>

class WelcomeWindow;
class WorkWindow;
class TagSession;
class LicenseLockOverlay;
class QStackedWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

private slots:
  void onVideoImportRequested();
  void onVideoClosed();
  void onEnterLicenseRequested();
  void onLicenseOverlayClosed();
  void onLicenseEntitlementChanged();

private:
  void showWelcomeWindow();
  void showWorkWindowWithSetup(const QString& filePath, const QStringList& sourceVideoPaths);
  void showLicenseOverlay(bool allowClose);
  void resetTagSession();

  QStackedWidget* stack_ = nullptr;
  WelcomeWindow* welcomeWindow_ = nullptr;
  WorkWindow* workWindow_ = nullptr;
  LicenseLockOverlay* licenseOverlay_ = nullptr;
  /// Unparented QObject; this unique_ptr is the only owner.
  std::unique_ptr<TagSession> tagSession_;
};

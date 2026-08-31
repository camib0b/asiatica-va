#pragma once

#include <QMainWindow>
#include <QString>

class WelcomeWindow;
class WorkWindow;
class TagSession;
class LicenseLockOverlay;
class QStackedWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override = default;

private slots:
  void onVideoImportRequested();
  void onVideoClosed();
  void onEnterLicenseRequested();
  void onLicenseOverlayClosed();
  void onLicenseEntitlementChanged();

private:
  void showWelcomeWindow();
  void showWorkWindowWithSetup(const QString& filePath);
  void showLicenseOverlay(bool allowClose);

  QStackedWidget* stack_ = nullptr;
  WelcomeWindow* welcomeWindow_ = nullptr;
  WorkWindow* workWindow_ = nullptr;
  LicenseLockOverlay* licenseOverlay_ = nullptr;
  TagSession* tagSession_ = nullptr;
};

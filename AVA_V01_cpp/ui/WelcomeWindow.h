#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QEvent;

class WelcomeWindow final : public QWidget {
  Q_OBJECT

public:
  explicit WelcomeWindow(QWidget* parent = nullptr);
  ~WelcomeWindow() override = default;

public slots:
  void applyUiStrings();

signals:
  void videoImportRequested();
  void enterLicenseRequested();

protected:
  void changeEvent(QEvent* event) override;

private:
  void buildUi();
  void wireSignals();
  void buildKeyboardShortcuts();
  void syncImportButtonMinimumWidth();

  QPointer<QLabel> titleLabel_;
  QPointer<QPushButton> importButton_;
  QPointer<QLabel> licenseStatusLabel_;
  QPointer<QPushButton> enterLicenseButton_;
};

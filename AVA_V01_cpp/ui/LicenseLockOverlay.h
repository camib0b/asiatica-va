#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class LicenseLockOverlay final : public QWidget {
  Q_OBJECT

public:
  explicit LicenseLockOverlay(QWidget* parent = nullptr);

  void setCloseAllowed(bool allowed);
  void refreshCopy();

signals:
  void closeRequested();

private slots:
  void onActivateClicked();
  void onLoadFileClicked();
  void onLanguageChanged(int index);
  void onLicenseStateChanged();

private:
  void buildUi();
  void applyUiStrings();

  QComboBox* languageCombo_ = nullptr;
  QLabel* titleLabel_ = nullptr;
  QLabel* bodyLabel_ = nullptr;
  QLabel* emailLabel_ = nullptr;
  QLineEdit* emailEdit_ = nullptr;
  QLabel* keyLabel_ = nullptr;
  QPlainTextEdit* keyEdit_ = nullptr;
  QPushButton* activateButton_ = nullptr;
  QPushButton* loadFileButton_ = nullptr;
  QPushButton* closeButton_ = nullptr;
  QLabel* contactLabel_ = nullptr;
  QLabel* messageLabel_ = nullptr;
  bool closeAllowed_ = false;
};

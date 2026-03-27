#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

class WelcomeWindow final : public QWidget {
  Q_OBJECT

public:
  explicit WelcomeWindow(QWidget* parent = nullptr);
  ~WelcomeWindow() override = default;

public slots:
  void applyUiStrings();

signals:
  void videoImportRequested();

private:
  void buildUi();
  void wireSignals();
  void buildKeyboardShortcuts();

  QLabel* subtitleLabel_ = nullptr;
  QPushButton* importButton_ = nullptr;
  QLabel* speedLabel_ = nullptr;
};

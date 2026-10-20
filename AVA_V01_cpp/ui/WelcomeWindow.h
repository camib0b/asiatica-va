#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QShowEvent;
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

protected:
  void changeEvent(QEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  void buildUi();
  void wireSignals();
  void buildKeyboardShortcuts();
  void syncImportButtonMinimumWidth();
  void playEntrance();

  QPointer<QLabel> titleLabel_;
  QPointer<QPushButton> importButton_;
  QPointer<QWidget> titleEntrance_;
  QPointer<QWidget> importEntrance_;
  bool entrancePlayed_ = false;
};

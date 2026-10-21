#pragma once

#include <QPointer>
#include <QWidget>

class QAction;
class QEvent;
class QHideEvent;
class QLabel;
class QPushButton;
class QShowEvent;
class QVBoxLayout;

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
  void hideEvent(QHideEvent* event) override;

private:
  enum class CopyFixture { Demo, WorstCase, Empty, One };

  void buildUi();
  void buildFixtureToggle(QVBoxLayout* outerLayout);
  void wireSignals();
  void buildKeyboardShortcuts();
  void activateImportButton();
  void scheduleAmbientPulse(int delayMs);
  void syncImportButtonMinimumWidth();
  void playEntrance();
  QString titleForFixture() const;
  QString importLabelForFixture() const;

  QPointer<QLabel> titleLabel_;
  QPointer<QPushButton> importButton_;
  QPointer<QWidget> titleEntrance_;
  QPointer<QWidget> importEntrance_;
  QPointer<QAction> importMnemonicAction_;
  CopyFixture copyFixture_ = CopyFixture::Demo;
  bool entrancePlayed_ = false;
};

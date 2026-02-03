#pragma once

#include <QWidget>
#include <QStringList>
#include <QList>
#include <QHash>

class QPushButton;
class QGridLayout;
class QHBoxLayout;
class QWidget;
class QTimer;
class QAction;

class GameControls final : public QWidget {
  Q_OBJECT

public:
  explicit GameControls(QWidget* parent = nullptr);

  QString possessionTeam() const { return possessionTeam_; }

signals:
  void mainEventPressed(const QString& mainEvent);
  void gameEventMarked(const QString& mainEvent, const QString& followUpEvent = QString());
  void possessionChanged(const QString& team);  // "Home" or "Away"

private slots:
  void onTurnoverClicked();
  void onMainButtonClicked();
  void onFollowUpButtonClicked();

private:
  void buildUi();
  void wireSignals();
  void buildKeyboardShortcuts();
  void showFirstLevelFollowUps(const QString& mainEvent);
  void showSecondLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp);
  void hideFollowUpButtons();
  QStringList getFirstLevelFollowUps(const QString& mainEvent) const;
  QStringList getSecondLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp) const;
  void flashButtonBorder(QPushButton* button);
  void setActiveMainButton(QPushButton* button);
  void clearActiveMainButton();

  enum class FollowUpStage {
    None,
    FirstLevel,
    SecondLevel,
  };

  QGridLayout* mainGridLayout_ = nullptr;
  QHBoxLayout* followUpLayout_ = nullptr;
  QWidget* followUpContainer_ = nullptr;

  QPushButton* turnoverButton_ = nullptr;
  QString possessionTeam_;  // "Home" or "Away"
  
  QPushButton* hit16ydButton_ = nullptr;
  QPushButton* hit50ydButton_ = nullptr;
  QPushButton* hit75ydButton_ = nullptr;
  QPushButton* enterDButton_ = nullptr;
  QPushButton* shotButton_ = nullptr;
  QPushButton* goalButton_ = nullptr;
  QPushButton* pcButton_ = nullptr;
  QPushButton* psButton_ = nullptr;
  QPushButton* cardButton_ = nullptr;

  // keyboard shortcuts:
  QAction* hit16ydAction_ = nullptr;
  QAction* hit50ydAction_ = nullptr;
  QAction* hit75ydAction_ = nullptr;
  QAction* enterDAction_ = nullptr;
  QAction* shotAction_ = nullptr;
  QAction* goalAction_ = nullptr;
  QAction* pcAction_ = nullptr;
  QAction* psAction_ = nullptr;
  QAction* cardAction_ = nullptr;
  QList<QAction*> followUpNumberActions_;
  QAction* escapeAction_ = nullptr;

  QString currentMainEvent_;
  QString currentFirstFollowUp_;
  FollowUpStage followUpStage_ = FollowUpStage::None;
  QPushButton* activeMainButton_ = nullptr;
  QList<QPushButton*> followUpButtons_;
  QHash<QPushButton*, QTimer*> flashTimers_;
};

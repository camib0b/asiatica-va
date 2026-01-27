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

class GameControls final : public QWidget {
  Q_OBJECT

public:
  explicit GameControls(QWidget* parent = nullptr);

signals:
  void mainEventPressed(const QString& mainEvent);
  void gameEventMarked(const QString& mainEvent, const QString& followUpEvent = QString());

private slots:
  void onMainButtonClicked();
  void onFollowUpButtonClicked();

private:
  void buildUi();
  void wireSignals();
  void showFirstLevelFollowUps(const QString& mainEvent);
  void showSecondLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp);
  void hideFollowUpButtons();
  QStringList getFirstLevelFollowUps(const QString& mainEvent) const;
  QStringList getSecondLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp) const;
  void flashButtonBorder(QPushButton* button);

  enum class FollowUpStage {
    None,
    FirstLevel,
    SecondLevel,
  };

  QGridLayout* mainGridLayout_ = nullptr;
  QHBoxLayout* followUpLayout_ = nullptr;
  QWidget* followUpContainer_ = nullptr;
  
  QPushButton* enterDButton_ = nullptr;
  QPushButton* shotButton_ = nullptr;
  QPushButton* pcButton_ = nullptr;
  QPushButton* goalButton_ = nullptr;
  QPushButton* hit16ydButton_ = nullptr;
  QPushButton* hit50ydButton_ = nullptr;
  QPushButton* recoveryButton_ = nullptr;
  QPushButton* lossButton_ = nullptr;

  QString currentMainEvent_;
  QString currentFirstFollowUp_;
  FollowUpStage followUpStage_ = FollowUpStage::None;
  QList<QPushButton*> followUpButtons_;
  QHash<QPushButton*, QTimer*> flashTimers_;
};

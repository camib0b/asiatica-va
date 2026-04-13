#pragma once

#include <QWidget>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QVector>

class QPushButton;
class QLabel;
class QGridLayout;
class QHBoxLayout;
class QWidget;
class QTimer;
class QAction;
class QKeyEvent;

class GameControls final : public QWidget {
  Q_OBJECT

public:
  /// Minimum width of the control grid (button column content), in device pixels.
  static constexpr int kMinimumPanelWidthPx = 255;

  explicit GameControls(QWidget* parent = nullptr);

  /// Sets home/away team names on the top row (from game setup). Empty names use "home"/"away".
  void setSessionTeamNames(const QString& homeName, const QString& awayName);

  /// Selects home or away on the top row (e.g. after loading a session so tagging can start immediately).
  void setInitialTeamSide(bool selectHome);

  /// Canonical side for tagging: \c "Home", \c "Away", or empty if neither is selected.
  QString selectedTeamSideKey() const;

  void applyUiLanguage();

signals:
  void mainEventPressed(const QString& mainEvent);
  void gameEventMarked(const QString& mainEvent, const QString& followUpEvent = QString());
  /// Emitted when the user picks home or away on the top row (isHome true = home team).
  void teamSideSelected(bool isHome);

private slots:
  void onHomeTeamButtonClicked();
  void onAwayTeamButtonClicked();
  void onMainButtonClicked();
  void onFollowUpButtonClicked();

private:
  void buildUi();
  void wireSignals();
  void buildKeyboardShortcuts();
  void showFirstLevelFollowUps(const QString& mainEvent);
  void showSecondLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp);
  void showThirdLevelFollowUps();
  void hideFollowUpButtons();
  void configureMainGameControlButton(QPushButton* button, const QString& eventName,
                                      const QString& shortcutHint);
  QStringList getFirstLevelFollowUps(const QString& mainEvent) const;
  QStringList getSecondLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp) const;
  QStringList getThirdLevelFollowUps(const QString& mainEvent, const QString& firstFollowUp,
                                     const QString& secondFollowUp) const;
  QString selectedTeamLabel() const;
  /// After possession changes to the other side (e.g. Turnover), select the other team for the next tag.
  void switchTeamSideToOppositeTeam();
  void updateTeamButtonSelectionVisual();
  void flashButtonBorder(QPushButton* button);
  void setActiveMainButton(QPushButton* button);
  void clearActiveMainButton();
  QList<QPushButton*> focusableButtonsOrder() const;
  void focusNextInDirection(Qt::Key key);
  void applyTeamOnlyTabNavigation(bool forwardTab);

protected:
  bool eventFilter(QObject* obj, QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

  enum class FollowUpStage {
    None,
    FirstLevel,
    SecondLevel,
    ThirdLevel,
  };

  enum class TeamSideSelection {
    None,
    Home,
    Away,
  };

  QGridLayout* mainGridLayout_ = nullptr;
  QHBoxLayout* followUpLayout_ = nullptr;
  QWidget* followUpContainer_ = nullptr;

  QPushButton* homeTeamButton_ = nullptr;
  QPushButton* awayTeamButton_ = nullptr;

  QPushButton* hit16ydButton_ = nullptr;
  QPushButton* hit50ydButton_ = nullptr;
  QPushButton* hit75ydButton_ = nullptr;
  QPushButton* pcButton_ = nullptr;
  QPushButton* enterDButton_ = nullptr;
  QPushButton* pcFoulButton_ = nullptr;
  QPushButton* shotButton_ = nullptr;
  QPushButton* goalButton_ = nullptr;
  QPushButton* passButton_ = nullptr;
  QPushButton* specialButton_ = nullptr;
  QPushButton* turnoverButton_ = nullptr;
  QPushButton* cardButton_ = nullptr;
  QPushButton* shootoutButton_ = nullptr;
  QPushButton* psButton_ = nullptr;

  // keyboard shortcuts (same row-major order as on-screen grid):
  QAction* hit16ydAction_ = nullptr;
  QAction* hit50ydAction_ = nullptr;
  QAction* hit75ydAction_ = nullptr;
  QAction* pcAction_ = nullptr;
  QAction* enterDAction_ = nullptr;
  QAction* pcFoulAction_ = nullptr;
  QAction* shotAction_ = nullptr;
  QAction* goalAction_ = nullptr;
  QAction* passAction_ = nullptr;
  QAction* specialAction_ = nullptr;
  QAction* turnoverAction_ = nullptr;
  QAction* cardAction_ = nullptr;
  QAction* shootoutAction_ = nullptr;
  QAction* psAction_ = nullptr;
  QList<QAction*> followUpNumberActions_;
  QAction* escapeAction_ = nullptr;

  QString currentMainEvent_;
  QString currentFirstFollowUp_;
  QString currentSecondFollowUp_;
  FollowUpStage followUpStage_ = FollowUpStage::None;
  QPushButton* activeMainButton_ = nullptr;
  QList<QPushButton*> followUpButtons_;
  QHash<QPushButton*, QTimer*> flashTimers_;

  QString homeTeamFollowUpLabel_ = QStringLiteral("home");
  QString awayTeamFollowUpLabel_ = QStringLiteral("away");
  TeamSideSelection teamSideSelection_ = TeamSideSelection::None;

  QVector<QLabel*> mainButtonTitleLabels_;
};

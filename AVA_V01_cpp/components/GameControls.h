#pragma once

#include <QWidget>
#include <QStringList>
#include <QList>
#include <QVector>

#include "FollowUpState.h"
#include "../state/TagSession.h"

class QPushButton;
class QLabel;
class QGridLayout;
class QHBoxLayout;
class QWidget;
class QKeyEvent;

class GameControls final : public QWidget {
  Q_OBJECT

public:
  /// Minimum width of the control grid (button column content), in device pixels.
  static constexpr int kMinimumPanelWidthPx = 255;

  explicit GameControls(QWidget* parent = nullptr);
  ~GameControls() override;

  /// Sets home/away team names and jersey colors on the top row (from game setup).
  /// Empty names use "home"/"away"; empty colors keep the default selected border.
  void setSessionTeamNames(const QString& homeName, const QString& awayName,
                           const QString& homeColorHex = QString(),
                           const QString& awayColorHex = QString());

  /// Selects home or away on the top row (e.g. after loading a session so tagging can start immediately).
  void setInitialTeamSide(bool selectHome);

  /// Canonical side for tagging: \c "Home", \c "Away", or empty if neither is selected.
  QString selectedTeamSideKey() const;

  /// Resets the Start Game / Next Quarter UI back to the "no game started" state.
  void resetGameTimeState();

  /// Syncs Start/Next Quarter buttons with imported or restored session quarter state.
  void restoreGamePhase(TagSession::QuarterPhase phase, int currentQuarterIndex);

  /// Returns the canonical period name (\c "Q1".."Q4") of the quarter currently in progress,
  /// or an empty string when no quarter is in progress.
  QString currentPeriodName() const;

  void applyUiLanguage();

signals:
  /// Playhead timestamp captured when the user presses a main-event button (before follow-ups).
  void mainEventTimestampCaptured(const QString& mainEvent);
  /// Follow-up flow finished (or skipped); WorkWindow should create the GameTag.
  void tagCommitted(const QString& mainEvent, const QString& followUpEvent = QString());
  /// Emitted when the user picks home or away on the top row (isHome true = home team).
  void teamSideSelected(bool isHome);
  /// Emitted when the user clicks the Start Game button.
  /// WorkWindow is expected to insert the start-anchor tag and start Q1.
  void gameStartRequested();
  /// Emitted when the user clicks the Next Quarter button.
  /// WorkWindow is expected to close the current quarter and (if applicable) open the next one.
  void nextQuarterRequested();

private slots:
  void onHomeTeamButtonClicked();
  void onAwayTeamButtonClicked();
  void onMainButtonClicked();
  void onFollowUpButtonClicked();
  void onStartGameButtonClicked();
  void onNextQuarterButtonClicked();

private:
  void buildUi();
  void wireSignals();
  bool handleApplicationShortcut(QKeyEvent* event);
  void showFirstLevelFollowUps();
  void presentFollowUpChoices(const QStringList& actions, FollowUpState::Stage stage);
  void hideFollowUpButtons();
  void configureMainGameControlButton(QPushButton* button, const QString& eventName,
                                      const QString& shortcutHint);
  void advanceFollowUpFlow(const QString& choice);
  void commitFollowUpFlow();
  void cancelFollowUpFlow();
  void beginChainedGoalFlow();
  void clearFollowUpUi();
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
  void updateGameTimeButtonsUi();

protected:
  bool eventFilter(QObject* obj, QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

  enum class TeamSideSelection {
    None,
    Home,
    Away,
  };

  enum class GamePhase {
    NotStarted,        // Start Game enabled; Next Quarter disabled.
    Q1, Q2, Q3, Q4,    // Quarter in progress.
    Ended,             // All quarters closed; both buttons disabled.
  };

  QGridLayout* mainGridLayout_ = nullptr;
  QHBoxLayout* followUpLayout_ = nullptr;
  QWidget* followUpContainer_ = nullptr;

  QPushButton* startGameButton_ = nullptr;
  QPushButton* nextQuarterButton_ = nullptr;
  QLabel* quarterStatusLabel_ = nullptr;

  QPushButton* homeTeamButton_ = nullptr;
  QPushButton* awayTeamButton_ = nullptr;

  QPushButton* sixteenYardButton_ = nullptr;
  QPushButton* fiftyYardButton_ = nullptr;
  QPushButton* seventyFiveYardButton_ = nullptr;
  QPushButton* pcButton_ = nullptr;
  QPushButton* circleEntryButton_ = nullptr;
  QPushButton* pcFoulButton_ = nullptr;
  QPushButton* shotButton_ = nullptr;
  QPushButton* goalButton_ = nullptr;
  QPushButton* passButton_ = nullptr;
  QPushButton* specialButton_ = nullptr;
  QPushButton* turnoverButton_ = nullptr;
  QPushButton* cardButton_ = nullptr;
  QPushButton* shootoutButton_ = nullptr;
  QPushButton* psButton_ = nullptr;

  FollowUpState followUpState_{};
  GamePhase gamePhase_ = GamePhase::NotStarted;
  QPushButton* activeMainButton_ = nullptr;
  QList<QPushButton*> followUpButtons_;

  QString homeTeamFollowUpLabel_ = QStringLiteral("home");
  QString awayTeamFollowUpLabel_ = QStringLiteral("away");
  QString homeTeamColorHex_;
  QString awayTeamColorHex_;
  TeamSideSelection teamSideSelection_ = TeamSideSelection::None;

  QVector<QLabel*> mainButtonTitleLabels_;
};

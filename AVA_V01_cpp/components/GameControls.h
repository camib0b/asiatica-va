#pragma once

#include <QColor>
#include <QHash>
#include <QWidget>
#include <QStringList>
#include <QList>
#include <QVector>
#include <array>

#include "FollowUpState.h"
#include "../state/EventCatalog.h"
#include "../state/TagSession.h"

class QPushButton;
class QLabel;
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

  /// Selects the side used for the next tag. Unspecified clears the selection.
  void setTeamSide(TagSession::TeamSide side);

  /// Side that will be stored on the next tag. GameControls is the only owner.
  TagSession::TeamSide selectedTeamSide() const { return teamSide_; }

  /// Resets the period-advance UI back to the "no game started" state.
  void resetGameTimeState();

  /// Syncs the period-advance button and quarter track with imported or restored session quarter state.
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
  /// One step of the on-video confirmation. \c startsChain replaces the chain (main event).
  /// \c chainOpen stays true while follow-up questions remain.
  void tagSequenceAdvanced(const QString& shortcutKey, const QString& stepLabel, const QColor& teamColor,
                           bool chainOpen, bool startsChain);
  /// Esc ended the open follow-up. tagCommitted still records the main event with an empty path.
  void tagSequenceDismissed();
  /// Emitted when the user advances from NotStarted (period-advance button / G or H).
  /// WorkWindow asks the session to insert the start-anchor tag, which opens Q1.
  void gameStartRequested();
  /// Emitted when the user advances from Q1–Q4 (period-advance button / G or H).
  /// WorkWindow asks the session to close the current quarter.
  void nextQuarterRequested();

private slots:
  void onHomeTeamButtonClicked();
  void onAwayTeamButtonClicked();
  void onMainButtonClicked();
  void onFollowUpButtonClicked();
  void onPeriodAdvanceButtonClicked();

private:
  void buildUi();
  void wireSignals();
  bool handleApplicationShortcut(QKeyEvent* event);
  void showFirstLevelFollowUps();
  void presentFollowUpChoices(const QStringList& actions, FollowUpState::Stage stage);
  void hideFollowUpButtons();
  void configureMainGameControlButton(QPushButton* button, const QString& eventName,
                                      const QString& shortcutHint);
  void bindMainEventButton(QPushButton* button, EventCatalog::EventId eventId);
  void configurePeriodAdvanceButton(QPushButton* button, const QString& shortcutHint);
  void advanceFollowUpFlow(const QString& choice);
  void commitFollowUpFlow();
  void cancelFollowUpFlow();
  void beginChainedGoalFlow();
  void clearFollowUpUi();
  QString selectedTeamLabel() const;
  QColor selectedTeamColor() const;
  void notifyTagSequence(const QString& shortcutKey, const QString& stepLabel, bool chainOpen,
                         bool startsChain);
  void refreshTeamButtonLabels();
  std::array<QPushButton*, 14> keyboardOrderButtons() const;
  /// After possession changes to the other side (e.g. Turnover), select the other team for the next tag.
  void switchTeamSideToOppositeTeam();
  void updateTeamButtonSelectionVisual();
  void refreshMainButtonTitles();
  void flashButtonBorder(QPushButton* button);
  void setActiveMainButton(QPushButton* button);
  void clearActiveMainButton();
  QList<QPushButton*> focusableButtonsOrder() const;
  void focusNextInDirection(Qt::Key key);
  void applyTeamOnlyTabNavigation(bool forwardTab);
  void updateGameTimeUi(bool animateQuarterChange);
  void updateQuarterTrack(bool animateQuarterChange);

protected:
  bool eventFilter(QObject* obj, QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

  enum class GamePhase {
    NotStarted,        // Period-advance enabled; starts the game.
    Q1, Q2, Q3, Q4,    // Quarter in progress; next click starts the next Q or ends the game.
    Ended,             // All quarters closed; period-advance disabled.
  };

  QHBoxLayout* followUpLayout_ = nullptr;
  QWidget* followUpContainer_ = nullptr;

  QPushButton* periodAdvanceButton_ = nullptr;
  QLabel* periodAdvanceTitleLabel_ = nullptr;
  QWidget* quarterTrack_ = nullptr;
  std::array<QWidget*, 4> quarterSegments_{};
  std::array<QLabel*, 4> quarterTrackLabels_{};
  std::array<QString, 4> quarterSegmentStates_{};

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
  QHash<int, QPushButton*> mainEventButtonsByKey_;

  FollowUpState followUpState_{};
  GamePhase gamePhase_ = GamePhase::NotStarted;
  QPushButton* activeMainButton_ = nullptr;
  QList<QPushButton*> followUpButtons_;

  QString homeTeamFollowUpLabel_ = QStringLiteral("home");
  QString awayTeamFollowUpLabel_ = QStringLiteral("away");
  QString homeTeamColorHex_;
  QString awayTeamColorHex_;
  TagSession::TeamSide teamSide_ = TagSession::TeamSide::Unspecified;

  QVector<QLabel*> mainButtonTitleLabels_;
};

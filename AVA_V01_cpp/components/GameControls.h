#pragma once

#include <QColor>
#include <QHash>
#include <QPointer>
#include <QWidget>
#include <QStringList>
#include <QList>
#include <QVector>
#include <array>

#include "FollowUpState.h"
#include "../state/EventCatalog.h"
#include "../state/EventLabelOverrides.h"
#include "../state/TagSession.h"

class QPushButton;
class QLabel;
class QGridLayout;
class QShowEvent;
class QResizeEvent;
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

  /// Paints the period-advance control from the tag session. The session is the
  /// only owner of the quarter; this does not store a second phase.
  void resetGameTimeState();

  /// Returns the canonical period name (\c "Q1".."Q4") of the quarter currently in progress,
  /// or an empty string when no quarter is in progress.
  QString currentPeriodName() const;

  /// Button titles, shortcut letters, and hidden controls. Canonical names stay
  /// on the buttons so commits do not change. A hidden button stays in the grid
  /// and does not tag. The first visible button in keyboard order keeps a key
  /// when two events share it.
  void setEventLabelOverrides(const EventLabelOverrides::Table& overrides);

  /// Follows label, shortcut, and hidden edits on this session.
  void setTagSession(TagSession* session);

  void applyUiLanguage();

signals:
  /// Playhead timestamp captured when the user presses a main-event button (before follow-ups).
  void mainEventTimestampCaptured(const QString& mainEvent);
  /// Follow-up flow finished (or skipped); WorkWindow should create the GameTag.
  void tagCommitted(const QString& mainEvent, const QString& followUpEvent = QString());
  /// One step of the on-video confirmation. \c startsChain replaces the chain (main event).
  /// \c chainOpen stays true while follow-up questions remain.
  /// \p stepLabel is the canonical token. \p stepEventId is the catalog id the
  /// toast resolves, so a rename does not change the token tests and commits use.
  void tagSequenceAdvanced(const QString& shortcutKey, const QString& stepLabel, const QColor& teamColor,
                           bool chainOpen, bool startsChain, EventCatalog::EventId stepEventId);
  /// Esc ended the open follow-up. tagCommitted still records the main event with an empty path.
  void tagSequenceDismissed();
  /// Emitted when the user advances from NotStarted (period-advance button / G or H).
  /// WorkWindow asks the session to insert the start-anchor tag, which opens Q1.
  void gameStartRequested();
  /// Emitted when the user advances from Q1–Q4 (period-advance button / G or H).
  /// WorkWindow asks the session to close the current quarter.
  void nextQuarterRequested();
  /// Gear menu and the tagging-grid context menu open the full-screen editor.
  void editGameControlsRequested();

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
                         bool startsChain, EventCatalog::EventId stepEventId);
  void refreshTeamButtonLabels();
  const QList<QPushButton*>& keyboardOrderButtons() const;
  /// After possession changes to the other side (e.g. Turnover), select the other team for the next tag.
  void switchTeamSideToOppositeTeam();
  void updateTeamButtonSelectionVisual();
  void refreshMainButtonTitles();
  void applyControlChrome();
  void refreshOpenFollowUps();
  QStringList visibleFollowUpChoices(const QStringList& actions) const;
  bool buttonIsSuppressed(const QPushButton* button) const;
  void installControlContextMenu(QPushButton* button);
  void flashButtonBorder(QPushButton* button);
  void setActiveMainButton(QPushButton* button);
  void clearActiveMainButton();
  QList<QPushButton*> focusableButtonsOrder() const;
  void focusNextInDirection(Qt::Key key);
  void applyTeamOnlyTabNavigation(bool forwardTab);
  enum class DisplayedQuarter {
    NotStarted,
    Q1,
    Q2,
    Q3,
    Q4,
    Ended,
  };

  DisplayedQuarter displayedQuarter() const;
  void syncQuarterFromSession();
  void updateGameTimeUi(bool animateQuarterChange);
  void updateQuarterTrack(bool animateQuarterChange);
  void updateStackedButtonHeight(QPushButton* button);
  void updateAllStackedButtonHeights();
  void refreshFollowUpTitles();
  void refreshPeriodButtonTitle();
  void relayoutFollowUpButtons();
  int columnCountForFollowUps(int availableWidth) const;
  void clearFollowUpPreview();
  void previewMainEventFollowUps(const QString& mainEvent);
  void buildBreakToggle();
  void updateBreakToggleLabels();
  void positionBreakToggle();
  void applyBreakFixture(int fixtureId);
  void scheduleFittedRefresh();
  void refreshFittedControls();

protected:
  bool eventFilter(QObject* obj, QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;

  QGridLayout* followUpLayout_ = nullptr;
  QWidget* followUpContainer_ = nullptr;
  int followUpColumnCount_ = 1;

  QPushButton* periodAdvanceButton_ = nullptr;
  QLabel* periodAdvanceTitleLabel_ = nullptr;
  QWidget* quarterTrack_ = nullptr;
  std::array<QWidget*, 4> quarterSegments_{};
  std::array<QLabel*, 4> quarterTrackLabels_{};
  std::array<QString, 4> quarterSegmentStates_{};

  QPushButton* homeTeamButton_ = nullptr;
  QPushButton* awayTeamButton_ = nullptr;

  /// Main-event buttons in catalog keyboard order (row, then column).
  QList<QPushButton*> mainEventButtons_;
  QHash<int, QPushButton*> mainEventButtonsByKey_;
  QHash<int, QPushButton*> mainEventButtonsById_;

  FollowUpState followUpState_{};
  QPushButton* activeMainButton_ = nullptr;
  QList<QPushButton*> followUpButtons_;

  QString homeTeamFollowUpLabel_ = QStringLiteral("home");
  QString awayTeamFollowUpLabel_ = QStringLiteral("away");
  QString homeTeamColorHex_;
  QString awayTeamColorHex_;
  TagSession::TeamSide teamSide_ = TagSession::TeamSide::Unspecified;
  EventLabelOverrides::Table labelOverrides_;
  QPointer<TagSession> tagSession_;
  QMetaObject::Connection labelOverrideConnection_;
  QMetaObject::Connection quarterPhaseConnection_;
  /// Set for the click that asked the session to advance. The session signal
  /// consumes it. A refused advance never changes the painted quarter.
  bool animateNextQuarterChange_ = false;

  QVector<QLabel*> mainButtonTitleLabels_;

  bool arrangingFollowUps_ = false;
  bool applyingTitleFit_ = false;
  bool stackedHeightUpdateQueued_ = false;

  QWidget* breakUiToggle_ = nullptr;
  bool breakFixtureActive_ = false;
  EventLabelOverrides::Table breakSavedOverrides_;
  QString breakSavedHomeName_;
  QString breakSavedAwayName_;
  QString breakSavedHomeColor_;
  QString breakSavedAwayColor_;
};

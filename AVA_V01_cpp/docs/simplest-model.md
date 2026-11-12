# The simplest model of AVA

AVA is a match the coach tags while a video plays. Tagging mode and the game-controls panel are the core. Everything else (analysis, presentation, export) reads the tags that panel writes.

One screen is visible at a time: welcome, then game setup, then the work window. Inside the work window the coach is in exactly one mode, and the video is in exactly one open stage. The tag list, the quarter, and the selected team are what a tagging action actually changes.

This note records the data the coach can see, which actions can be reordered, what has to be true first, and four kinds of trouble in the code. Every finding names the file and the function.

## User-visible state

### Tagging and game controls

These are the facts that change the tagging screen. Each one has a single owner. A second copy is called out in the hunt below.

| What the coach sees | Owner | Where it is written |
| --- | --- | --- |
| Home or Away selected for the next tag, or neither | `GameControls::teamSide_` | `GameControls::setTeamSide` |
| Home and away names and jersey colors on the team buttons | Copied from `TagSession` | `GameControls::setSessionTeamNames` |
| Which quarter is painted, and whether Start / Q2 / Q3 / Q4 / End is enabled | `TagSession` quarter phase | `TagSession::commitQuarterPhase`. The panel only paints in `GameControls::updateGameTimeUi` |
| The follow-up question on screen (none, first, second, or third) | `GameControls::followUpState_` | `FollowUpState::beginMainEvent`, `select`, `setStage`, `takeCommit`, `reset` |
| Which main-event button looks pressed | `GameControls::activeMainButton_` | `GameControls::setActiveMainButton` |
| Button titles, shortcut letters, and hidden holes | A copy of the session override table | `GameControls::setEventLabelOverrides` |
| The toast over the video (`D  Shot › On target`) | `TagToastOverlay` steps, key, color, and whether the chain is still open | `WorkWindow::showTagSequenceStep` writes it through `TagToastOverlay::beginEvent` and `appendFollowUp`. `dismissImmediately` clears it |
| The mark that will be stored when the tag commits | `WorkWindow::capturedMainEvent_` | `WorkWindow::rememberCapturedMainEvent` |
| The tag list: event, team, time, note | `TagSession::tags_` | `TagSession::addTag` from `WorkWindow::commitTagFromGameControls` |
| The quarter stored on that new tag | Derived from the mark | `TagSession::periodRecordedForNewTag` |
| Event counts in the filter and, later, the stats tree | Rebuilt from `tags_` | `TagSession::rebuildEventCountsFromTags` |

Mode and video stage gate whether this panel is even on screen. They are not tagging data, but tagging cannot be understood without them.

- **Mode** is Tagging, Analyzing, or Presenting. `WorkWindow::transitionMode` is the only writer of `mode_`. Tagging puts the controls on the left (`WorkWindow::placeSidePanelForMode`). Analyzing shows the stats tree. Presenting shows the clip queue. `WorkWindow::showModeSidePage` must switch the stack; leaving it unset keeps the previous page.
- **Video stage** is NoVideo, Preparing, VideoOpen, or Replacing. `VideoOpenStage::reduce` decides. `WorkWindow::commitVideoStage` is the only writer. XML import and export are allowed only in VideoOpen.

The follow-up walk is the state machine inside the panel:

1. Idle. `FollowUpState::isIdle` is true when the stage is `None` or the main-event name is empty. `beginMainEvent` resets first, then stores the name, and leaves the stage at `None` until the choices are shown.
2. A main event is chosen. `GameControls::onMainButtonClicked` stores the name, paints the button, asks `WorkWindow` to remember the playhead, and shows the first question.
3. Each answer appends one canonical name (`GameControls::advanceFollowUpFlow`). `FollowUpCatalog::optionsAfter` reads the catalog. Depth stops at three.
4. Commit. `FollowUpState::takeCommit` copies the walk and resets it. `GameControls::commitFollowUpFlow` emits `tagCommitted`. It switches the team only when `FollowUpCatalog::switchesTeamOnCommit` is true and the payload is not empty. It then starts a goal chain when `FollowUpCatalog::continuesAsGoal` is true. Depth stops at `EventCatalog::kMaxFollowUpDepth` (3).
5. Escape. `GameControls::cancelFollowUpFlow` commits the main event with an empty follow-up path. The answers already shown are dropped.

`TaggingKeys::decide` is the only function that chooses what a shortcut does after the panel has decided it is allowed to see the key. `GameControls::handleApplicationShortcut` still owns the buttons and performs the click.

### The rest of what the coach sees

These matter after a tag exists. They do not decide the tagging walk.

- **Playhead, playing, rate, mute.** `QMediaPlayer` is the playback truth. `VideoPlayer` also stores `playbackRate_` and pushes it to `VideoControlsBar`. Mute is stored on the audio output and again on the controls bar (`VideoControlsBar::setMuted`). The timeline shows a third position while a scrub is in flight (`TimelineBar::setPositionMs`).
- **Tags table highlight.** `TagsTableModel` highlights rows near the playhead. The row text is rebuilt from the session, not stored as a second tag list.
- **Notes.** A clip note lives on the tag. An unsaved edit lives in `PendingClipNote` until `NotesController` flushes it. The match note is one HTML string on the session.
- **Stats.** `StatsWindow` counts tags. It does not keep its own copy of the quarter.
- **Presentation queue.** `PresentationQueue` stores selected tag ids and a current index. `PresentationPanel::selectedTagIdSet_` is the checkbox copy. Lead and lag are the tag interval, written back through `TagSession::setTagInterval`.
- **Presentation playback.** `PresentationPlayback::reduce` decides seek, play, and pause. `PresentationPlaybackController::commitPresentationPlayback` is the only assignment of that state. Auto-pause is armed at the current clip's end.
- **Drawings.** `Telestration::Session` holds them. They show on the video in presentation and go into the match file. They are omitted from the Sportscode XML.
- **Export jobs.** `ExportJobManager::finishJob` is the only writer of a job's phase. The jobs bar paints the snapshot.
- **Language.** `AppLocale` plus `LocaleNotifier`. A renamed event is not translated again. `EventLabelResolver` returns the stored label in both languages.
- **Session file.** `MatchSessionFile` persists team metadata, the match note, tags (including the stored period), control overrides, clip durations, the presentation selection, and drawings. It does not persist the selected team side, the open follow-up walk, the mode, or the playhead.

## Which actions commute

### Tagging

These can happen in either order, or again, and the visible result is the same:

- Picking Home and renaming a button. The team is `teamSide_`. The title is the override table. Neither reads the other.
- Changing language and committing a tag. The tag stores catalog ids. The label is resolved when something paints.
- Tagging two different events that do not continue into a goal and do not switch the team. Each tag is appended. The counts are rebuilt from the whole list in `TagSession::rebuildEventCountsFromTags`, so the order of the rebuild does not matter. The list order itself is the order of `addTag`, which the table shows.
- Hiding a button and changing another button's shortcut, as long as the two edits are different ids. `EventLabelOverrides` keeps one record per id.
- Holding a main-event key while no follow-up question is open, for an event with no follow-ups (50-yd is `W`). Each repeat appends another tag. `TaggingKeys::decide` still returns `ActivateMainEvent` in that case.

These care about order. Doing them backwards is a different match:

- Select a team, then press the event. `GameControls::onMainButtonClicked` returns without a tag when `teamSide_` is `Unspecified`. The press is gone.
- Press the main event, then answer 1, then 2. The path is the selection list in `FollowUpState`. Shot then 1 then 2 is On target › Saved (`TaggingToastTest::shotChainPublishesEachAnswer`). The other order is a different path.
- Escape versus a later answer. Escape commits the main event and drops the answers (`GameControls::cancelFollowUpFlow`). An answer after that starts nothing, because the walk is already idle.
- A new main event while a question is open. `FollowUpState::beginMainEvent` resets the old walk and does not commit it. The first event is abandoned. The toast starts over because `startsChain` is true (`TagToastOverlay::beginEvent` clears `steps_`).
- Start the game, then advance the quarter, then tag. `TagSession::startGame` only works from `NotStarted`. `advanceQuarter` only works from `QuarterInProgress`. The period stored on the new tag is the quarter that contains the mark (`TagSession::periodRecordedForNewTag`), so tagging before the advance and tagging after it disagree when the mark sits in the quarter that just closed.
- Turnover, then the next tag. `commitFollowUpFlow` flips the team only when the payload is not empty, and it does that before a chained goal starts. Escape commits an empty payload, so Escape on Turnover does not flip the team. The next tag after a real Turnover belongs to the other side.
- A follow-up whose catalog node has `continuesAsGoal`, then the automatic goal. The goal is a second tag, captured at the moment the chain commits (`GameControls::beginChainedGoalFlow`), not at the moment the original event was pressed. Time spent answering moves the goal's mark.
- G or H, then a tag, when that key is still the quarter key. The quarter advance is a tag of its own (`TagSession::advanceQuarter`). The play tag that follows sees the new spans. A G that was reassigned to a main event tags that event instead.
- Mode change during an open question. Leaving Tagging hides the panel, so shortcuts stop (`GameControls::handleApplicationShortcut` requires `isVisible`). The walk is not reset. `WorkWindow::updateTagToastForMode` suspends the toast and `TagToastOverlay::resumeOpenChain` shows it again on the way back. The answers are still in `followUpState_`.
- Tab between the team buttons. `GameControls::applyTeamOnlyTabNavigation` moves focus and selects that team. Tab is not a pure focus change.

Shortcut order inside one key press is fixed by `TaggingKeys::decide`:

1. A visible main-event shortcut wins, including a G or H that was reassigned to an event.
2. Otherwise G or H advances the quarter.
3. Otherwise 1–9 picks the visible follow-up at that index.
4. Otherwise Escape cancels an open question.
5. A repeated G or H is swallowed. A repeated tagging key is also swallowed while a question is open. A repeated arrow is ignored by this function, so focus still repeats.

### The rest of the app

These commute with tagging and with each other:

- Mute and playback rate. They do not write tags.
- A clip note on tag A and a clip note on tag B. Each note is one index. `TagSession::setTagNote` does not rebuild quarters.
- A drawing and a tag, unless the drawing is anchored to that tag's id. The drawing stores an optional tag id (`MatchSessionFile` writes `tagId`). Deleting the tag first leaves the drawing with an id the session no longer has.
- Language and a presentation filter. Filters use catalog ids.

These do not:

- Open a video, then tag, then close it. `WorkWindow::commitVideoStage` saves the previous match when a video is already open, then loads the session for the new paths. Closing saves first. Tagging with no video still needs a playhead; `WorkWindow::commitTagFromGameControls` returns immediately when `videoPlayer_` is missing, and `onGameStartRequested` returns when the player or the session is missing.
- Prepare, then commit, then replace. `VideoOpenStage::reduce` refuses Replace from NoVideo and from Preparing, and refuses a fresh BeginPreparation from VideoOpen.
- Next clip, then a playhead tick. `PresentationPlayback::reduce` arms auto-pause at the clip end. A tick that crosses that end pauses. A seek that jumps past it only disarms. The order of tick and seek is the difference between a pause and a silent disarm.
- Check clips, then edit a tag, then export. The queue rebuilds from ids (`PresentationQueue::rebuildClipsFromSession`). An export reads the intervals after that rebuild. Export during Replacing is refused (`WorkWindow::videoImportAndExportAllowed`).
- Import XML, then sync the offset, then map codes. The offset is applied to the instances before `XmlEventMappingDialog` builds tags. Mapping first and offsetting later would move different rows.
- Start an export, then cancel it. `ExportJobTransition::apply` only leaves `Exporting`. A cancel that arrives after success stays success (`ExportJobManager::finishJob`).

## Preconditions

### Before a tagging action

| Action | What must already be true | What happens when it is not |
| --- | --- | --- |
| Press a main event | A team is selected. The button is not hidden. The panel is visible. Focus is not in a text field. No modifier is held. | `onMainButtonClicked` returns, or `handleApplicationShortcut` returns false and the text field keeps the key |
| Answer 1–9 | A follow-up row is visible and the index is in range | `TaggingKeys::decide` returns `Ignore` |
| Escape | A follow-up prompt is open (`followUpState_` is not idle) | The key is ignored |
| G or H, or the period button | The game has not ended. For a real advance, a player and a session exist, and the session phase allows it | The button is disabled in `updateGameTimeUi` when the quarter is Ended. `startGame` / `advanceQuarter` return false. The panel does not paint a new quarter, because it only paints from `quarterPhaseChanged` |
| Commit a tag | `videoPlayer_` and `tagSession_` exist | `commitTagFromGameControls` returns |
| Start the game | Phase is `NotStarted` | `TagSession::startGame` returns false and adds nothing |
| Advance a quarter | Phase is `QuarterInProgress` and the index is 0..3 | `advanceQuarter` returns false |
| See tagging shortcuts | `GameControls` is visible, which means the tagging page is the current stack page | Analyzing and Presenting hide it |

Hidden buttons stay in the grid. `GameControls::applyControlChrome` gives a shared shortcut to the first visible button in keyboard order. A hidden button does not tag (`buttonIsSuppressed`).

### Before the rest

- XML import and XML or presentation export: video stage is `VideoOpen` (`VideoOpenStage::allowsXmlImportAndExport`).
- A presentation Next or Previous: the queue has clips. The first press plays the clip the queue is already on. Later presses move, and they stop at the ends (`PresentationPlayback::reduce`).
- Export of a job: the output path is free, and `finishJob` is still in `Exporting`.
- Save the match: the video stage is `VideoOpen` or `Replacing`, and a session is open (`WorkWindow::saveOpenMatchSession`). `MatchSessionFile::saveForSources` leaves a corrupt file and a newer-version file on disk (`SaveStatus::PreservedCorrupt`, `PreservedNewerVersion`). A missing file is a new match. `WorkWindow::restoreOpenedMatchSession` shows the one-line notice (`showSessionFileWarning`) for corrupt and newer files and does not apply them.
- Metadata suggestion: an API key is configured. Otherwise `GameMetadataSuggester::startSuggestionFromVideoPaths` finishes quietly and the setup form stays empty.

## Timing surprises

### Tagging

- **Key repeat and an open question.** A key repeat is a second press that arrives before the coach lets go. Quarter keys have always been swallowed, because holding G would close several quarters (`TaggingKeys::decide`, and previously the same check in `GameControls::handleApplicationShortcut`). Follow-up digits and main-event letters were not. Holding 1 while Shot's choices were up selected On target, and the next repeat would have taken the first choice of the next level. Holding D restarted Shot and moved `capturedMainEvent_` to the later time. `TaggingKeys::decide` now swallows those repeats while `followUpPromptOpen` is true. A real second press, with `isAutoRepeat` false, still answers. The smallest input that used to walk the chain, and no longer does, is in the trust-me section below.
- **A held letter with no question open still repeats.** 50-yd has no follow-ups, so each press commits immediately inside `GameControls::showFirstLevelFollowUps`. Holding W still appends a tag per repeat. That is the case the quarter comment deliberately did not include.
- **The quarter animation assumes the session signal is synchronous.** `GameControls::onPeriodAdvanceButtonClicked` sets `animateNextQuarterChange_`, emits `gameStartRequested` or `nextQuarterRequested`, then clears the flag. `WorkWindow::onGameStartRequested` calls `TagSession::startGame` on the same stack, and `commitQuarterPhase` emits `quarterPhaseChanged` before that returns. The panel's lambda reads the flag in time. A refused advance emits nothing, and the flag is cleared so a later import does not animate. If that connection were queued, the flag would already be false and the track would jump with no animation.
- **Escape and a chained goal do not stack.** Escape commits an empty path and clears the UI (`cancelFollowUpFlow`). It does not call `beginChainedGoalFlow`. A key-repeat Escape arrives after the walk is idle, so the second press is ignored. The partial answers the toast already showed are not in the tag. `TaggingToastTest::escapeKeepsTheMainTag` locks that.
- **The goal's mark is "now", the original event's mark is "when the button went down".** `rememberCapturedMainEvent` stores the playhead at the main-event press. `beginChainedGoalFlow` emits a new capture at commit time. If the video kept playing while the coach answered, the goal is later than the corner.
- **A quarter-key G during an open question still advances the quarter.** The first press is not a repeat. When G is not a main-event shortcut, `decide` returns `AdvanceQuarter`. The follow-up walk is left as it is. The tag that eventually commits then sees the new spans. A G that was reassigned starts that main event and abandons the open walk.

### The rest

- **Scrub versus player position.** `TimelineBar` keeps `isScrubbing_` and `waitingForSeekCommit_`. While a seek is outstanding, `setPositionMs` keeps showing the requested time until the player reports a position within 250 ms or 2 seconds pass. A position event from before the seek, arriving inside that window, does not move the label. A position that arrives after the timeout does, even if the seek has not landed.
- **Stall recovery and sleep.** `VideoPlayer::onPlaybackStallTimeout` waits through ticks where the position does not move, then nudges, then reloads the file. `onSystemWillSleep` snapshots position, rate, and whether playback was wanted, because after wake the player's own fields are not trustworthy. `onSystemDidWake` reloads 500 ms later. A wake that arrives with no earlier sleep captures the snapshot at wake (`onSystemDidWake` calls `onSystemWillSleep` when `sleepRecoveryPending_` is false).
- **Suggestion replies.** `GameMetadataSuggester` bumps `generation_` on every start and ignores a reply from an older generation. `onChatReplyFinished` reads `activeReply_`, which is "the reply we currently remember", not "the reply object that just finished". `abort` emits `finished` on this thread before `startSuggestionFromVideoPaths` posts the next request, so the old slot runs while `activeReply_` is already null. That saves the new request. A queued connection would let the old slot clear the new reply.
- **Thumbnail retry.** `onThumbnailProcessFinished` tries a second seek when the first frame is empty. The color request waits until both `namesDone_` and `thumbnailDone_` are true (`startColorsOrFinish`). Names that arrive first are held. Colors are never requested twice (`colorsRequested_`).
- **Export finish versus cancel.** A progress or finished signal can be queued after the job is already terminal. `finishJob` is a no-op once the phase has left `Exporting` (`ExportJobTransition::apply`). The message text is not parsed for the word "cancelled".
- **Presentation auto-pause.** A playhead step larger than `kMaxContinuousPlaybackAdvanceMs` (2 seconds) is treated as a seek. It disarms and does not pause (`PresentationPlayback::reduce`). Editing the clip end rearms only when the playhead is still before the new end. If the playhead is already past the new end, the old pause point stays (`reduce` on `ClipIntervalUpdated`). Playback can continue to the old end.

## Rules the code hopes are true

- **`FollowUpState::isIdle` means "no question is on screen".** It is true when the stage is `None` or the main-event name is empty (`FollowUpState::isIdle`). Right after `beginMainEvent`, the name is set and the stage is still `None`, so the walk is idle for that instant. `onMainButtonClicked` does not return to the event loop before `showFirstLevelFollowUps` sets the stage or commits. A nested call to `refreshOpenFollowUps` in that gap would see idle and do nothing. The code hopes nothing re-enters there.
- **The override copy matches the session.** `GameControls::labelOverrides_` is a full table, refreshed from `eventLabelOverridesChanged` in `setTagSession`. While `breakFixtureActive_` is true, that slot returns without copying. The break toggle (`applyBreakFixture`, only when `AVA_BREAK_UI` is set) is allowed to diverge on purpose. Any other writer that changed the session without the signal would leave the buttons on the old names.
- **The shortcut map matches the visible buttons.** `applyControlChrome` rebuilds `mainEventButtonsByKey_`. `bindMainEventButton` also inserts during construction, before that rebuild. The code hopes every later edit goes through `applyControlChrome`.
- **`capturedMainEvent_` names the walk that is about to commit.** `commitTagFromGameControls` uses the captured mark only when the event name matches. A commit whose name does not match uses the live playhead. `previewMainEventFollowUps` starts a walk for the break fixtures and does not emit `mainEventTimestampCaptured`. A commit from that preview would use whatever capture was left over, or the live playhead.
- **Quarter phase and the painted track agree.** The panel does not store a phase. It does store `quarterSegmentStates_` so `updateQuarterTrack` can see which segment just became current. That array is a memory of the last paint. If `updateGameTimeUi` were skipped after a real phase change, the next animation would compare against a stale string.
- **`currentPeriodName` is not the period of a new tag.** `GameControls::currentPeriodName` returns an empty string when the game has not started and when it has ended. `TagSession::currentPeriodLabel` returns Q4 after the whistle. New tags use `periodRecordedForNewTag`, which follows the mark. The panel name is only the accessibility string of the track while a quarter is in progress (`GameControls::updateQuarterTrack`).
- **A tag's stored `period` stays put when a later edit moves a quarter span.** `setTagInterval` on a quarter tag rebuilds the spans (`TagSession::restoreGameTimeStateFromTags`) and does not rewrite `period` on older play tags. Export does not read that field. `XmlExporter` computes the label with `periodLabelAtTimestampMs` inside the export loop. The file can still hold the old string.
- **Direct connections for the suggester and the quarter panel.** Cited above. Both are correct only while the slot runs before the caller continues.
- **Presentation checks and the queue name the same ids.** `PresentationPanel` keeps `selectedTagIdSet_`. `PresentationQueue` keeps `selectedTagIds_`. They are meant to move together through `WorkWindow::onPresentationSelectionChanged`. A refresh that pruned one and not the other would show a count the export does not play.

## The same fact in two places

- **The follow-up chain.** `FollowUpState::selections` holds canonical names. `TagToastOverlay::steps_` holds the display strings captured at each step. `GameControls::notifyTagSequence` only emits `tagSequenceAdvanced`. `WorkWindow::showTagSequenceStep` resolves the label and the toast stores that string. A rename while the question is open repaints the buttons from `labelOverrides_` (`refreshOpenFollowUps`) and leaves the toast text as it was. Escape then throws the selections away and dismisses the toast, so the two copies disappear together, and the tag has an empty path.
- **The mark.** `followUpState_` knows which event. `WorkWindow::capturedMainEvent_` knows the time. They are paired by event name in `commitTagFromGameControls`. A restart (`beginMainEvent`) emits a new capture. A reset that forgot the emit would leave the old time on the new event only when the names happened to match.
- **Button labels.** The session table is the one that is saved (`MatchSessionFile` writes `labelOverrides`). `GameControls::labelOverrides_` is the one the buttons read. The signal is the only bridge (`setTagSession`).
- **The quarter.** `TagSession` owns the phase, the in-progress index, the closed spans, and `gameTimeTags_`. The panel owns the painted segment states and `animateNextQuarterChange_`. `currentPeriodName` and `currentPeriodLabel` are two readings of that phase, and they disagree when the game has ended (empty versus Q4). The period stored on a tag is a third reading: the quarter that contained the mark at commit time. It is not updated when the spans move.
- **Team names.** The session stores the setup names. The panel stores `homeTeamFollowUpLabel_` and `awayTeamFollowUpLabel_`, and uses the words "home" and "away" when the setup name is empty (`setSessionTeamNames`). The payload for a TeamThenChain event uses the panel's label (`FollowUpCatalog::formatPayload` via `selectedTeamLabel`). The tag's `team` field stores the side key `Home` or `Away`, not that label.
- **Selected team side.** Only the panel has it. The session stores the side on each tag at commit. Reopening a match clears the selection back to `Unspecified` because the session file has no field for it. The next tag needs a fresh press.
- **Playback rate and mute.** `VideoPlayer::playbackRate_` and `VideoControlsBar::playbackRate_`. `QAudioOutput` mute and `VideoControlsBar::muted_`. `applyPlaybackRate` and `onMuteToggled` write both. A path that set only one would show a rate or a mute icon the player is not using.
- **Presentation selection.** Panel set and queue vector, both keyed by tag id. Cited above.

## Work that runs and never shows up

- **Escape drops answers the toast already showed.** `cancelFollowUpFlow` emits `tagCommitted` with an empty path. The coach saw On target. The tag row does not contain it. This is intentional (`tagSequenceDismissed` says Escape still records the main event). It is still work that reached the screen and then did not reach the tag.
- **Switching main events drops the open walk.** No `tagCommitted` for the abandoned event. The toast is replaced, so the old chain leaves the screen too. The playhead capture for the old event is overwritten.
- **Hidden follow-ups are computed and then skipped.** `visibleFollowUpChoices` asks the catalog for the full list, then drops hidden paths. If every choice is hidden, `presentFollowUpChoices` commits immediately. The coach never sees the question. The tag still exists.
- **`GameTag::period` on an old tag.** The tags table has time, team, and event (`TagsTableModel`). It does not show `period`. Presentation and the scoreboard call `periodLabelAtTimestampMs`. The stored field is written into the session file and is not what those screens read. After this change, a new tag's stored period matches the mark. An older tag's stored period can still disagree with the screen if a quarter span is edited later.
- **Suggestion failures.** `GameMetadataSuggester` finishes quietly when there is no key, no frame, or a bad response (`finishQuietly`, `handleNameDateResponse`). The setup form does not gain an error line. The coach sees an empty form, which is also what a slow request looks like until a suggestion arrives.
- **A suspended toast.** `TagToastOverlay::suspend` hides the window and keeps `steps_` and `chainOpen_`. Leaving Tagging is the only caller (`updateTagToastForMode`). Coming back shows it only when the chain is still open. A chain that already finished and was fading is not resumed, and the fade timer was stopped, so those steps stay in memory until the next `beginEvent` clears them. They are not on screen.
- **Drawings in the Sportscode XML.** They are saved in the session file and they are drawn on the video. The compilation export can burn them in when the include-drawings checkbox is on (`ExportSettingsDialog`, `ClipExporter`). `XmlExporter` does not write them.
- **Label overrides in the XML.** `XmlExporter::exportedEventName` writes the canonical English name, and its comment says a session label override must not reach `<code>`. The same rename does reach the compilation overlay and the sidecar file name, because `ExportClipBuilder` asks `EventLabelResolver::labelForKey`. Renaming an event leaves the XML `<code>` unchanged.

## Refactors in this branch

### Definitely the same

`TaggingKeys::decide` is the shortcut decision that used to be inlined in `GameControls::handleApplicationShortcut`. The buttons, the visibility checks, and the text-field check stayed in the widget. The choice moved.

What stayed the same, and is tested:

- A repeated G or H is swallowed, including a G that was reassigned to a main event (`shortcutDecisionKeepsQuarterRepeatAndFollowUpRepeatApart`).
- A non-repeated G that is a main-event shortcut tags that event instead of advancing the quarter.
- A repeated digit while a question is open used to choose that follow-up. The extraction preserved that, and the next commit changes it.
- Arrow keys are not tagging shortcuts.

### Trust me

**1. A repeated tagging key does nothing while a follow-up question is open.**

`TaggingKeys::decide` swallows the repeat when `followUpPromptOpen` is true and the key would otherwise tag, answer, cancel, or advance. A repeated letter while no question is open still tags. Arrow repeat still moves focus.

The smallest input that breaks the old behavior: Shot's follow-ups are on screen (key D, team already selected), then a `KeyPress` of `Qt::Key_1` with `isAutoRepeat == true` arrives. Previously the chain became On target. Now it stays on Shot until a press that is not a repeat. The same hold of D used to restart Shot and move the mark. It no longer does.

`TaggingToastTest::repeatedFollowUpDigitStaysOnTheOpenQuestion` sends that repeat to a real `GameControls` and then sends a non-repeating 1, which still selects On target.

The assumption this would get wrong: a coach who holds 1 on purpose to walk the first choice of every level in one hold. That hold now stops on the current question.

**2. A new tag stores the quarter that contains its mark.**

`WorkWindow::commitTagFromGameControls` used to copy `TagSession::currentPeriodLabel`, the quarter that is in progress. It now calls `TagSession::periodRecordedForNewTag`. That uses `periodLabelAtTimestampMs` when the mark falls in a span, and the in-progress label only when the mark is in no span (before the game, or a gap).

The smallest input that breaks the old behavior: `startGame(0)`, `advanceQuarter(10000)`, tag at mark `10000`. Q1's closed span is `[0, 10000]`. The in-progress quarter is Q2. The stored period is now Q1. `WorkStateTest::newTagKeepsTheClosedQuarterAtItsExactEnd` checks the function. `ExplicitStateMachineTest::tagAtTheClosedQuarterWhistleStoresThatQuarter` clicks 50-yd on a `WorkWindow` whose playhead is 0 after that same advance, and the tag's period is Q1.

XML export already used the mark (`XmlExporter`'s per-tag call to `periodLabelAtTimestampMs`). The session file was the copy that disagreed. The tags table never showed either string.

The assumption this would get wrong: a coach who seeks back into Q1 while Q2 is in progress and wants the tag filed as Q2 because that is the quarter they are "in". The file now says Q1, which is the quarter the mark sits in. Marks before the first span still get the in-progress label, including an empty label before the game starts.

## What was left alone

Packaging, CMake, and signing were not edited. The context menu, the control editor, button layout, and styles were not edited. Findings in those files are notes only.

The behaviors above that were not changed — Escape dropping the path, a new main event abandoning the walk, Tab also selecting the team, G advancing the quarter during a question, the synchronous quarter animation, the suggester's `activeReply_` — are the ones a later change would have to name a breaking input for.

# AVA V02 GUI Parity Checklist

## Theme and Styling
- [ ] `main.qss` matches V01 visual language (roles, variants, sizes).
- [ ] Dynamic properties repaint correctly (`role`, `variant`, `size`, `flash`, `activeMain`, `speedActive`).
- [ ] Object-name scoped styles work (`AppRoot`, `TimelineSlider`, `VideoControlsBarSlim`, `TaggingRightCol`).

## Flow
- [ ] `MainWindow` starts on welcome screen.
- [ ] Selecting a video transitions to team setup and then work screen.
- [ ] Closing/discarding video returns to welcome and clears session state.

## Video Playback
- [ ] Space toggles play/pause.
- [ ] Left/Right seeks ±250ms.
- [ ] Shift+Left/Right seeks ±3000ms.
- [ ] `{` / `}` changes playback speed.
- [ ] `\\` resets to 1.0x.
- [ ] Timeline click-to-seek works.
- [ ] Drag scrub pauses and resumes playback consistently.

## Tagging
- [ ] Main grid shortcuts: `QWE`, `ASD`, `ZXC`.
- [ ] Follow-up shortcuts: `1..9`, `Esc` cancels/finalizes empty follow-up.
- [ ] Pending timestamp uses main-event press timestamp.
- [ ] New tag row flashes.
- [ ] Backspace deletes selected tag.
- [ ] Undo shortcut removes most recent tag.

## Analysis
- [ ] `M` toggles Tagging/Analyzing layout.
- [ ] Tags list resizes correctly by mode.
- [ ] Notes area enables/disables with selection and saves debounced edits.
- [ ] Stats tree shows main + follow-up counts with percentages.
- [ ] Double-click stats path filters tags.
- [ ] Comma shortcut opens stats overlay in Tagging mode.

## Filters
- [ ] Filter menu has Select all / Select none + main events.
- [ ] Filter indicator updates text accurately.
- [ ] Remove filters resets both path and quick filters.

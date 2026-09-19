# Codebase Architecture

- main.cpp  is the application entry point containing main(). It initializes a QApplication with metadata and icon, applies a light theme, bootstraps configuration/locale/defaults/licensing, creates and shows the MainWindow on the stack, then runs the Qt event loop.

## UI Views

- WelcomeWindow builds a centered Qt UI for the app splash with title, import button (with dynamic width calc), license status, and enter-license link. It wires signals, applies i18n strings, and handles trial/paid license display logic
- GameSetupWindow is a Qt QWidget that builds a configurable form UI for home/away team names, abbreviations, colors, optional game date, and language selection; it integrates with GameMetadataSuggester for AI-driven auto-population from video paths and derives abbreviations via a simple character-collection routine.
- WorkWindow implements the main application UI with dynamic mode-based layouts (Tagging/Analyzing/Presenting), coordinating VideoPlayer, TagSession, splitters, tag table, notes, and presentation queue.
- MainWindow acts as the top-level Qt UI orchestrator, using a QStackedWidget to manage transitions between WelcomeWindow, WorkWindow, and LicenseLockOverlay. It drives video import/concatenation flows (via VideoConcatenator and QTemporaryDir), enforces license entitlement gating, maintains a TagSession, and handles locale and shutdown cleanup.
- LicenseLockOverlay is a Qt widget that renders a license activation screen (or lock message) with dynamic localized text, email/key inputs, action buttons, and language selection. It observes LicenseManager signals to refresh UI state and maps lock reasons to specific translated strings.
- StatsWindow is a Qt QWidget displaying hierarchical event statistics for a TagSession, with team (home/away/both) filtering via QButtonGroup/QToolButtons and a QTreeWidget showing main/follow-up counts plus percentages. Core design centers on signal-driven rebuildTree() that aggregates tags, applies filters, sorts case-insensitively, and populates tree items with UserRole metadata for double-click navigation.
- PresentationPanel is a Qt widget implementing a presentation panel for filtering, selecting, and exporting tagged clips from a TagSession, with a checkable table sorted by time, lead/lag spinbox controls for the current clip, and UI state synchronization.
- PresentationInstancesModel is a QAbstractTableModel for presentation-mode tag instances (time, team, event columns) with checkable rows, current-clip highlighting, and a RAII bulk-update guard that suppresses checkStateChanged during programmatic updates.
- ClipDurationSettingsDialog implements a Qt dialog for configuring per-event lead/lag clip durations in a grid layout, pulling defaults from EventDefaults, updating a TagSession on changes, and supporting full reset. It manually manages a dynamic set of labels and spinboxes with parented QWidget ownership.
- XmlEventMappingDialog presents a table UI for mapping XML event codes to canonical events/teams with auto-detection based on abbrev patterns (+/- suffixes), performs validation, and constructs ordered TagSession::GameTag objects with time offset and period inference.
- XmlSyncDialog is a Qt QDialog that lets users align XML-imported subtitle timings to a VideoPlayer by setting a time offset from an XML anchor point to the current video playhead. It builds a form with labels and buttons, updates a live preview of affected instances (including out-of-bounds clamping counts), and uses standard Qt raw-pointer + parent-child ownership for its widgets.

## Components

- FollowUpCatalog maintains static QHash tables of first-, second-, and third-level follow-up options for game events, plus payload-layout policy (team-only, team-then-chain, chain-then-team, or plain chain), arrow-path formatting, and helpers for team-switch-on-commit and goal-continuation.
- GameControls is a complex Qt QWidget for sports event tagging, featuring team selection with color styling, game-phase management for quarters, a main grid of event buttons with dynamic multi-level follow-up prompts, extensive keyboard shortcuts, arrow-key focus navigation, and signal emission for tag commits.
- MatchNotesEditor is a QTextEdit subclass providing rich-text editing for match notes with @mention autocomplete for tags via a QListWidget popup, inserting mentions as HTML anchors that emit activation signals on click while preserving scroll position and supporting HTML serialization.
- TeamColorPicker is a compact Qt widget providing a clickable color well that opens a popup with a fixed localized palette, live hex editor, and system color picker fallback. Key design uses a custom-painted ColorCircleButton (with luminance-based borders), manual popup positioning, and tight state synchronization across the well, swatches, and text field via normalized hex strings.
- TimelineBar is a Qt media timeline widget with a custom ClickSeekSlider supporting direct click-to-seek, a label that toggles to an editable time field on click, throttled scrub events, and stateful synchronization between user seeking and external position updates. Key design uses flags (isScrubbing_, waitingForSeekCommit_) plus a tolerance timer to avoid feedback loops.
- VideoControlsBar is a Qt QWidget implementing a horizontal layout of playback controls (play/pause/seek/speed/mute buttons plus speed label). It wires signals for commands and visual flash effects, manages application-wide keyboard shortcuts behind media/focus gates, updates localized UI strings, and maintains playback/mute state with style variants.
- VideoPlayer is a QWidget subclass that encapsulates QMediaPlayer, QVideoWidget, controls, and timeline for playback with seeking, rate control, keyboard shortcuts, stall detection/recovery, and macOS-specific system sleep/wake state restoration.

## State
- EventDefaults implements a namespace for managing factory-default and user-overridable lead/lag clip durations (in ms) for canonical game events, backed by a static QHash table and QSettings persistence under the 'clipDurations' group. Key design uses lazy loading of overrides via a static flag, special-casing for non-overridable time-control events, and historical settings key compatibility.
- PresentationQueue manages a sorted vector of selected clips synced bidirectionally with a TagSession, preserving current index across rebuilds, enforcing min durations and video bounds, and emitting Qt signals for UI updates.
- TagSession manages a mutable list of timed GameTags for event marking in game video, maintains derived counts per main/follow-up event, automatically derives quarter/period state from special anchor tags, and supports import with clamping, defaults, and Qt signals for UI reactivity. Core design centers on manual cache maintenance for counts and multi-path state reconstruction for game timing.
- EventCodeMap maintains two static QHash lookup tables for bidirectional conversion between canonical event names and short codes, with the reverse map built via immediate lambda inversion of the primary table.

## Export

- ClipExporter handles FFmpeg-driven video clipping with QPainter-generated overlays (text, scoreboards, branding), video probing for dimensions/rotation, scaled layout computation, and complex filter_complex graphs ensuring YouTube-safe positioning before concatenation.
- ClipTrimBar is a QWidget that renders an interactive timeline for clip trimming, with draggable start/end handles, playhead, event marker, range highlight and time labels. Core design relies on bidirectional ms<->pixel conversion functions, mouse-driven clamping to enforce min duration, and direct paintEvent drawing without layout delegates.
- ConcatFileOrderDialog is a Qt QDialog that lets users reorder source video files before concatenation via a wrapping QListWidget with drag-and-drop InternalMove, left/right move buttons, and UserRole-backed path retrieval.
- ExportClipBuilder supplies utilities for sanitized filename generation (for reports and compilations), clip sorting by team/time, and construction of ClipSegment objects that embed overlay text plus dynamically computed timed scoreboard phases from goal tags.
- ExportJobManager tracks and drives asynchronous ClipExporter jobs (MP4 video and/or XML reports), enforces output-path exclusivity, maintains job state/lifecycle, and publishes snapshots for UI consumption.
- ExportJobsBar is a QWidget that dynamically builds and updates rows of job status UI (name, status text, progress bar, optional cancel/dismiss buttons) by clearing and reconstructing its layout in response to ExportJobManager::jobsChanged signals. The design relies on Qt parent-child ownership for all widgets/layouts and direct lambda slots for per-job actions.
- ExportSettingsDialog implements a Qt QDialog for configuring export of clips or XML tag reports, with dynamic UI (format-dependent controls, auto path suggestions, checkboxes for overlays/notes/audio) driven by TagSession and queued clips. Key design centers on format switching logic, path validation, and result struct population before accept().
- GameMetadataSuggester coordinates two asynchronous LLM queries to xAI's chat API (structured JSON schema outputs) to infer game names/date from filenames and jersey colors from an ffmpeg-extracted video thumbnail. Key design uses generation counters, state flags (namesDone_/thumbnailDone_), and explicit abort paths to manage concurrent ffmpeg QProcess and QNetworkReply lifecycles.
- PlaybackVideoPreparer transcodes MKV/WebM files to a standardized MP4 (using libx264/aac) via an external FFmpeg QProcess for playback compatibility. Key design centers on async process management, cancel flags, synchronous waitWithProgress via nested QEventLoop, and basic error forwarding from stderr.
- VideoConcatenator orchestrates FFmpeg-based video concatenation via a generated concat_list.txt and QProcess with copy codec +faststart flags. Key design centers on async process management, modal QProgressDialog with QEventLoop synchronization, cancellation guards, and a drag/drop reordering dialog.
- XaiConfig implements a namespace that retrieves an API key preferring XAI_API_KEY (or legacy AVA_XAI_API_KEY) environment variables over JSON files in Qt standard config locations, with bootstrap() to persist the env key to primary config.
- XmlExporter converts TagSession game tags into a specific XML schema for video analysis tools (LongoMatch/Olympia), synthesizing extra Goal instances from follow-up paths, emitting dual +/− team codes with running scores, and producing an ordered palette in <ROWS>.
- XmlImporter parses a specific XML format for event instances (with start/end times, codes, and QUARTOS labels) using QXmlStreamReader driven by a manual state machine of boolean flags; it validates data, sorts by time+ID, and supplies an anchor-instance lookup with fallback logic.

## i18n

- AppLocale implements bilingual (EN/ES) localization for UI strings and field-hockey event taxonomy using static QHash lookup tables, global language state persisted via QSettings, and helper functions that compose/translate compound event paths while stripping team labels.
- LocaleNotifier implements a Qt QObject-based singleton for broadcasting language change notifications via signals. Uses static local variable for lazy-initialized instance and a simple notify method to emit the signal.

## License

- DeviceId implements avaStableDeviceId() for non-macOS platforms by hashing /etc/machine-id or QSysInfo::machineUniqueId() with SHA-256 (truncated), falling back to a UUID persisted in an app-data file. Key design relies on system identifiers for stability while using a simple file-based fallback with no synchronization or strong integrity controls.
- LicenseConfig implements priority-based lookup for a license server API URL (environment, QSettings, standard config JSONs, compile define, bundled file) with URL sanitization, a validity check, and bootstrap to deploy the default JSON to user config dirs.
- LicenseCrypto implements license token signing/verification and store record persistence using HMAC-SHA256 over base64url-encoded canonical JSON payloads; relies on custom string-based JSON serialization and fragile key-lookup parsing instead of a full parser.
- LicenseManager is a Qt-based singleton that handles license activation/validation via crypto tokens, manages local persistent state (settings + file), performs server checks over HTTP, implements tamper-resistant timing, local trials, and entitlement evaluation with grace periods and lock reasons.

## Style

- theme.cpp applies a light theme to the Qt application by switching to the Fusion style, configuring a platform-aware font (Inter with Apple fallback), and loading a QSS stylesheet from embedded resources via a small helper.

#pragma once

#include <QtGlobal>

#include <optional>
#include <variant>
#include <vector>

/// Pure playback decisions for presentation mode.
///
/// WorkWindow stores one State, dispatches an Action, and performs the returned
/// effects on the player. The reducer does not reference widgets or the media player.
namespace PresentationPlayback {

/// A playhead step larger than this is a seek. Auto-pause disarms and does not pause.
inline constexpr qint64 kMaxContinuousPlaybackAdvanceMs = 2000;

struct State {
  bool playbackStarted = false;
  bool autoPauseArmed = false;
  qint64 autoPauseAtMs = 0;
  qint64 lastPlayheadMs = -1;
};

struct ClipSpan {
  qint64 startMs = 0;
  qint64 endMs = 0;
};

struct QueueSnapshot {
  int currentIndex = -1;
  std::vector<ClipSpan> clips;
};

struct Next {
  QueueSnapshot queue;
};

struct Previous {
  QueueSnapshot queue;
};

/// The caller has already selected the queue index. Seek there and arm auto-pause.
struct OpenClip {
  qint64 startMs = 0;
  qint64 endMs = 0;
};

struct PlayheadTick {
  qint64 positionMs = 0;
  bool presenting = false;
  bool playerIsPlaying = false;
};

/// Leaving presentation mode. Disarms auto-pause. The started latch stays so the
/// next Next or Previous continues the queue.
struct ModeExit {};

/// The queue has no clips. Clears the started latch and auto-pause.
/// The last playhead is kept.
struct QueueEmptied {};

/// A video was closed or another one was loaded.
/// Clears the started latch, auto-pause, and the last playhead.
struct Reset {};

/// The current clip's end moved. Re-arms only when the playhead is still before it.
struct ClipIntervalUpdated {
  qint64 playheadMs = 0;
  qint64 clipEndMs = 0;
};

using Action = std::variant<Next,
                            Previous,
                            OpenClip,
                            PlayheadTick,
                            ModeExit,
                            QueueEmptied,
                            Reset,
                            ClipIntervalUpdated>;

struct Seek {
  qint64 positionMs = 0;
};

struct Play {};

struct Pause {};

using Effect = std::variant<Seek, Play, Pause>;

struct Result {
  State state;
  std::vector<Effect> effects;
  /// Queue index Next or Previous should select before the effects run.
  /// Empty when this transition does not move the queue.
  std::optional<int> queueIndex;
};

Result reduce(const State& state, const Action& action);

}  // namespace PresentationPlayback

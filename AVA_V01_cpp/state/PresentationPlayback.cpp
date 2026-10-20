#include "PresentationPlayback.h"

#include <algorithm>
#include <cstddef>

namespace PresentationPlayback {
namespace {

template <typename... Functions>
struct Overloaded : Functions... {
  using Functions::operator()...;
};

template <typename... Functions>
Overloaded(Functions...) -> Overloaded<Functions...>;

Result withState(const State& state) {
  return Result{state, {}, std::nullopt};
}

Result showClip(const State& state, const ClipSpan& clip) {
  State next = state;
  next.playbackStarted = true;
  next.lastPlayheadMs = clip.startMs;
  next.autoPauseAtMs = clip.endMs;
  next.autoPauseArmed = true;

  Result result;
  result.state = next;
  result.effects.push_back(Seek{clip.startMs});
  result.effects.push_back(Play{});
  return result;
}

/// The first Next or Previous starts the clip the queue is already on.
/// Later presses move, and they stop at the ends.
std::optional<int> navigationTarget(const State& state,
                                    const QueueSnapshot& queue,
                                    bool forward) {
  const int clipCount = static_cast<int>(queue.clips.size());
  if (clipCount <= 0) return std::nullopt;

  if (!state.playbackStarted) {
    const int startIndex = std::max(0, queue.currentIndex);
    if (startIndex >= clipCount) return std::nullopt;
    return startIndex;
  }

  if (forward) {
    if (queue.currentIndex >= 0 && queue.currentIndex < clipCount - 1) {
      return queue.currentIndex + 1;
    }
    return std::nullopt;
  }

  if (queue.currentIndex > 0) return queue.currentIndex - 1;
  return std::nullopt;
}

Result reduceNavigation(const State& state, const QueueSnapshot& queue, bool forward) {
  const std::optional<int> target = navigationTarget(state, queue, forward);
  if (!target.has_value()) return withState(state);

  Result result = showClip(state, queue.clips.at(static_cast<std::size_t>(*target)));
  result.queueIndex = *target;
  return result;
}

Result reduceOpenClip(const State& state, const OpenClip& action) {
  return showClip(state, ClipSpan{action.startMs, action.endMs});
}

Result reducePlayheadTick(State state, const PlayheadTick& tick) {
  if (!tick.presenting) {
    state.lastPlayheadMs = tick.positionMs;
    return withState(state);
  }

  const qint64 previousPositionMs = state.lastPlayheadMs;
  state.lastPlayheadMs = tick.positionMs;

  if (!state.autoPauseArmed || tick.positionMs < state.autoPauseAtMs) {
    return withState(state);
  }

  // Crossing the end during playback pauses once. A seek past the end only disarms.
  const bool crossedWhilePlaying = tick.playerIsPlaying && previousPositionMs >= 0 &&
                                   previousPositionMs < state.autoPauseAtMs &&
                                   (tick.positionMs - previousPositionMs) <=
                                       kMaxContinuousPlaybackAdvanceMs;

  state.autoPauseArmed = false;
  Result result = withState(state);
  if (crossedWhilePlaying) result.effects.push_back(Pause{});
  return result;
}

Result reduceModeExit(State state) {
  state.autoPauseArmed = false;
  return withState(state);
}

Result reduceQueueEmptied(State state) {
  state.playbackStarted = false;
  state.autoPauseArmed = false;
  return withState(state);
}

Result reduceReset(State state) {
  state.playbackStarted = false;
  state.autoPauseArmed = false;
  state.lastPlayheadMs = -1;
  return withState(state);
}

Result reduceClipIntervalUpdated(State state, const ClipIntervalUpdated& update) {
  if (update.playheadMs < update.clipEndMs) {
    state.autoPauseAtMs = update.clipEndMs;
    state.autoPauseArmed = true;
  }
  return withState(state);
}

}  // namespace

Result reduce(const State& state, const Action& action) {
  return std::visit(
      Overloaded{
          [&](const Next& next) { return reduceNavigation(state, next.queue, true); },
          [&](const Previous& previous) {
            return reduceNavigation(state, previous.queue, false);
          },
          [&](const OpenClip& openClip) { return reduceOpenClip(state, openClip); },
          [&](const PlayheadTick& tick) { return reducePlayheadTick(state, tick); },
          [&](const ModeExit&) { return reduceModeExit(state); },
          [&](const QueueEmptied&) { return reduceQueueEmptied(state); },
          [&](const Reset&) { return reduceReset(state); },
          [&](const ClipIntervalUpdated& update) {
            return reduceClipIntervalUpdated(state, update);
          },
      },
      action);
}

}  // namespace PresentationPlayback

#include "PresentationPlaybackController.h"

#include "../components/VideoPlayer.h"
#include "../export/ClipTrimBar.h"
#include "../i18n/AppLocale.h"
#include "../state/EventLabelResolver.h"
#include "../state/TagSession.h"
#include "PresentationPanel.h"
#include "TagRowDisplay.h"
#include "TextInputFocus.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cstddef>

namespace {

/// Sets a reentrancy flag for this scope and restores the value it had on entry.
class ScopedReentrancyFlag {
public:
  explicit ScopedReentrancyFlag(bool& flag) : flag_(flag), previousValue_(flag) { flag_ = true; }
  ~ScopedReentrancyFlag() { flag_ = previousValue_; }

  ScopedReentrancyFlag(const ScopedReentrancyFlag&) = delete;
  ScopedReentrancyFlag& operator=(const ScopedReentrancyFlag&) = delete;

private:
  bool& flag_;
  bool previousValue_;
};

/// Same rule as the export reviewer: leave room on both sides of the clip so the presenter can
/// stretch the interval or scrub past it without the bar clipping the view.
struct PresentationClipWindow {
  qint64 startMs = 0;
  qint64 endMs = 0;
};

PresentationClipWindow presentationClipWindow(const PresentationQueue::Clip& clip,
                                             qint64 videoDurationMs) {
  const qint64 halfWindowMs =
      std::max<qint64>(25000, std::max(clip.leadMs(), clip.lagMs()) + 5000);
  qint64 windowStartMs = clip.markMs - halfWindowMs;
  qint64 windowEndMs = clip.markMs + halfWindowMs;
  if (windowStartMs < 0) windowStartMs = 0;
  if (videoDurationMs > 0 && windowEndMs > videoDurationMs) windowEndMs = videoDurationMs;
  return PresentationClipWindow{windowStartMs, windowEndMs};
}

template <typename... Functions>
struct Overloaded : Functions... {
  using Functions::operator()...;
};

template <typename... Functions>
Overloaded(Functions...) -> Overloaded<Functions...>;

PresentationPlayback::QueueSnapshot queueSnapshot(const PresentationQueue& queue) {
  PresentationPlayback::QueueSnapshot snapshot;
  snapshot.currentIndex = queue.currentIndex();
  const int clipCount = queue.count();
  snapshot.clips.reserve(static_cast<std::size_t>(clipCount));
  for (int index = 0; index < clipCount; ++index) {
    const PresentationQueue::Clip& clip = queue.clipAt(index);
    snapshot.clips.push_back(PresentationPlayback::ClipSpan{clip.startMs, clip.endMs});
  }
  return snapshot;
}

void applyPresentationEffect(VideoPlayer& player, const PresentationPlayback::Effect& effect) {
  std::visit(Overloaded{
                 [&player](const PresentationPlayback::Seek& seek) {
                   player.seekToMs(seek.positionMs);
                 },
                 [&player](const PresentationPlayback::Play&) { player.playWithControlFlash(); },
                 [&player](const PresentationPlayback::Pause&) { player.pauseWithControlFlash(); },
             },
             effect);
}

}  // namespace

PresentationPlaybackController::PresentationPlaybackController(QObject* parent)
    : QObject(parent),
      tagSession_(nullptr),
      eventLabelOverridesConnection_(),
      videoPlayer_(nullptr),
      presentationQueue_(nullptr),
      presentationClipBar_(nullptr),
      presentationPanel_(nullptr),
      presentationEventLabel_(nullptr),
      presentationContextLabel_(nullptr),
      presentationNoteLabel_(nullptr),
      hostWindow_(nullptr),
      presentingQuery_(),
      presentationKeyboardShortcutsAttached_(false),
      savingClipIntervalFromBar_(false),
      presentationPlayback_(),
      telestrationSession_(nullptr),
      drawingFreezeTimer_(nullptr),
      drawingFreezeActive_(false),
      drawingFreezeSawPause_(false),
      drawingFreezeId_(0),
      drawingFreezeAnchorMs_(0),
      drawingFreezeCrossedFromMs_(-1),
      drawingFreezePausedAtMs_(-1),
      heldDrawingIds_() {
  drawingFreezeTimer_ = new QTimer(this);
  drawingFreezeTimer_->setSingleShot(true);
  connect(drawingFreezeTimer_, &QTimer::timeout, this,
          &PresentationPlaybackController::finishDrawingFreeze);
}

PresentationPlaybackController::~PresentationPlaybackController() {
  detachPresentationKeyboardShortcuts();
}

void PresentationPlaybackController::setTagSession(TagSession* tagSession) {
  if (tagSession_ == tagSession) return;
  if (eventLabelOverridesConnection_) {
    QObject::disconnect(eventLabelOverridesConnection_);
    eventLabelOverridesConnection_ = {};
  }
  tagSession_ = tagSession;
  if (!tagSession_) return;
  eventLabelOverridesConnection_ =
      connect(tagSession_, &TagSession::eventLabelOverridesChanged, this,
              &PresentationPlaybackController::updatePresentationStage);
}

void PresentationPlaybackController::setVideoPlayer(VideoPlayer* videoPlayer) {
  videoPlayer_ = videoPlayer;
}

void PresentationPlaybackController::setPresentationQueue(PresentationQueue* queue) {
  presentationQueue_ = queue;
}

void PresentationPlaybackController::setPresentationClipBar(ClipTrimBar* clipBar) {
  presentationClipBar_ = clipBar;
}

void PresentationPlaybackController::setPresentationPanel(PresentationPanel* panel) {
  presentationPanel_ = panel;
}

void PresentationPlaybackController::setStageLabels(QLabel* eventLabel,
                                                    QLabel* contextLabel,
                                                    QLabel* noteLabel) {
  presentationEventLabel_ = eventLabel;
  presentationContextLabel_ = contextLabel;
  presentationNoteLabel_ = noteLabel;
}

void PresentationPlaybackController::setHostWindow(QWidget* hostWindow) {
  hostWindow_ = hostWindow;
}

void PresentationPlaybackController::setPresentingQuery(std::function<bool()> presenting) {
  presentingQuery_ = std::move(presenting);
}

void PresentationPlaybackController::setTelestrationSession(Telestration::Session* session) {
  if (telestrationSession_ == session) return;
  cancelDrawingFreeze();
  heldDrawingIds_.clear();
  telestrationSession_ = session;
}

void PresentationPlaybackController::wirePresentationQueueSignals() {
  if (!presentationQueue_) return;
  connect(presentationQueue_, &PresentationQueue::queueChanged, this,
          &PresentationPlaybackController::onPresentationQueueChanged);
  connect(presentationQueue_, &PresentationQueue::currentClipChanged, this,
          &PresentationPlaybackController::onPresentationCurrentClipChanged);
  connect(presentationQueue_, &PresentationQueue::clipIntervalChanged, this,
          &PresentationPlaybackController::onPresentationClipIntervalChanged);
}

void PresentationPlaybackController::wirePresentationClipBarSignals() {
  if (!presentationClipBar_) return;
  connect(presentationClipBar_, &ClipTrimBar::seekRequested, this,
          &PresentationPlaybackController::seekVideoFromPresentationClipBar);
  connect(presentationClipBar_, &ClipTrimBar::clipStartChanged, this,
          &PresentationPlaybackController::onPresentationClipBarRangeEdited);
  connect(presentationClipBar_, &ClipTrimBar::clipEndChanged, this,
          &PresentationPlaybackController::onPresentationClipBarRangeEdited);
}

void PresentationPlaybackController::enterPresentationPlayback() {
  updatePresentationStage();
  configurePresentationClipBarForCurrentClip();
  attachPresentationKeyboardShortcuts();
}

void PresentationPlaybackController::leavePresentationPlayback() {
  cancelDrawingFreeze();
  heldDrawingIds_.clear();
  detachPresentationKeyboardShortcuts();
  commitPresentationPlayback(PresentationPlayback::reduce(
      presentationPlayback_, PresentationPlayback::ModeExit{}));
}

void PresentationPlaybackController::detachPresentationKeyboardShortcuts() {
  if (!presentationKeyboardShortcutsAttached_) return;
  if (QApplication* application = qApp) {
    application->removeEventFilter(this);
  }
  presentationKeyboardShortcutsAttached_ = false;
}

void PresentationPlaybackController::resetPresentationForVideoChange() {
  cancelDrawingFreeze();
  heldDrawingIds_.clear();
  // The slot runs now, before Reset: Tagging, detach the Tab filter, then chrome.
  if (isPresenting()) {
    emit leavePresentationRequested();
  }
  commitPresentationPlayback(
      PresentationPlayback::reduce(presentationPlayback_, PresentationPlayback::Reset{}));
}

void PresentationPlaybackController::updatePresentationPlayhead(qint64 positionMs) {
  const bool presenting = isPresenting();
  if (presenting && presentationClipBar_) {
    presentationClipBar_->setPlayheadMs(positionMs);
  }
  if (videoPlayer_) videoPlayer_->setPresentationDrawingsActive(presenting);

  const bool playerIsPlaying = videoPlayer_ && videoPlayer_->isPlaying();
  if (drawingFreezeActive_) {
    if (!playerIsPlaying) {
      drawingFreezeSawPause_ = true;
      if (drawingFreezePausedAtMs_ < 0) drawingFreezePausedAtMs_ = positionMs;
    }
    const qint64 distanceFromHeldFrame =
        drawingFreezePausedAtMs_ < 0
            ? 0
            : (positionMs >= drawingFreezePausedAtMs_ ? positionMs - drawingFreezePausedAtMs_
                                                      : drawingFreezePausedAtMs_ - positionMs);
    const bool leftHeldFrame = drawingFreezePausedAtMs_ >= 0 && distanceFromHeldFrame > 400;
    if (!presenting) {
      cancelDrawingFreeze();
    } else if (playerIsPlaying && drawingFreezeSawPause_) {
      // The presenter pressed play during the hold. Do not freeze the same drawing again.
      heldDrawingIds_.insert(drawingFreezeId_);
      cancelDrawingFreeze();
    } else if (leftHeldFrame) {
      // A seek or scrub moved off the paused frame. Drift before pause lands does not.
      cancelDrawingFreeze();
      noteRewoundPlayhead(positionMs);
    } else {
      commitPresentationPlayback(PresentationPlayback::reduce(
          presentationPlayback_,
          PresentationPlayback::PlayheadTick{positionMs, presenting, playerIsPlaying}));
      return;
    }
  }

  const qint64 previousPositionMs = presentationPlayback_.lastPlayheadMs;
  commitPresentationPlayback(PresentationPlayback::reduce(
      presentationPlayback_,
      PresentationPlayback::PlayheadTick{positionMs, presenting, playerIsPlaying}));

  if (previousPositionMs >= 0 && positionMs + 250 < previousPositionMs) {
    noteRewoundPlayhead(positionMs);
  }

  if (!presenting || !playerIsPlaying || !videoPlayer_ || !telestrationSession_) return;
  const PresentationQueue::Clip* clip =
      presentationQueue_ ? presentationQueue_->currentClip() : nullptr;
  if (clip == nullptr || positionMs >= clip->endMs) return;

  Telestration::ClipWindow clipWindow;
  clipWindow.tagId = clip->tagId;
  clipWindow.startMs = clip->startMs;
  clipWindow.endMs = clip->endMs;
  const std::optional<Telestration::FreezeCue> cue = Telestration::earliestFreezeCue(
      telestrationSession_->drawings(), clipWindow, previousPositionMs, positionMs, true,
      heldDrawingIds_);
  if (!cue.has_value()) return;
  beginDrawingFreeze(*cue, previousPositionMs);
}

void PresentationPlaybackController::showPresentationClip(int queueIndex) {
  if (!presentationQueue_ || !videoPlayer_) return;
  if (!presentationQueue_->setCurrentIndex(queueIndex)) return;

  const PresentationQueue::Clip* clip = presentationQueue_->currentClip();
  if (!clip) return;

  commitPresentationPlayback(PresentationPlayback::reduce(
      presentationPlayback_, PresentationPlayback::OpenClip{clip->startMs, clip->endMs}));
}

void PresentationPlaybackController::onPresentationQueueChanged() {
  if (presentationQueue_ && presentationQueue_->isEmpty()) {
    commitPresentationPlayback(PresentationPlayback::reduce(
        presentationPlayback_, PresentationPlayback::QueueEmptied{}));
  }
  updatePresentationStage();
  // setClipInterval refresh emits queueChanged before clipIntervalChanged; reconfiguring here
  // would reset ClipTrimBar::dragTarget_ and end an in-progress handle drag.
  if (!savingClipIntervalFromBar_) {
    configurePresentationClipBarForCurrentClip();
  }
}

void PresentationPlaybackController::onPresentationCurrentClipChanged(int /*queueIndex*/) {
  cancelDrawingFreeze();
  heldDrawingIds_.clear();
  updatePresentationStage();
  if (!savingClipIntervalFromBar_) {
    configurePresentationClipBarForCurrentClip();
  }
}

void PresentationPlaybackController::onPresentationClipIntervalChanged(int queueIndex) {
  if (!presentationQueue_ || queueIndex != presentationQueue_->currentIndex()) return;
  const PresentationQueue::Clip* clip = presentationQueue_->currentClip();
  if (!clip) return;

  if (presentationPanel_) {
    presentationPanel_->setCurrentClip(clip->tagSessionIndex, clip->leadMs(), clip->lagMs());
  }
  // A drag on the clip bar already moved the handles; reconfiguring mid-drag would fight the user.
  if (!savingClipIntervalFromBar_) {
    configurePresentationClipBarForCurrentClip();
  }

  const qint64 positionMs = videoPlayer_ ? videoPlayer_->currentPositionMs() : 0;
  commitPresentationPlayback(PresentationPlayback::reduce(
      presentationPlayback_,
      PresentationPlayback::ClipIntervalUpdated{positionMs, clip->endMs}));
}

void PresentationPlaybackController::seekVideoFromPresentationClipBar(qint64 positionMs) {
  cancelDrawingFreeze();
  noteRewoundPlayhead(positionMs);
  if (videoPlayer_) videoPlayer_->seekToMs(positionMs);
}

void PresentationPlaybackController::onPresentationClipBarRangeEdited(qint64 /*endpointMs*/) {
  savePresentationClipIntervalFromClipBar();
}

void PresentationPlaybackController::goToNextPresentationClip() {
  stepPresentationQueue(true);
}

void PresentationPlaybackController::goToPreviousPresentationClip() {
  stepPresentationQueue(false);
}

void PresentationPlaybackController::stepPresentationQueue(bool forward) {
  if (!presentationQueue_ || presentationQueue_->isEmpty() || !videoPlayer_) return;

  const PresentationPlayback::QueueSnapshot snapshot = queueSnapshot(*presentationQueue_);
  const PresentationPlayback::Action action =
      forward ? PresentationPlayback::Action{PresentationPlayback::Next{snapshot}}
              : PresentationPlayback::Action{PresentationPlayback::Previous{snapshot}};
  commitPresentationPlayback(PresentationPlayback::reduce(presentationPlayback_, action));
}

void PresentationPlaybackController::commitPresentationPlayback(
    const PresentationPlayback::Result& result) {
  if (result.queueIndex.has_value()) {
    if (!presentationQueue_ || !presentationQueue_->setCurrentIndex(*result.queueIndex)) {
      return;
    }
  }
  presentationPlayback_ = result.state;
  if (!videoPlayer_) return;
  for (const PresentationPlayback::Effect& effect : result.effects) {
    applyPresentationEffect(*videoPlayer_, effect);
  }
}

void PresentationPlaybackController::updatePresentationStage() {
  if (!presentationEventLabel_ || !presentationContextLabel_ || !presentationNoteLabel_) return;

  const PresentationQueue::Clip* clip =
      presentationQueue_ ? presentationQueue_->currentClip() : nullptr;
  if (!clip) {
    showEmptyPresentationStage();
    return;
  }
  showCurrentPresentationClip(*clip);
}

void PresentationPlaybackController::showEmptyPresentationStage() {
  presentationEventLabel_->setText(AppLocale::trUi("presentation.empty_title"));
  presentationContextLabel_->setText(AppLocale::trUi("presentation.empty_hint"));
  presentationNoteLabel_->clear();
  presentationNoteLabel_->hide();
  if (presentationPanel_) presentationPanel_->clearCurrentClip();
}

void PresentationPlaybackController::showCurrentPresentationClip(
    const PresentationQueue::Clip& clip) {
  presentationEventLabel_->setText(
      EventLabelResolver::tagLine(clip.mainEvent, clip.followUps, tagSession_));
  presentationContextLabel_->setText(presentationContextText(clip));

  const bool showNote =
      presentationPanel_ && presentationPanel_->showNotesEnabled() && !clip.note.isEmpty();
  presentationNoteLabel_->setText(clip.note);
  presentationNoteLabel_->setVisible(showNote);

  if (presentationPanel_) {
    presentationPanel_->setCurrentClip(clip.tagSessionIndex, clip.leadMs(), clip.lagMs());
  }
}

QString PresentationPlaybackController::presentationContextText(
    const PresentationQueue::Clip& clip) const {
  TagSession::GameTag tagForDisplay;
  tagForDisplay.team = clip.team;
  const QString periodLabel =
      tagSession_ ? tagSession_->periodLabelAtTimestampMs(clip.markMs) : QString();
  QStringList contextParts;
  contextParts.append(QStringLiteral("%1 / %2")
                          .arg(presentationQueue_->currentIndex() + 1)
                          .arg(presentationQueue_->count()));
  const QString teamName = TagRowDisplay::displayTeamForTag(tagForDisplay, tagSession_);
  if (!teamName.isEmpty() && teamName != QStringLiteral("—")) contextParts.append(teamName);
  if (!periodLabel.isEmpty()) contextParts.append(periodLabel);
  contextParts.append(TagRowDisplay::formatTimestampMs(clip.markMs));
  return contextParts.join(QStringLiteral("  ·  "));
}

void PresentationPlaybackController::configurePresentationClipBarForCurrentClip() {
  if (!presentationClipBar_) return;

  const PresentationQueue::Clip* clip =
      presentationQueue_ ? presentationQueue_->currentClip() : nullptr;
  if (!clip) {
    presentationClipBar_->hide();
    return;
  }
  presentationClipBar_->show();

  const qint64 videoDurationMs = videoPlayer_ ? videoPlayer_->durationMs() : 0;
  const PresentationClipWindow window = presentationClipWindow(*clip, videoDurationMs);
  presentationClipBar_->configure(clip->markMs, clip->startMs, clip->endMs, window.startMs,
                                  window.endMs);
  if (videoPlayer_) {
    presentationClipBar_->setPlayheadMs(videoPlayer_->currentPositionMs());
  }
}

void PresentationPlaybackController::savePresentationClipIntervalFromClipBar() {
  if (!presentationQueue_ || !presentationClipBar_) return;
  const int queueIndex = presentationQueue_->currentIndex();
  if (queueIndex < 0) return;

  const ScopedReentrancyFlag savingClipInterval(savingClipIntervalFromBar_);
  presentationQueue_->setClipInterval(queueIndex, presentationClipBar_->clipStartMs(),
                                      presentationClipBar_->clipEndMs());
}

void PresentationPlaybackController::attachPresentationKeyboardShortcuts() {
  if (presentationKeyboardShortcutsAttached_) return;
  if (QApplication* application = qApp) {
    application->installEventFilter(this);
    presentationKeyboardShortcutsAttached_ = true;
  }
}

bool PresentationPlaybackController::eventFilter(QObject* watched, QEvent* event) {
  if (handlePresentationTabShortcut(watched, event)) return true;
  return QObject::eventFilter(watched, event);
}

bool PresentationPlaybackController::handlePresentationTabShortcut(QObject* watched,
                                                                  QEvent* event) {
  if (!event || !presentationKeyboardShortcutsAttached_ || !isPresenting() ||
      event->type() != QEvent::KeyPress) {
    return false;
  }

  auto* targetWidget = qobject_cast<QWidget*>(watched);
  if (!targetWidget || !hostWindow_ || targetWidget->window() != hostWindow_->window()) {
    return false;
  }

  auto* keyEvent = static_cast<QKeyEvent*>(event);
  const bool isTabKey = keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab;
  if (!isTabKey) return false;

  // Let Tab keep its normal meaning while the user is typing in the panel.
  if (TextInputFocus::widgetIsTextInput(QApplication::focusWidget())) {
    return false;
  }

  const bool goBackwards = keyEvent->key() == Qt::Key_Backtab ||
                           (keyEvent->modifiers() & Qt::ShiftModifier) != 0;
  if (goBackwards) {
    goToPreviousPresentationClip();
  } else {
    goToNextPresentationClip();
  }
  return true;
}

bool PresentationPlaybackController::isPresenting() const {
  return presentingQuery_ && presentingQuery_();
}

void PresentationPlaybackController::cancelDrawingFreeze() {
  if (drawingFreezeTimer_) drawingFreezeTimer_->stop();
  drawingFreezeActive_ = false;
  drawingFreezeSawPause_ = false;
  drawingFreezeId_ = 0;
  drawingFreezeAnchorMs_ = 0;
  drawingFreezeCrossedFromMs_ = -1;
  drawingFreezePausedAtMs_ = -1;
  if (videoPlayer_) videoPlayer_->setForcedVisibleDrawingId(0);
}

void PresentationPlaybackController::beginDrawingFreeze(const Telestration::FreezeCue& cue,
                                                       qint64 crossedFromMs) {
  if (!videoPlayer_) return;
  drawingFreezeActive_ = true;
  drawingFreezeSawPause_ = videoPlayer_ != nullptr && !videoPlayer_->isPlaying();
  drawingFreezeId_ = cue.drawingId;
  drawingFreezeAnchorMs_ = cue.anchorMs;
  drawingFreezeCrossedFromMs_ = crossedFromMs;
  drawingFreezePausedAtMs_ = -1;
  videoPlayer_->setForcedVisibleDrawingId(cue.drawingId);
  videoPlayer_->pauseWithControlFlash();
  if (!videoPlayer_->isPlaying()) {
    drawingFreezeSawPause_ = true;
    drawingFreezePausedAtMs_ = videoPlayer_->currentPositionMs();
  }
  const qint64 boundedHoldMs = std::clamp(cue.holdMs, qint64{1}, qint64{60 * 60 * 1000});
  if (drawingFreezeTimer_) drawingFreezeTimer_->start(static_cast<int>(boundedHoldMs));
}

void PresentationPlaybackController::finishDrawingFreeze() {
  if (!drawingFreezeActive_) return;
  heldDrawingIds_.insert(drawingFreezeId_);
  const qint64 crossedFromMs = drawingFreezeCrossedFromMs_;
  const qint64 positionMs = videoPlayer_ ? videoPlayer_->currentPositionMs() : drawingFreezeAnchorMs_;
  drawingFreezeActive_ = false;
  drawingFreezeId_ = 0;
  if (videoPlayer_) videoPlayer_->setForcedVisibleDrawingId(0);
  if (drawingFreezeTimer_) drawingFreezeTimer_->stop();

  if (!isPresenting() || !videoPlayer_ || !telestrationSession_) return;
  const PresentationQueue::Clip* clip =
      presentationQueue_ ? presentationQueue_->currentClip() : nullptr;
  if (clip == nullptr || positionMs >= clip->endMs) return;

  Telestration::ClipWindow clipWindow;
  clipWindow.tagId = clip->tagId;
  clipWindow.startMs = clip->startMs;
  clipWindow.endMs = clip->endMs;
  const std::optional<Telestration::FreezeCue> nextCue = Telestration::earliestUnheldCueThrough(
      telestrationSession_->drawings(), clipWindow, crossedFromMs, positionMs, heldDrawingIds_);
  if (nextCue.has_value()) {
    beginDrawingFreeze(*nextCue, crossedFromMs);
    return;
  }
  videoPlayer_->playWithControlFlash();
}

void PresentationPlaybackController::noteRewoundPlayhead(qint64 positionMs) {
  if (telestrationSession_ == nullptr) return;
  Telestration::forgetHeldDrawingsAfterRewind(heldDrawingIds_, telestrationSession_->drawings(),
                                             positionMs);
}

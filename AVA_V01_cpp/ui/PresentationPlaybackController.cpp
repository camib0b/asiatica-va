#include "PresentationPlaybackController.h"

#include "../components/VideoPlayer.h"
#include "../export/ClipTrimBar.h"
#include "../i18n/AppLocale.h"
#include "../state/TagSession.h"
#include "PresentationPanel.h"
#include "TagRowDisplay.h"
#include "TextInputFocus.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QStringList>
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
      presentationPlayback_() {}

PresentationPlaybackController::~PresentationPlaybackController() {
  detachPresentationKeyboardShortcuts();
}

void PresentationPlaybackController::setTagSession(TagSession* tagSession) {
  tagSession_ = tagSession;
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

  const bool playerIsPlaying = videoPlayer_ && videoPlayer_->isPlaying();
  commitPresentationPlayback(PresentationPlayback::reduce(
      presentationPlayback_,
      PresentationPlayback::PlayheadTick{positionMs, presenting, playerIsPlaying}));
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
      TagEvent::displayTagLine(clip.mainEvent, clip.followUps));
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

#pragma once

#include "../state/PresentationPlayback.h"
#include "../state/PresentationQueue.h"

#include <QObject>
#include <QPointer>

#include <functional>

class QEvent;
class QLabel;
class QWidget;

class ClipTrimBar;
class PresentationPanel;
class TagSession;
class VideoPlayer;

/// Owns presentation playback: reducer state, queue signal wiring, the clip bar
/// (including the reentrancy guard around interval writes), Tab stepping, and the
/// stage labels for the empty queue and the current clip.
///
/// WorkWindow still builds the banner, clip bar, queue, and panel, and still applies
/// mode chrome. Closing or loading a video while presenting emits
/// leavePresentationRequested so WorkWindow can assign Tagging and refresh chrome.
/// That path must not call setMode: setMode would flush notes and commit ModeExit
/// before Reset, and it would not install the Tab filter again for the next video.
class PresentationPlaybackController final : public QObject {
  Q_OBJECT

public:
  explicit PresentationPlaybackController(QObject* parent = nullptr);
  ~PresentationPlaybackController() override;

  void setTagSession(TagSession* tagSession);
  void setVideoPlayer(VideoPlayer* videoPlayer);
  void setPresentationQueue(PresentationQueue* queue);
  void setPresentationClipBar(ClipTrimBar* clipBar);
  void setPresentationPanel(PresentationPanel* panel);
  void setStageLabels(QLabel* eventLabel, QLabel* contextLabel, QLabel* noteLabel);
  void setHostWindow(QWidget* hostWindow);
  /// Live read of WorkWindow::mode(). Cached state would drift if mode is assigned directly.
  void setPresentingQuery(std::function<bool()> presenting);

  void wirePresentationQueueSignals();
  void wirePresentationClipBarSignals();

  void enterPresentationPlayback();
  void leavePresentationPlayback();
  void detachPresentationKeyboardShortcuts();
  void resetPresentationForVideoChange();
  void updatePresentationStage();
  void updatePresentationPlayhead(qint64 positionMs);
  void showPresentationClip(int queueIndex);
  /// Apply-to-all still calls this after the queue signal has already refreshed the bar.
  void configurePresentationClipBarForCurrentClip();

signals:
  /// Video close or load while Presenting, before playback Reset. Direct connection.
  /// The slot assigns Tagging, detaches the Tab filter, applies chrome, and syncs the mode toggle.
  void leavePresentationRequested();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void onPresentationQueueChanged();
  void onPresentationCurrentClipChanged(int queueIndex);
  void onPresentationClipIntervalChanged(int queueIndex);
  void seekVideoFromPresentationClipBar(qint64 positionMs);
  void onPresentationClipBarRangeEdited(qint64 endpointMs);
  void goToNextPresentationClip();
  void goToPreviousPresentationClip();
  void stepPresentationQueue(bool forward);
  void commitPresentationPlayback(const PresentationPlayback::Result& result);
  void savePresentationClipIntervalFromClipBar();
  void showEmptyPresentationStage();
  void showCurrentPresentationClip(const PresentationQueue::Clip& clip);
  QString presentationContextText(const PresentationQueue::Clip& clip) const;
  void attachPresentationKeyboardShortcuts();
  bool handlePresentationTabShortcut(QObject* watched, QEvent* event);
  bool isPresenting() const;

  TagSession* tagSession_ = nullptr;
  QMetaObject::Connection eventLabelOverridesConnection_;
  QPointer<VideoPlayer> videoPlayer_;
  QPointer<PresentationQueue> presentationQueue_;
  QPointer<ClipTrimBar> presentationClipBar_;
  QPointer<PresentationPanel> presentationPanel_;
  QPointer<QLabel> presentationEventLabel_;
  QPointer<QLabel> presentationContextLabel_;
  QPointer<QLabel> presentationNoteLabel_;
  QPointer<QWidget> hostWindow_;
  std::function<bool()> presentingQuery_;
  bool presentationKeyboardShortcutsAttached_ = false;
  bool savingClipIntervalFromBar_ = false;
  PresentationPlayback::State presentationPlayback_{};
};

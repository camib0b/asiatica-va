#include "PresentationQueue.h"

#include "TagSession.h"

#include <algorithm>

namespace {
/// A clip must stay long enough to be watchable even if the user drags lead and lag to zero.
constexpr qint64 kMinimumClipDurationMs = 500;
} // namespace

PresentationQueue::PresentationQueue(QObject* parent) : QObject(parent) {}

void PresentationQueue::setTagSession(TagSession* session) {
  if (tagSession_ == session) return;
  if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);

  tagSession_ = session;
  selectedTagIndexes_.clear();
  clips_.clear();
  currentIndex_ = -1;

  if (tagSession_) {
    connect(tagSession_, &TagSession::cleared, this, &PresentationQueue::clear);
    connect(tagSession_, &TagSession::tagsImported, this, &PresentationQueue::clear);
    connect(tagSession_, &TagSession::statsChanged, this,
            &PresentationQueue::onSessionTagsChanged);
    connect(tagSession_, &TagSession::tagNoteChanged, this,
            [this](int) { onSessionTagsChanged(); });
    connect(tagSession_, &TagSession::tagIntervalChanged, this, [this](int) {
      if (writingIntervalToSession_) return;  // our own write; already reflected locally
      onSessionTagsChanged();
    });
  }

  emit queueChanged();
  emit currentClipChanged(currentIndex_);
}

void PresentationQueue::setVideoDurationMs(qint64 videoDurationMs) {
  if (videoDurationMs_ == videoDurationMs) return;
  videoDurationMs_ = videoDurationMs > 0 ? videoDurationMs : 0;
  rebuildClipsFromSession();
  emit queueChanged();
}

void PresentationQueue::setSelectedTagIndexes(const QVector<int>& tagSessionIndexes) {
  const int previousTagIndex =
      (currentIndex_ >= 0 && currentIndex_ < clips_.size()) ? clips_.at(currentIndex_).tagSessionIndex : -1;

  selectedTagIndexes_ = tagSessionIndexes;
  dropSelectedIndexesOutsideSession();
  rebuildClipsFromSession();

  const int restoredIndex = queueIndexForTagIndex(previousTagIndex);
  const int previousCurrentIndex = currentIndex_;
  const int lastIndex = static_cast<int>(clips_.size()) - 1;
  if (restoredIndex >= 0) {
    currentIndex_ = restoredIndex;
  } else if (clips_.isEmpty()) {
    currentIndex_ = -1;
  } else {
    currentIndex_ = std::clamp(currentIndex_ < 0 ? 0 : currentIndex_, 0, lastIndex);
  }

  emit queueChanged();
  if (currentIndex_ != previousCurrentIndex || restoredIndex < 0) {
    emit currentClipChanged(currentIndex_);
  }
}

void PresentationQueue::clear() {
  const bool hadContent = !clips_.isEmpty() || !selectedTagIndexes_.isEmpty();
  selectedTagIndexes_.clear();
  clips_.clear();
  currentIndex_ = -1;
  if (!hadContent) return;
  emit queueChanged();
  emit currentClipChanged(currentIndex_);
}

const PresentationQueue::Clip* PresentationQueue::currentClip() const {
  if (currentIndex_ < 0 || currentIndex_ >= clips_.size()) return nullptr;
  return &clips_.at(currentIndex_);
}

bool PresentationQueue::setCurrentIndex(int index) {
  if (index < 0 || index >= clips_.size()) return false;
  if (index == currentIndex_) {
    emit currentClipChanged(currentIndex_);  // re-arm playback for the same clip
    return true;
  }
  currentIndex_ = index;
  emit currentClipChanged(currentIndex_);
  return true;
}

bool PresentationQueue::moveToNextClip() {
  if (!hasNextClip()) return false;
  return setCurrentIndex(currentIndex_ + 1);
}

bool PresentationQueue::moveToPreviousClip() {
  if (!hasPreviousClip()) return false;
  return setCurrentIndex(currentIndex_ - 1);
}

bool PresentationQueue::setCurrentTagIndex(int tagSessionIndex) {
  const int queueIndex = queueIndexForTagIndex(tagSessionIndex);
  if (queueIndex < 0) return false;
  return setCurrentIndex(queueIndex);
}

void PresentationQueue::setClipInterval(int index, qint64 startMs, qint64 endMs) {
  if (index < 0 || index >= clips_.size()) return;

  Clip& clip = clips_[index];
  if (startMs < 0) startMs = 0;
  if (endMs < startMs + kMinimumClipDurationMs) endMs = startMs + kMinimumClipDurationMs;
  clip.startMs = startMs;
  clip.endMs = endMs;
  clampClipToVideo(clip);

  if (tagSession_ && clip.tagSessionIndex >= 0 && clip.tagSessionIndex < tagSession_->tags().size()) {
    writingIntervalToSession_ = true;
    tagSession_->setTagInterval(clip.tagSessionIndex, clip.startMs, clip.endMs,
                                /*userInitiated=*/true);
    writingIntervalToSession_ = false;
  }

  emit clipIntervalChanged(index);
}

void PresentationQueue::applyLeadLagToAllClips(qint64 leadMs, qint64 lagMs) {
  if (leadMs < 0) leadMs = 0;
  if (lagMs < 0) lagMs = 0;
  for (int index = 0; index < clips_.size(); ++index) {
    const qint64 markMs = clips_.at(index).markMs;
    setClipInterval(index, markMs - leadMs, markMs + lagMs);
  }
}

void PresentationQueue::onSessionTagsChanged() {
  dropSelectedIndexesOutsideSession();
  const int previousTagIndex =
      (currentIndex_ >= 0 && currentIndex_ < clips_.size()) ? clips_.at(currentIndex_).tagSessionIndex : -1;

  rebuildClipsFromSession();

  const int restoredIndex = queueIndexForTagIndex(previousTagIndex);
  const int previousCurrentIndex = currentIndex_;
  const int lastIndex = static_cast<int>(clips_.size()) - 1;
  currentIndex_ = clips_.isEmpty()
      ? -1
      : (restoredIndex >= 0 ? restoredIndex : std::clamp(currentIndex_, 0, lastIndex));

  emit queueChanged();
  if (currentIndex_ != previousCurrentIndex) {
    emit currentClipChanged(currentIndex_);
  }
}

void PresentationQueue::rebuildClipsFromSession() {
  clips_.clear();
  if (!tagSession_) return;

  const auto& tags = tagSession_->tags();
  clips_.reserve(selectedTagIndexes_.size());
  for (const int tagSessionIndex : selectedTagIndexes_) {
    if (tagSessionIndex < 0 || tagSessionIndex >= tags.size()) continue;
    const TagSession::GameTag& tag = tags.at(tagSessionIndex);

    Clip clip;
    clip.tagSessionIndex = tagSessionIndex;
    clip.markMs = tag.positionMs;
    clip.startMs = tag.startMs;
    clip.endMs = tag.endMs;
    clip.mainEvent = tag.mainEvent;
    clip.followUpEvent = tag.followUpEvent;
    clip.team = tag.team;
    clip.note = tag.note.trimmed();
    clampClipToVideo(clip);
    clips_.append(clip);
  }

  std::sort(clips_.begin(), clips_.end(), [](const Clip& first, const Clip& second) {
    if (first.markMs != second.markMs) return first.markMs < second.markMs;
    return first.tagSessionIndex < second.tagSessionIndex;
  });
}

void PresentationQueue::dropSelectedIndexesOutsideSession() {
  const int tagCount = tagSession_ ? tagSession_->tags().size() : 0;
  QVector<int> validIndexes;
  validIndexes.reserve(selectedTagIndexes_.size());
  for (const int tagSessionIndex : selectedTagIndexes_) {
    if (tagSessionIndex < 0 || tagSessionIndex >= tagCount) continue;
    if (validIndexes.contains(tagSessionIndex)) continue;
    validIndexes.append(tagSessionIndex);
  }
  selectedTagIndexes_ = validIndexes;
}

void PresentationQueue::clampClipToVideo(Clip& clip) const {
  if (clip.startMs < 0) clip.startMs = 0;
  if (videoDurationMs_ > 0 && clip.endMs > videoDurationMs_) clip.endMs = videoDurationMs_;
  if (clip.endMs < clip.startMs + kMinimumClipDurationMs) {
    clip.endMs = clip.startMs + kMinimumClipDurationMs;
  }
}

int PresentationQueue::queueIndexForTagIndex(int tagSessionIndex) const {
  if (tagSessionIndex < 0) return -1;
  for (int index = 0; index < clips_.size(); ++index) {
    if (clips_.at(index).tagSessionIndex == tagSessionIndex) return index;
  }
  return -1;
}

#include "PresentationQueue.h"

#include "TagSession.h"

#include <algorithm>

#include <QSet>

namespace {
/// A clip must stay long enough to be watchable even if the user drags lead and lag to zero.
constexpr qint64 kMinimumClipDurationMs = 500;

class SessionWriteGuard {
public:
  explicit SessionWriteGuard(bool& writingFlag) : writingFlag_(writingFlag) { writingFlag_ = true; }
  ~SessionWriteGuard() { writingFlag_ = false; }

  SessionWriteGuard(const SessionWriteGuard&) = delete;
  SessionWriteGuard& operator=(const SessionWriteGuard&) = delete;

private:
  bool& writingFlag_;
};
} // namespace

PresentationQueue::PresentationQueue(QObject* parent) : QObject(parent) {}

void PresentationQueue::setTagSession(TagSession* session) {
  if (tagSession_ == session) return;
  if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);

  tagSession_ = session;
  selectedTagIds_.clear();
  clips_.clear();
  currentIndex_ = -1;

  if (tagSession_) {
    connect(tagSession_, &TagSession::cleared, this, &PresentationQueue::clear);
    connect(tagSession_, &TagSession::tagsImported, this, &PresentationQueue::clear);
    connect(tagSession_, &TagSession::tagsChanged, this,
            &PresentationQueue::refreshQueueFromSession);
    connect(tagSession_, &TagSession::tagNoteChanged, this,
            [this](int) { refreshQueueFromSession(); });
    connect(tagSession_, &TagSession::tagIntervalChanged, this, [this](int) {
      if (writingIntervalToSession_) return;
      refreshQueueFromSession();
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
  const quint64 previousTagId = currentTagId();

  QVector<quint64> selectedTagIds;
  selectedTagIds.reserve(tagSessionIndexes.size());
  QSet<quint64> seenTagIds;
  if (tagSession_) {
    const auto& tags = tagSession_->tags();
    for (const int tagSessionIndex : tagSessionIndexes) {
      if (tagSessionIndex < 0 || tagSessionIndex >= tags.size()) continue;
      const quint64 tagId = tags.at(tagSessionIndex).id;
      if (tagId == 0 || seenTagIds.contains(tagId)) continue;
      seenTagIds.insert(tagId);
      selectedTagIds.append(tagId);
    }
  }
  selectedTagIds_ = selectedTagIds;
  rebuildClipsFromSession();
  restoreCurrentIndex(previousTagId);
  emit queueChanged();
}

void PresentationQueue::clear() {
  const bool hadContent = !clips_.isEmpty() || !selectedTagIds_.isEmpty();
  selectedTagIds_.clear();
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

bool PresentationQueue::setCurrentTagIndex(int tagSessionIndex) {
  const int queueIndex = queueIndexForTagIndex(tagSessionIndex);
  if (queueIndex < 0) return false;
  return setCurrentIndex(queueIndex);
}

void PresentationQueue::setClipInterval(int index, qint64 startMs, qint64 endMs) {
  if (index < 0 || index >= clips_.size()) return;

  const quint64 tagId = clips_[index].tagId;
  const int tagSessionIndex = clips_[index].tagSessionIndex;

  Clip intervalClip = clips_[index];
  if (startMs < 0) startMs = 0;
  if (endMs < startMs + kMinimumClipDurationMs) endMs = startMs + kMinimumClipDurationMs;
  intervalClip.startMs = startMs;
  intervalClip.endMs = endMs;
  clampClipToVideo(intervalClip);

  if (tagSession_ && tagSessionIndex >= 0 && tagSessionIndex < tagSession_->tags().size()) {
    SessionWriteGuard guard(writingIntervalToSession_);
    tagSession_->setTagInterval(tagSessionIndex, intervalClip.startMs, intervalClip.endMs);
    refreshQueueFromSession();
    const int refreshedIndex = queueIndexForTagId(tagId);
    if (refreshedIndex >= 0) emit clipIntervalChanged(refreshedIndex);
    return;
  }

  clips_[index] = intervalClip;
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

void PresentationQueue::refreshQueueFromSession() {
  pruneSelectedTagIds();
  const quint64 previousTagId = currentTagId();
  rebuildClipsFromSession();
  restoreCurrentIndex(previousTagId);
  emit queueChanged();
}

void PresentationQueue::rebuildClipsFromSession() {
  clips_.clear();
  if (!tagSession_) return;

  const auto& tags = tagSession_->tags();
  clips_.reserve(selectedTagIds_.size());
  for (const quint64 tagId : selectedTagIds_) {
    if (tagId == 0) continue;
    const int tagSessionIndex = tagSession_->indexOfTagId(tagId);
    if (tagSessionIndex < 0 || tagSessionIndex >= tags.size()) continue;
    const TagSession::GameTag& tag = tags.at(tagSessionIndex);

    Clip clip;
    clip.tagSessionIndex = tagSessionIndex;
    clip.tagId = tag.id;
    clip.markMs = tag.markMs;
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

void PresentationQueue::pruneSelectedTagIds() {
  if (!tagSession_) {
    selectedTagIds_.clear();
    return;
  }

  QVector<quint64> validTagIds;
  validTagIds.reserve(selectedTagIds_.size());
  QSet<quint64> seenTagIds;
  for (const quint64 tagId : selectedTagIds_) {
    if (tagId == 0) continue;
    if (tagSession_->indexOfTagId(tagId) < 0) continue;
    if (seenTagIds.contains(tagId)) continue;
    seenTagIds.insert(tagId);
    validTagIds.append(tagId);
  }
  selectedTagIds_ = validTagIds;
}

void PresentationQueue::clampClipToVideo(Clip& clip) const {
  if (clip.startMs < 0) clip.startMs = 0;

  if (videoDurationMs_ > 0) {
    if (videoDurationMs_ <= kMinimumClipDurationMs) {
      clip.startMs = std::clamp(clip.startMs, 0LL, videoDurationMs_);
      clip.endMs = videoDurationMs_;
      return;
    }
    if (clip.startMs > videoDurationMs_) clip.startMs = videoDurationMs_;
  }

  if (clip.endMs < clip.startMs + kMinimumClipDurationMs) {
    clip.endMs = clip.startMs + kMinimumClipDurationMs;
  }

  if (videoDurationMs_ > 0 && clip.endMs > videoDurationMs_) {
    clip.endMs = videoDurationMs_;
    const qint64 minimumStartMs = clip.endMs - kMinimumClipDurationMs;
    if (clip.startMs > minimumStartMs) {
      clip.startMs = std::max(0LL, minimumStartMs);
    }
  }

  if (clip.endMs < clip.startMs) clip.endMs = clip.startMs;
}

quint64 PresentationQueue::currentTagId() const {
  if (currentIndex_ < 0 || currentIndex_ >= clips_.size()) return 0;
  return clips_.at(currentIndex_).tagId;
}

void PresentationQueue::restoreCurrentIndex(quint64 previousTagId) {
  const int previousCurrentIndex = currentIndex_;
  const int restoredIndex = queueIndexForTagId(previousTagId);
  const int lastIndex = static_cast<int>(clips_.size()) - 1;

  if (restoredIndex >= 0) {
    currentIndex_ = restoredIndex;
  } else if (clips_.isEmpty()) {
    currentIndex_ = -1;
  } else {
    currentIndex_ = std::clamp(currentIndex_ < 0 ? 0 : currentIndex_, 0, lastIndex);
  }

  const quint64 currentTagIdValue =
      (currentIndex_ >= 0 && currentIndex_ < clips_.size()) ? clips_.at(currentIndex_).tagId : 0;
  if (currentIndex_ != previousCurrentIndex || restoredIndex < 0 ||
      currentTagIdValue != previousTagId) {
    emit currentClipChanged(currentIndex_);
  }
}

int PresentationQueue::queueIndexForTagIndex(int tagSessionIndex) const {
  if (tagSessionIndex < 0) return -1;
  for (int index = 0; index < clips_.size(); ++index) {
    if (clips_.at(index).tagSessionIndex == tagSessionIndex) return index;
  }
  return -1;
}

int PresentationQueue::queueIndexForTagId(quint64 tagId) const {
  if (tagId == 0) return -1;
  for (int index = 0; index < clips_.size(); ++index) {
    if (clips_.at(index).tagId == tagId) return index;
  }
  return -1;
}

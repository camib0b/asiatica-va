#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

class TagSession;

/// Ordered set of tagged instances the user picked to show in presentation mode, plus the position
/// of the clip currently on screen.
///
/// Clip intervals are read from and written back to the TagSession, so a lead/lag adjustment made
/// while presenting is exactly the interval the clip exporter will later use. The queue stores
/// TagSession indexes (not copies) as its source of truth and rebuilds its cached clip data
/// whenever the session changes.
class PresentationQueue final : public QObject {
  Q_OBJECT

public:
  struct Clip {
    int tagSessionIndex = -1;
    qint64 markMs = 0;   ///< Event mark (GameTag::positionMs).
    qint64 startMs = 0;  ///< Mark minus lead time.
    qint64 endMs = 0;    ///< Mark plus lag time.
    QString mainEvent;
    QString followUpEvent;
    QString team;
    QString note;

    qint64 leadMs() const { return markMs > startMs ? markMs - startMs : 0; }
    qint64 lagMs() const { return endMs > markMs ? endMs - markMs : 0; }
  };

  explicit PresentationQueue(QObject* parent = nullptr);

  void setTagSession(TagSession* session);
  /// Used to clamp clip intervals; 0 means "unknown duration, do not clamp the end".
  void setVideoDurationMs(qint64 videoDurationMs);

  /// Replaces the queue with the given TagSession indexes, ordered by event mark. The clip that is
  /// currently on screen stays selected when it is still part of the new selection.
  void setSelectedTagIndexes(const QVector<int>& tagSessionIndexes);
  QVector<int> selectedTagIndexes() const { return selectedTagIndexes_; }
  void clear();

  bool isEmpty() const { return clips_.isEmpty(); }
  int count() const { return static_cast<int>(clips_.size()); }
  const Clip& clipAt(int index) const { return clips_.at(index); }
  int currentIndex() const { return currentIndex_; }
  /// Clip currently on screen, or nullptr when the queue is empty.
  const Clip* currentClip() const;
  bool hasNextClip() const { return currentIndex_ >= 0 && currentIndex_ < clips_.size() - 1; }
  bool hasPreviousClip() const { return currentIndex_ > 0; }

  bool setCurrentIndex(int index);
  bool moveToNextClip();
  bool moveToPreviousClip();
  /// Selects the queued clip belonging to \p tagSessionIndex; false when it is not queued.
  bool setCurrentTagIndex(int tagSessionIndex);

  /// Writes a new interval for the clip at \p index through to the TagSession, marking the tag as
  /// manually trimmed so later clip-duration default changes cannot overwrite it.
  void setClipInterval(int index, qint64 startMs, qint64 endMs);
  /// Re-pads every queued clip with the same lead/lag around its own event mark.
  void applyLeadLagToAllClips(qint64 leadMs, qint64 lagMs);

signals:
  /// The queued clips changed (selection, ordering, or underlying tag data).
  void queueChanged();
  /// The clip on screen changed; \p index is -1 when the queue became empty.
  void currentClipChanged(int index);
  /// The interval of the clip at \p index changed.
  void clipIntervalChanged(int index);

private:
  void rebuildClipsFromSession();
  void dropSelectedIndexesOutsideSession();
  void clampClipToVideo(Clip& clip) const;
  int queueIndexForTagIndex(int tagSessionIndex) const;
  void onSessionTagsChanged();

  TagSession* tagSession_ = nullptr;
  QVector<Clip> clips_;
  QVector<int> selectedTagIndexes_;
  int currentIndex_ = -1;
  qint64 videoDurationMs_ = 0;
  /// Guards against reacting to the TagSession signals this queue itself triggered.
  bool writingIntervalToSession_ = false;
};

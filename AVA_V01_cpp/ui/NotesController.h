#pragma once

#include "../state/PendingClipNote.h"
#include "../state/TagSession.h"

#include <QObject>
#include <QPointer>
#include <QVector>

#include <optional>

class QEvent;
class QLabel;
class QPlainTextEdit;
class QTableView;
class QTimer;
class QWidget;

class MatchNotesEditor;
class TagsTableModel;
class VideoPlayer;

/// Owns the clip-note and match-note editors: wiring, debounce timers, the
/// unsaved clip draft, and @-mention candidates.
///
/// Callers use flush, discard, load, and suspend/resume. The tags-table rebuild
/// still wraps load in its own signal blockers; those stay on TagsTableController.
/// aboutToClear and aboutToReplaceTags are direct connections so a session
/// clear cannot drop or mis-apply the open draft.
class NotesController final : public QObject {
  Q_OBJECT

public:
  explicit NotesController(QObject* parent = nullptr);
  ~NotesController() override;

  void setTagSession(TagSession* tagSession);
  void setTagsTable(QTableView* tagsTable, TagsTableModel* tagsModel);
  void setVideoPlayer(VideoPlayer* videoPlayer);
  void setNotesColumnWidgets(QLabel* matchNotesLabel,
                        MatchNotesEditor* matchNotesEditor,
                        QLabel* clipNotesLabel,
                        QPlainTextEdit* clipNotesEditor);
  /// Text-changed, mention activation, and clip-editor focus-out. Timers already exist.
  void wireEditorSignals();

  /// Direct connections for aboutToClear and aboutToReplaceTags.
  void connectTagSessionSignals();
  void disconnectTagSessionSignals();

  void applyUiStrings() const;

  quint64 selectedTagId() const;
  /// indexOfTagId, then the current table row when the id is already gone.
  int sessionIndexForTagId(quint64 tagId) const;
  bool isClipNotesEditor(const QWidget* widget) const;

  void flushPendingClipNote();
  void loadClipNoteForSelectedTag();
  void loadMatchNote();
  void refreshMatchNoteMentionCandidates();
  void suspendNoteDebounceTimers();
  void resumeNoteDebounceTimers();
  void saveMatchNoteDebounceFired();
  /// Drops the draft when it belongs to \p tagId. Any other open clip draft is written first.
  void discardPendingClipNoteForRemovedTag(quint64 tagId);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void onClipNoteTextChanged();
  void onMatchNoteTextChanged();
  void onMatchNoteTagMentionActivated(quint64 tagId);
  void saveClipNoteDebounceFired();
  void discardPendingClipNote();
  void onTagSessionTagNoteChanged(int changedIndex);
  void onTagSessionMatchNoteChanged();
  void showPendingClipNoteInEditor();
  void showDisabledClipNoteEditor();
  void showStoredClipNoteInEditor(quint64 selectedTagId);

  TagSession* tagSession_ = nullptr;
  QPointer<QTableView> tagsTable_;
  QPointer<TagsTableModel> tagsModel_;
  QPointer<QLabel> matchNotesLabel_;
  QPointer<MatchNotesEditor> matchNotesEditor_;
  QPointer<QLabel> clipNotesLabel_;
  QPointer<QPlainTextEdit> clipNotesEditor_;
  QPointer<VideoPlayer> videoPlayer_;
  QTimer* clipNoteDebounceTimer_ = nullptr;
  QTimer* matchNoteDebounceTimer_ = nullptr;
  bool clipNoteDebounceShouldResume_ = false;
  bool matchNoteDebounceShouldResume_ = false;
  std::optional<PendingClipNote> pendingClipNote_;
  QVector<QMetaObject::Connection> tagSessionConnections_;
};

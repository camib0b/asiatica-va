#include "NotesController.h"

#include "../components/MatchNotesEditor.h"
#include "../components/VideoPlayer.h"
#include "../i18n/AppLocale.h"
#include "../state/TagSession.h"
#include "TagRowDisplay.h"
#include "TagsTableModel.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTableView>
#include <QTimer>

NotesController::NotesController(QObject* parent)
    : QObject(parent),
      tagSession_(nullptr),
      tagsTable_(nullptr),
      tagsModel_(nullptr),
      matchNotesLabel_(nullptr),
      matchNotesEditor_(nullptr),
      clipNotesLabel_(nullptr),
      clipNotesEditor_(nullptr),
      videoPlayer_(nullptr),
      clipNoteDebounceTimer_(nullptr),
      matchNoteDebounceTimer_(nullptr),
      clipNoteDebounceShouldResume_(false),
      matchNoteDebounceShouldResume_(false) {
  clipNoteDebounceTimer_ = new QTimer(this);
  clipNoteDebounceTimer_->setSingleShot(true);
  connect(clipNoteDebounceTimer_, &QTimer::timeout, this, &NotesController::saveClipNoteDebounceFired);

  matchNoteDebounceTimer_ = new QTimer(this);
  matchNoteDebounceTimer_->setSingleShot(true);
  connect(matchNoteDebounceTimer_, &QTimer::timeout, this,
          &NotesController::saveMatchNoteDebounceFired);
}

NotesController::~NotesController() = default;

void NotesController::setTagSession(TagSession* tagSession) {
  tagSession_ = tagSession;
}

void NotesController::setTagsTable(QTableView* tagsTable, TagsTableModel* tagsModel) {
  tagsTable_ = tagsTable;
  tagsModel_ = tagsModel;
}

void NotesController::setVideoPlayer(VideoPlayer* videoPlayer) {
  videoPlayer_ = videoPlayer;
}

void NotesController::setNotesColumnWidgets(QLabel* matchNotesLabel,
                                       MatchNotesEditor* matchNotesEditor,
                                       QLabel* clipNotesLabel,
                                       QPlainTextEdit* clipNotesEditor) {
  matchNotesLabel_ = matchNotesLabel;
  matchNotesEditor_ = matchNotesEditor;
  clipNotesLabel_ = clipNotesLabel;
  clipNotesEditor_ = clipNotesEditor;
}

void NotesController::wireEditorSignals() {
  if (clipNotesEditor_) {
    clipNotesEditor_->installEventFilter(this);
    connect(clipNotesEditor_, &QPlainTextEdit::textChanged, this, &NotesController::onClipNoteTextChanged);
  }
  if (matchNotesEditor_) {
    connect(matchNotesEditor_, &MatchNotesEditor::textChanged, this,
            &NotesController::onMatchNoteTextChanged);
    connect(matchNotesEditor_, &MatchNotesEditor::tagMentionActivated, this,
            &NotesController::onMatchNoteTagMentionActivated);
  }
}

void NotesController::connectTagSessionSignals() {
  if (!tagSession_) return;
  tagSessionConnections_.append(connect(
      tagSession_, &TagSession::aboutToClear, this, &NotesController::flushPendingClipNote,
      Qt::DirectConnection));
  tagSessionConnections_.append(connect(
      tagSession_, &TagSession::aboutToReplaceTags, this, &NotesController::flushPendingClipNote,
      Qt::DirectConnection));
  tagSessionConnections_.append(connect(
      tagSession_, &TagSession::tagNoteChanged, this, &NotesController::onTagSessionTagNoteChanged));
  tagSessionConnections_.append(connect(
      tagSession_, &TagSession::matchNoteChanged, this,
      &NotesController::onTagSessionMatchNoteChanged));
}

void NotesController::disconnectTagSessionSignals() {
  for (const QMetaObject::Connection& connection : tagSessionConnections_) {
    QObject::disconnect(connection);
  }
  tagSessionConnections_.clear();
}

void NotesController::applyUiStrings() const {
  if (clipNotesEditor_) clipNotesEditor_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
  if (matchNotesLabel_) matchNotesLabel_->setText(AppLocale::trUi("notes.match_title"));
  if (clipNotesLabel_) clipNotesLabel_->setText(AppLocale::trUi("notes.clip_title"));
  if (matchNotesEditor_) matchNotesEditor_->applyUiStrings();
}

quint64 NotesController::selectedTagId() const {
  if (!tagsTable_ || !tagsModel_ || !tagsTable_->selectionModel()) return 0;
  const QModelIndex currentIndex = tagsTable_->selectionModel()->currentIndex();
  if (!currentIndex.isValid()) return 0;
  return tagsModel_->tagIdAt(currentIndex.row());
}

int NotesController::sessionIndexForTagId(quint64 tagId) const {
  if (!tagSession_) return -1;
  const int sessionIndex = tagSession_->indexOfTagId(tagId);
  if (sessionIndex >= 0) return sessionIndex;
  if (!tagsModel_ || !tagsTable_ || !tagsTable_->selectionModel()) return sessionIndex;
  const QModelIndex currentIndex = tagsTable_->selectionModel()->currentIndex();
  if (!currentIndex.isValid()) return sessionIndex;
  return tagsModel_->tagSessionIndexAt(currentIndex.row());
}

bool NotesController::isClipNotesEditor(const QWidget* widget) const {
  return clipNotesEditor_ && widget == clipNotesEditor_.data();
}

void NotesController::flushPendingClipNote() {
  if (clipNoteDebounceTimer_) clipNoteDebounceTimer_->stop();
  clipNoteDebounceShouldResume_ = false;
  if (!pendingClipNote_) return;

  const PendingClipNote note = *pendingClipNote_;
  pendingClipNote_.reset();
  if (!tagSession_) return;

  const int sessionIndex = tagSession_->indexOfTagId(note.tagId);
  const bool tagStillPresent = tagSession_->isValidTagIndex(sessionIndex);
  if (!pendingClipNoteApplies(note, tagSession_->tagIdEpoch(), tagStillPresent)) return;
  tagSession_->setTagNote(sessionIndex, note.text);
}

void NotesController::loadClipNoteForSelectedTag() {
  if (!clipNotesEditor_) return;

  const QSignalBlocker notesBlocker(clipNotesEditor_);
  const quint64 selectedId = selectedTagId();
  if (pendingClipNote_ && pendingClipNote_->tagId == selectedId) {
    showPendingClipNoteInEditor();
    return;
  }

  if (selectedId == 0 || !tagSession_) {
    showDisabledClipNoteEditor();
    return;
  }

  showStoredClipNoteInEditor(selectedId);
}

void NotesController::loadMatchNote() {
  if (!matchNotesEditor_) return;
  const QString html = tagSession_ ? tagSession_->matchNote() : QString();
  if (matchNotesEditor_->isDocumentEquivalentTo(html)) return;
  matchNotesEditor_->setSerializedHtml(html);
}

void NotesController::refreshMatchNoteMentionCandidates() {
  if (!matchNotesEditor_) return;

  QVector<MatchNotesEditor::MentionCandidate> candidates;
  if (tagSession_) {
    for (const TagSession::GameTag& tag : tagSession_->tags()) {
      if (tag.id == 0) continue;
      if (tag.mainEventKey().isTimeControl()) continue;
      const QString eventLine = TagRowDisplay::eventLineForTag(tag, tagSession_);
      const QString teamName = TagRowDisplay::displayTeamForTag(tag, tagSession_);
      MatchNotesEditor::MentionCandidate candidate;
      candidate.tagId = tag.id;
      candidate.label = QStringLiteral("%1  %2  ·  %3")
                            .arg(TagRowDisplay::formatTimestampMs(tag.markMs), eventLine, teamName);
      candidate.searchText = candidate.label;
      candidates.append(candidate);
    }
  }
  matchNotesEditor_->setMentionCandidates(candidates);
}

void NotesController::suspendNoteDebounceTimers() {
  clipNoteDebounceShouldResume_ = clipNoteDebounceTimer_ && clipNoteDebounceTimer_->isActive();
  matchNoteDebounceShouldResume_ = matchNoteDebounceTimer_ && matchNoteDebounceTimer_->isActive();
  if (clipNoteDebounceTimer_) clipNoteDebounceTimer_->stop();
  if (matchNoteDebounceTimer_) matchNoteDebounceTimer_->stop();
}

void NotesController::resumeNoteDebounceTimers() {
  if (clipNoteDebounceShouldResume_ && pendingClipNote_ && clipNoteDebounceTimer_) {
    clipNoteDebounceTimer_->start(400);
  }
  if (matchNoteDebounceShouldResume_ && matchNoteDebounceTimer_) {
    matchNoteDebounceTimer_->start(400);
  }
  clipNoteDebounceShouldResume_ = false;
  matchNoteDebounceShouldResume_ = false;
}

void NotesController::saveMatchNoteDebounceFired() {
  if (!tagSession_ || !matchNotesEditor_) return;
  tagSession_->setMatchNote(matchNotesEditor_->serializedHtml());
}

void NotesController::discardPendingClipNoteForRemovedTag(quint64 tagId) {
  if (pendingClipNote_ && pendingClipNote_->tagId == tagId) {
    discardPendingClipNote();
  } else {
    flushPendingClipNote();
  }
}

bool NotesController::eventFilter(QObject* watched, QEvent* event) {
  if (watched == clipNotesEditor_.data() && event && event->type() == QEvent::FocusOut) {
    flushPendingClipNote();
  }
  return QObject::eventFilter(watched, event);
}

void NotesController::onClipNoteTextChanged() {
  if (!clipNotesEditor_ || !tagSession_ || !clipNoteDebounceTimer_) return;
  const quint64 tagId = selectedTagId();
  if (tagId == 0 || tagSession_->indexOfTagId(tagId) < 0) return;
  pendingClipNote_ = PendingClipNote{tagId, tagSession_->tagIdEpoch(), clipNotesEditor_->toPlainText()};
  clipNoteDebounceTimer_->start(400);
}

void NotesController::onMatchNoteTextChanged() {
  if (!matchNoteDebounceTimer_) return;
  matchNoteDebounceTimer_->start(400);
}

void NotesController::onMatchNoteTagMentionActivated(quint64 tagId) {
  if (!tagSession_ || !tagsTable_ || !tagsModel_) return;
  const int sessionIndex = tagSession_->indexOfTagId(tagId);
  if (!tagSession_->isValidTagIndex(sessionIndex)) return;

  const int row = tagsModel_->rowForTagId(tagId);
  if (row >= 0) {
    tagsTable_->selectRow(row);
    tagsTable_->scrollTo(tagsModel_->index(row, 0), QAbstractItemView::PositionAtCenter);
    if (videoPlayer_) {
      videoPlayer_->seekToMs(tagsModel_->markMsAt(row));
    }
    return;
  }

  if (videoPlayer_) {
    videoPlayer_->seekToMs(tagSession_->tags().at(sessionIndex).markMs);
  }
}

void NotesController::saveClipNoteDebounceFired() {
  flushPendingClipNote();
}

void NotesController::discardPendingClipNote() {
  if (clipNoteDebounceTimer_) clipNoteDebounceTimer_->stop();
  clipNoteDebounceShouldResume_ = false;
  pendingClipNote_.reset();
}

void NotesController::onTagSessionTagNoteChanged(int changedIndex) {
  if (!clipNotesEditor_ || !tagSession_) return;
  if (clipNotesEditor_->hasFocus() || pendingClipNote_.has_value()) return;
  if (!tagSession_->isValidTagIndex(changedIndex)) return;
  const quint64 changedTagId = tagSession_->tags().at(changedIndex).id;
  if (selectedTagId() != changedTagId) return;
  loadClipNoteForSelectedTag();
}

void NotesController::onTagSessionMatchNoteChanged() {
  if (!tagSession_) return;
  if (matchNotesEditor_ && matchNotesEditor_->hasFocus()) return;
  loadMatchNote();
}

void NotesController::showPendingClipNoteInEditor() {
  if (clipNotesEditor_->toPlainText() != pendingClipNote_->text) {
    clipNotesEditor_->setPlainText(pendingClipNote_->text);
  }
  clipNotesEditor_->setEnabled(true);
  clipNotesEditor_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
}

void NotesController::showDisabledClipNoteEditor() {
  if (!clipNotesEditor_->toPlainText().isEmpty()) {
    clipNotesEditor_->clear();
  }
  clipNotesEditor_->setEnabled(false);
  clipNotesEditor_->setPlaceholderText(AppLocale::trUi("notes.clip_placeholder_none"));
}

void NotesController::showStoredClipNoteInEditor(quint64 selectedTagId) {
  const int sessionIndex = sessionIndexForTagId(selectedTagId);
  if (tagSession_->isValidTagIndex(sessionIndex)) {
    const QString noteText = tagSession_->tagNote(sessionIndex);
    if (clipNotesEditor_->toPlainText() != noteText) {
      clipNotesEditor_->setPlainText(noteText);
    }
    clipNotesEditor_->setEnabled(true);
    clipNotesEditor_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
  } else {
    if (!clipNotesEditor_->toPlainText().isEmpty()) {
      clipNotesEditor_->clear();
    }
    clipNotesEditor_->setEnabled(true);
    clipNotesEditor_->setPlaceholderText(AppLocale::trUi("notes.clip_placeholder_none"));
  }
}

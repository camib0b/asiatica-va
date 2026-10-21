#include "NotesController.h"

#include "../components/MatchNotesEditor.h"
#include "../components/VideoPlayer.h"
#include "../i18n/AppLocale.h"
#include "../state/EventDefaults.h"
#include "../state/TagSession.h"
#include "TagsTableModel.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTableView>
#include <QTimer>

namespace {

QString formatTimestampMs(qint64 milliseconds) {
  const qint64 clampedMilliseconds = milliseconds < 0 ? 0 : milliseconds;
  const qint64 totalSeconds = clampedMilliseconds / 1000;
  const qint64 hours = totalSeconds / 3600;
  const qint64 minutes = (totalSeconds / 60) % 60;
  const qint64 seconds = totalSeconds % 60;

  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
  }

  const qint64 minutesTotal = totalSeconds / 60;
  return QStringLiteral("%1:%2")
      .arg(minutesTotal, 2, 10, QChar('0'))
      .arg(seconds, 2, 10, QChar('0'));
}

/// Removes segments that duplicate the session team labels (embedded in follow-up strings from GameControls).
QString followUpForEventColumn(const QString& followUpEvent, const TagSession* session) {
  if (!session || followUpEvent.isEmpty()) {
    return followUpEvent;
  }
  return AppLocale::followUpPathWithoutTeamSegments(followUpEvent, session->homeTeamName(),
                                                    session->awayTeamName());
}

}  // namespace

NotesController::NotesController(QObject* parent)
    : QObject(parent),
      tagSession_(nullptr),
      tagsTable_(nullptr),
      tagsModel_(nullptr),
      matchNotesLabel_(nullptr),
      matchNotesEditor_(nullptr),
      clipNotesLabel_(nullptr),
      notesEdit_(nullptr),
      videoPlayer_(nullptr),
      noteDebounceTimer_(nullptr),
      matchNoteDebounceTimer_(nullptr),
      resumeClipNoteTimer_(false),
      resumeMatchNoteTimer_(false) {
  noteDebounceTimer_ = new QTimer(this);
  noteDebounceTimer_->setSingleShot(true);
  connect(noteDebounceTimer_, &QTimer::timeout, this, &NotesController::saveNoteDebounceFired);

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

void NotesController::setColumnWidgets(QLabel* matchNotesLabel,
                                       MatchNotesEditor* matchNotesEditor,
                                       QLabel* clipNotesLabel,
                                       QPlainTextEdit* clipNotesEditor) {
  matchNotesLabel_ = matchNotesLabel;
  matchNotesEditor_ = matchNotesEditor;
  clipNotesLabel_ = clipNotesLabel;
  notesEdit_ = clipNotesEditor;
}

void NotesController::wireEditorSignals() {
  if (notesEdit_) {
    notesEdit_->installEventFilter(this);
    connect(notesEdit_, &QPlainTextEdit::textChanged, this, &NotesController::onNoteTextChanged);
  }
  if (matchNotesEditor_) {
    connect(matchNotesEditor_, &MatchNotesEditor::textChanged, this,
            &NotesController::onMatchNoteTextChanged);
    connect(matchNotesEditor_, &MatchNotesEditor::tagMentionActivated, this,
            &NotesController::onMatchNoteTagMentionActivated);
  }
}

void NotesController::connectSessionSignals() {
  if (!tagSession_) return;
  sessionConnections_.append(connect(
      tagSession_, &TagSession::aboutToClear, this, &NotesController::flushPendingClipNote,
      Qt::DirectConnection));
  sessionConnections_.append(connect(
      tagSession_, &TagSession::aboutToReplaceTags, this, &NotesController::flushPendingClipNote,
      Qt::DirectConnection));
  sessionConnections_.append(connect(
      tagSession_, &TagSession::tagNoteChanged, this, &NotesController::onTagSessionTagNoteChanged));
  sessionConnections_.append(connect(
      tagSession_, &TagSession::matchNoteChanged, this,
      &NotesController::onTagSessionMatchNoteChanged));
}

void NotesController::disconnectSessionSignals() {
  for (const QMetaObject::Connection& connection : sessionConnections_) {
    QObject::disconnect(connection);
  }
  sessionConnections_.clear();
}

void NotesController::applyUiStrings() const {
  if (notesEdit_) notesEdit_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
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
  return notesEdit_ && widget == notesEdit_.data();
}

void NotesController::flushPendingClipNote() {
  if (noteDebounceTimer_) noteDebounceTimer_->stop();
  resumeClipNoteTimer_ = false;
  if (!pendingClipNote_) return;

  const PendingClipNote note = *pendingClipNote_;
  pendingClipNote_.reset();
  if (!tagSession_) return;

  const int sessionIndex = tagSession_->indexOfTagId(note.tagId);
  const bool tagStillPresent = tagSession_->isValidTagIndex(sessionIndex);
  if (!pendingClipNoteApplies(note, tagSession_->tagIdEpoch(), tagStillPresent)) return;
  tagSession_->setTagNote(sessionIndex, note.text);
}

void NotesController::loadNoteForSelectedTag() {
  if (!notesEdit_) return;

  const QSignalBlocker notesBlocker(notesEdit_);
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
      if (EventDefaults::isTimeControlEvent(tag.mainEvent)) continue;
      const QString followUp = followUpForEventColumn(tag.followUpEvent, tagSession_);
      const QString eventLine = AppLocale::trDisplayTagLine(tag.mainEvent, followUp);
      const QString teamName = displayTeamForTag(tag);
      MatchNotesEditor::MentionCandidate candidate;
      candidate.tagId = tag.id;
      candidate.label = QStringLiteral("%1  %2  ·  %3")
                            .arg(formatTimestampMs(tag.markMs), eventLine, teamName);
      candidate.searchText = candidate.label;
      candidates.append(candidate);
    }
  }
  matchNotesEditor_->setMentionCandidates(candidates);
}

void NotesController::stopNoteTimers() {
  resumeClipNoteTimer_ = noteDebounceTimer_ && noteDebounceTimer_->isActive();
  resumeMatchNoteTimer_ = matchNoteDebounceTimer_ && matchNoteDebounceTimer_->isActive();
  if (noteDebounceTimer_) noteDebounceTimer_->stop();
  if (matchNoteDebounceTimer_) matchNoteDebounceTimer_->stop();
}

void NotesController::resumeNoteTimers() {
  if (resumeClipNoteTimer_ && pendingClipNote_ && noteDebounceTimer_) {
    noteDebounceTimer_->start(400);
  }
  if (resumeMatchNoteTimer_ && matchNoteDebounceTimer_) {
    matchNoteDebounceTimer_->start(400);
  }
  resumeClipNoteTimer_ = false;
  resumeMatchNoteTimer_ = false;
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
  if (watched == notesEdit_.data() && event && event->type() == QEvent::FocusOut) {
    flushPendingClipNote();
  }
  return QObject::eventFilter(watched, event);
}

void NotesController::onNoteTextChanged() {
  if (!notesEdit_ || !tagSession_ || !noteDebounceTimer_) return;
  const quint64 tagId = selectedTagId();
  if (tagId == 0 || tagSession_->indexOfTagId(tagId) < 0) return;
  pendingClipNote_ = PendingClipNote{tagId, tagSession_->tagIdEpoch(), notesEdit_->toPlainText()};
  noteDebounceTimer_->start(400);
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

void NotesController::saveNoteDebounceFired() {
  flushPendingClipNote();
}

void NotesController::discardPendingClipNote() {
  if (noteDebounceTimer_) noteDebounceTimer_->stop();
  resumeClipNoteTimer_ = false;
  pendingClipNote_.reset();
}

void NotesController::onTagSessionTagNoteChanged(int changedIndex) {
  if (!notesEdit_ || !tagSession_) return;
  if (notesEdit_->hasFocus() || pendingClipNote_.has_value()) return;
  if (!tagSession_->isValidTagIndex(changedIndex)) return;
  const quint64 changedTagId = tagSession_->tags().at(changedIndex).id;
  if (selectedTagId() != changedTagId) return;
  loadNoteForSelectedTag();
}

void NotesController::onTagSessionMatchNoteChanged() {
  if (!tagSession_) return;
  if (matchNotesEditor_ && matchNotesEditor_->hasFocus()) return;
  loadMatchNote();
}

void NotesController::showPendingClipNoteInEditor() {
  if (notesEdit_->toPlainText() != pendingClipNote_->text) {
    notesEdit_->setPlainText(pendingClipNote_->text);
  }
  notesEdit_->setEnabled(true);
  notesEdit_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
}

void NotesController::showDisabledClipNoteEditor() {
  if (!notesEdit_->toPlainText().isEmpty()) {
    notesEdit_->clear();
  }
  notesEdit_->setEnabled(false);
  notesEdit_->setPlaceholderText(AppLocale::trUi("notes.clip_placeholder_none"));
}

void NotesController::showStoredClipNoteInEditor(quint64 selectedTagId) {
  const int sessionIndex = sessionIndexForTagId(selectedTagId);
  if (tagSession_->isValidTagIndex(sessionIndex)) {
    const QString noteText = tagSession_->tagNote(sessionIndex);
    if (notesEdit_->toPlainText() != noteText) {
      notesEdit_->setPlainText(noteText);
    }
    notesEdit_->setEnabled(true);
    notesEdit_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
  } else {
    if (!notesEdit_->toPlainText().isEmpty()) {
      notesEdit_->clear();
    }
    notesEdit_->setEnabled(true);
    notesEdit_->setPlaceholderText(AppLocale::trUi("notes.clip_placeholder_none"));
  }
}

QString NotesController::displayTeamForTag(const TagSession::GameTag& tag) const {
  if (!tagSession_) {
    if (tag.team == QStringLiteral("Home")) return QStringLiteral("Home");
    if (tag.team == QStringLiteral("Away")) return QStringLiteral("Away");
    return tag.team.isEmpty() ? QStringLiteral("—") : tag.team;
  }
  if (tag.team == QStringLiteral("Home")) {
    const QString n = tagSession_->homeTeamName();
    return n.isEmpty() ? QStringLiteral("Home") : n;
  }
  if (tag.team == QStringLiteral("Away")) {
    const QString n = tagSession_->awayTeamName();
    return n.isEmpty() ? QStringLiteral("Away") : n;
  }
  return tag.team.isEmpty() ? QStringLiteral("—") : tag.team;
}

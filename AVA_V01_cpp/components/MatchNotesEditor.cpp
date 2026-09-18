#include "MatchNotesEditor.h"

#include "../i18n/AppLocale.h"

#include <QAbstractItemView>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QVariant>

namespace {
constexpr char kTagMentionScheme[] = "ava-tag:";
}

MatchNotesEditor::MatchNotesEditor(QWidget* parent) : QTextEdit(parent) {
  setObjectName(QStringLiteral("MatchNotesEditor"));
  setAcceptRichText(true);
  setTabChangesFocus(false);
  setMouseTracking(true);
  viewport()->setMouseTracking(true);
  document()->setDefaultStyleSheet(
      QStringLiteral("a { background-color: #18181b; color: #fafafa; text-decoration: none; }"));

  mentionPopup_ = new QListWidget(this);
  mentionPopup_->setObjectName(QStringLiteral("MatchNoteMentionPopup"));
  mentionPopup_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
  mentionPopup_->setAttribute(Qt::WA_ShowWithoutActivating);
  mentionPopup_->setAttribute(Qt::WA_StyledBackground, true);
  mentionPopup_->setFocusPolicy(Qt::NoFocus);
  mentionPopup_->setMouseTracking(true);
  mentionPopup_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  mentionPopup_->setAutoFillBackground(true);
  mentionPopup_->hide();

  connect(mentionPopup_, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
    insertSelectedMention();
  });

  applyUiStrings();
}

void MatchNotesEditor::applyUiStrings() {
  setPlaceholderText(AppLocale::trUi("notes.match_placeholder"));
  if (mentionPopup_ && mentionPopup_->isVisible()) {
    updateMentionFilterFromCursor();
  }
}

void MatchNotesEditor::setMentionCandidates(const QVector<MentionCandidate>& candidates) {
  candidates_ = candidates;
  if (mentionPopup_ && mentionPopup_->isVisible()) {
    updateMentionFilterFromCursor();
  }
}

void MatchNotesEditor::setSerializedHtml(const QString& html) {
  const int previousScroll = verticalScrollBar() ? verticalScrollBar()->value() : 0;
  blockSignals(true);
  if (html.trimmed().isEmpty()) {
    clear();
  } else {
    setHtml(html);
  }
  blockSignals(false);
  if (verticalScrollBar()) verticalScrollBar()->setValue(previousScroll);
}

QString MatchNotesEditor::serializedHtml() const {
  if (toPlainText().trimmed().isEmpty()) return QString();
  return toHtml();
}

bool MatchNotesEditor::isDocumentEquivalentTo(const QString& html) const {
  if (html.trimmed().isEmpty()) return toPlainText().trimmed().isEmpty();
  QTextDocument other;
  other.setHtml(html);
  return toPlainText() == other.toPlainText();
}

quint64 MatchNotesEditor::tagIdFromAnchor(const QString& href) const {
  if (!href.startsWith(QLatin1String(kTagMentionScheme))) return 0;
  bool ok = false;
  const quint64 tagId = href.mid(static_cast<int>(qstrlen(kTagMentionScheme))).toULongLong(&ok);
  return ok ? tagId : 0;
}

void MatchNotesEditor::keyPressEvent(QKeyEvent* event) {
  // Popup navigation keys are intentionally not forwarded to QTextEdit: Escape must
  // not bubble as an unhandled shortcut, Return must not insert a newline after a
  // mention, and Up/Down must move the popup selection instead of the text caret.
  if (mentionPopup_ && mentionPopup_->isVisible()) {
    if (event->key() == Qt::Key_Escape) {
      closeMentionPopup();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      if (insertSelectedMention()) {
        event->accept();
        return;
      }
      // No selectable mention — fall through so QTextEdit can insert a newline.
    } else if (event->key() == Qt::Key_Down) {
      const int nextRow = qMin(mentionPopup_->currentRow() + 1, mentionPopup_->count() - 1);
      mentionPopup_->setCurrentRow(qMax(0, nextRow));
      event->accept();
      return;
    } else if (event->key() == Qt::Key_Up) {
      const int nextRow = qMax(mentionPopup_->currentRow() - 1, 0);
      mentionPopup_->setCurrentRow(nextRow);
      event->accept();
      return;
    }
  }

  QTextEdit::keyPressEvent(event);

  if (event->text() == QLatin1Char('@')) {
    mentionStartPosition_ = textCursor().position() - 1;
    openMentionPopup();
    return;
  }

  if (mentionPopup_ && mentionPopup_->isVisible()) {
    updateMentionFilterFromCursor();
  }
}

void MatchNotesEditor::mousePressEvent(QMouseEvent* event) {
  const QString href = anchorAt(event->pos());
  const quint64 tagId = tagIdFromAnchor(href);
  // Always run the base implementation so focus, caret placement, and selection
  // still apply when the click lands on a tag mention.
  QTextEdit::mousePressEvent(event);
  if (tagId != 0) {
    emit tagMentionActivated(tagId);
  }
  if (mentionPopup_ && mentionPopup_->isVisible()) {
    updateMentionFilterFromCursor();
  }
}

void MatchNotesEditor::mouseMoveEvent(QMouseEvent* event) {
  const QString href = anchorAt(event->pos());
  if (tagIdFromAnchor(href) != 0) {
    viewport()->setCursor(Qt::PointingHandCursor);
  } else {
    viewport()->unsetCursor();
  }
  QTextEdit::mouseMoveEvent(event);
}

void MatchNotesEditor::focusOutEvent(QFocusEvent* event) {
  closeMentionPopup();
  QTextEdit::focusOutEvent(event);
}

void MatchNotesEditor::hideEvent(QHideEvent* event) {
  closeMentionPopup();
  QTextEdit::hideEvent(event);
}

void MatchNotesEditor::openMentionPopup() {
  if (!mentionPopup_) return;
  updateMentionFilterFromCursor();
}

void MatchNotesEditor::closeMentionPopup() {
  mentionStartPosition_ = -1;
  if (mentionPopup_) mentionPopup_->hide();
}

QString MatchNotesEditor::mentionQuery() const {
  if (mentionStartPosition_ < 0) return QString();
  const int cursorPosition = textCursor().position();
  if (cursorPosition < mentionStartPosition_) return QString();

  QTextCursor range(document());
  range.setPosition(mentionStartPosition_);
  range.setPosition(cursorPosition, QTextCursor::KeepAnchor);
  const QString selected = range.selectedText();
  if (!selected.startsWith(QLatin1Char('@'))) return QString();
  if (selected.contains(QChar::ParagraphSeparator) || selected.contains(QLatin1Char('\n'))) {
    return QString();
  }
  return selected.mid(1);
}

void MatchNotesEditor::updateMentionFilterFromCursor() {
  if (!mentionPopup_ || mentionStartPosition_ < 0) {
    closeMentionPopup();
    return;
  }
  if (textCursor().position() < mentionStartPosition_) {
    closeMentionPopup();
    return;
  }

  QTextCursor atCursor(document());
  atCursor.setPosition(mentionStartPosition_);
  atCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
  if (atCursor.selectedText() != QLatin1String("@")) {
    closeMentionPopup();
    return;
  }

  const QString filterText = mentionQuery();
  if (filterText.contains(QChar::ParagraphSeparator) || filterText.contains(QLatin1Char('\n'))) {
    closeMentionPopup();
    return;
  }
  mentionPopup_->clear();

  int matchCount = 0;
  for (const MentionCandidate& candidate : candidates_) {
    if (!filterText.isEmpty() &&
        !candidate.searchText.contains(filterText, Qt::CaseInsensitive) &&
        !candidate.label.contains(filterText, Qt::CaseInsensitive)) {
      continue;
    }
    auto* item = new QListWidgetItem(candidate.label, mentionPopup_);
    item->setData(Qt::UserRole, QVariant::fromValue(candidate.tagId));
    ++matchCount;
  }

  if (matchCount == 0) {
    auto* emptyItem = new QListWidgetItem(AppLocale::trUi("notes.mention_empty"), mentionPopup_);
    emptyItem->setFlags(Qt::NoItemFlags);
    emptyItem->setData(Qt::UserRole, QVariant::fromValue(quint64(0)));
  }

  mentionPopup_->setCurrentRow(matchCount > 0 ? 0 : -1);
  const int rowHeight = mentionPopup_->sizeHintForRow(0);
  const int visibleRows = qMin(qMax(1, mentionPopup_->count()), 8);
  mentionPopup_->setFixedSize(qMax(240, width()), visibleRows * qMax(24, rowHeight) + 8);
  positionMentionPopup();
  mentionPopup_->show();
  mentionPopup_->raise();
}

void MatchNotesEditor::positionMentionPopup() {
  if (!mentionPopup_) return;
  const QRect caretRect = cursorRect(textCursor());
  const QPoint caretBottomLeft(caretRect.left(), caretRect.bottom() + 4);
  const QPoint globalPos =
      viewport() ? viewport()->mapToGlobal(caretBottomLeft) : mapToGlobal(caretBottomLeft);
  mentionPopup_->move(globalPos);
  mentionPopup_->raise();
}

bool MatchNotesEditor::insertSelectedMention() {
  if (!mentionPopup_) return false;
  QListWidgetItem* item = mentionPopup_->currentItem();
  if (!item) return false;
  const quint64 tagId = item->data(Qt::UserRole).toULongLong();
  if (tagId == 0) return false;

  for (const MentionCandidate& candidate : candidates_) {
    if (candidate.tagId == tagId) {
      insertMention(candidate);
      return true;
    }
  }
  return false;
}

void MatchNotesEditor::insertMention(const MentionCandidate& candidate) {
  if (mentionStartPosition_ < 0) return;

  QTextCursor cursor(document());
  cursor.setPosition(mentionStartPosition_);
  cursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
  cursor.removeSelectedText();

  const QString escapedLabel = candidate.label.toHtmlEscaped();
  cursor.insertHtml(QStringLiteral("<a href=\"ava-tag:%1\">%2</a>&nbsp;")
                        .arg(candidate.tagId)
                        .arg(escapedLabel));
  setTextCursor(cursor);
  closeMentionPopup();
}

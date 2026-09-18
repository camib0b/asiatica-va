#pragma once

#include <QTextEdit>
#include <QVector>
#include <QtGlobal>

class QListWidget;
class QListWidgetItem;

/// Match-level notes with Cursor-style @-mentions of clip tags.
class MatchNotesEditor final : public QTextEdit {
  Q_OBJECT

public:
  struct MentionCandidate {
    quint64 tagId = 0;
    QString label;
    QString searchText;
  };

  explicit MatchNotesEditor(QWidget* parent = nullptr);

  void setMentionCandidates(const QVector<MentionCandidate>& candidates);
  void setSerializedHtml(const QString& html);
  QString serializedHtml() const;
  bool isDocumentEquivalentTo(const QString& html) const;
  void applyUiStrings();

signals:
  void tagMentionActivated(quint64 tagId);

protected:
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private:
  void openMentionPopup();
  void closeMentionPopup();
  void updateMentionFilterFromCursor();
  void insertSelectedMention();
  void insertMention(const MentionCandidate& candidate);
  void positionMentionPopup();
  quint64 tagIdFromAnchor(const QString& href) const;
  QString mentionQuery() const;

  QVector<MentionCandidate> candidates_;
  /// Qt::Tool window whose QObject parent stays this editor (never reparented).
  /// The parent tree deletes it; unique_ptr would double-delete.
  QListWidget* mentionPopup_ = nullptr;
  int mentionStartPosition_ = -1;
};

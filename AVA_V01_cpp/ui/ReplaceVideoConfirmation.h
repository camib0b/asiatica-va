#pragma once

class QWidget;
class TagSession;

/// What the analyst chose before a video replace that would drop session work.
enum class ReplaceVideoChoice {
  ContinueWithoutExport,
  ExportThenContinue,
  Cancel,
};

/// True when replacing the video would drop at least one tag or a visible match note.
bool replaceVideoWouldDiscardWork(const TagSession* session);

/// Returns ContinueWithoutExport immediately when \p session has nothing to lose.
/// Otherwise asks with the replace-video confirmation dialog.
ReplaceVideoChoice chooseReplaceVideoAction(const TagSession* session, QWidget* parent);

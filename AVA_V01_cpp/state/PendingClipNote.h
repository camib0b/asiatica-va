#pragma once

#include <QString>
#include <QtGlobal>

/// Unsaved clip-note edit. The session owns the saved text; this value exists
/// only until a flush writes it or a discard drops it.
///
/// `tagId` is not a stable identity across `clear()` or a replace import: both
/// restart ids at 1. `idEpoch` is the session epoch captured when the edit
/// began, so a note for the old tag 1 cannot be written onto the new tag 1.
struct PendingClipNote {
  quint64 tagId = 0;
  quint64 idEpoch = 0;
  QString text;
};

/// A flush may call `setTagNote` only while the epoch still matches and the
/// tag is still in the session. A zero id, a newer epoch, or a missing tag
/// refuses the write.
inline bool pendingClipNoteApplies(const PendingClipNote& note,
                                   quint64 currentIdEpoch,
                                   bool tagStillPresent) {
  if (note.tagId == 0) return false;
  if (note.idEpoch != currentIdEpoch) return false;
  return tagStillPresent;
}

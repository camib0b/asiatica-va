#pragma once

#include "ClipExporter.h"

#include <QString>

/// Maps a clip-export outcome onto a job phase. The message is status text only.
/// It is never inspected for the word "cancelled".
namespace ExportJobTransition {

enum class Phase {
    Exporting,
    Succeeded,
    Failed,
    Cancelled,
};

struct Decision {
    bool accepted = false;
    Phase phase = Phase::Exporting;
    QString statusText;
};

inline Decision apply(Phase current, ClipExporter::Outcome outcome, const QString& message) {
    Decision refused;
    refused.accepted = false;
    refused.phase = current;

    if (current != Phase::Exporting) return refused;

    Decision accepted;
    accepted.accepted = true;
    accepted.statusText = message;
    switch (outcome) {
    case ClipExporter::Outcome::Succeeded:
        accepted.phase = Phase::Succeeded;
        break;
    case ClipExporter::Outcome::Failed:
        accepted.phase = Phase::Failed;
        break;
    case ClipExporter::Outcome::Cancelled:
        accepted.phase = Phase::Cancelled;
        break;
    }
    return accepted;
}

}  // namespace ExportJobTransition

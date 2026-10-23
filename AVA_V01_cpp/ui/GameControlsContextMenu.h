#pragma once

#include "../state/EventCatalog.h"

#include <QPoint>
#include <QString>

#include <functional>

class QMenu;
class QWidget;
class TagSession;

/// Right-click actions for one tagging-grid button.
///
/// Rename, change key, hide or show, and the same edits for follow-ups that
/// already exist. The catalog does not add, remove, or reorder nodes, so this
/// menu does not offer those. Edits go through EventControlsValidation and then
/// TagSession::replaceEventLabelOverrides.
namespace GameControlsContextMenu {

void populate(QMenu* menu, TagSession* session, EventCatalog::EventId eventId,
              const std::function<void()>& openEditor);

void popup(QWidget* anchor, const QPoint& globalPosition, TagSession* session,
           EventCatalog::EventId eventId, const std::function<void()>& openEditor);

/// Yes / No box. The default button is No, so Enter does not confirm a hide or a reset.
bool confirmDestructive(QWidget* parent, const QString& title, const QString& message);

}  // namespace GameControlsContextMenu

#pragma once

#include "../state/EventCatalog.h"
#include "../state/EventLabelOverrides.h"

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

/// How a rename or a hide of a follow-up shared by several main buttons is applied.
enum class SharedScopeChoice {
  Cancel,
  AllInstances,
  ThisPath,
};

/// Lists every other path that uses the follow-up and asks whether the edit
/// applies to all of them or only to \p path. Cancel leaves the table unchanged.
SharedScopeChoice askSharedFollowUpScope(QWidget* parent,
                                         const EventLabelOverrides::Table& table,
                                         const EventLabelOverrides::FollowUpPath& path);

void populate(QMenu* menu, TagSession* session, EventCatalog::EventId eventId,
              const std::function<void()>& openEditor,
              const EventLabelOverrides::FollowUpPath& path = {});

void popup(QWidget* anchor, const QPoint& globalPosition, TagSession* session,
           EventCatalog::EventId eventId, const std::function<void()>& openEditor,
           const EventLabelOverrides::FollowUpPath& path = {});

/// Yes / No box. The default button is No, so Enter does not confirm a hide or a reset.
bool confirmDestructive(QWidget* parent, const QString& title, const QString& message);

}  // namespace GameControlsContextMenu

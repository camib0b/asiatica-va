#pragma once

#include "AppLocale.h"
#include "EventLabelOverrides.h"
#include "TagEvent.h"

class TagSession;

/// Display text for a catalog id, after the session's label overrides.
///
/// A stored label replaces the catalog text in every language. With no label
/// override, the result matches EventCatalog::labelForId / TagEvent::displayLabel.
/// Unlisted names (Inicio, Q1–Q4) have no catalog id and stay on AppLocale.
/// A null session means there are no overrides.
///
/// GameControls, the tagging toast, the tags table, the event log, stats,
/// analysis filters, presentation, clip-duration names, export filenames, and
/// the .clips.json overlay all read labels through this namespace. labelForId
/// stays on the catalog id, which is what stats rows and export codes use.
/// labelForPath and a tag line that knows its main event use a path label
/// when one is stored, and the id label otherwise.
namespace EventLabelResolver {

QString labelForId(EventCatalog::EventId id, const EventLabelOverrides::Table& overrides,
                   AppLocale::Language language);
QString labelForId(EventCatalog::EventId id, const EventLabelOverrides::Table& overrides);
QString labelForId(EventCatalog::EventId id, const TagSession* session,
                   AppLocale::Language language);
QString labelForId(EventCatalog::EventId id, const TagSession* session);

/// Path label when one is stored, otherwise labelForId of the chain's last id.
QString labelForPath(const EventLabelOverrides::FollowUpPath& path,
                     const EventLabelOverrides::Table& overrides, AppLocale::Language language);
QString labelForPath(const EventLabelOverrides::FollowUpPath& path,
                     const EventLabelOverrides::Table& overrides);
QString labelForPath(const EventLabelOverrides::FollowUpPath& path, const TagSession* session,
                     AppLocale::Language language);
QString labelForPath(const EventLabelOverrides::FollowUpPath& path, const TagSession* session);

QString labelForKey(const TagEvent::MainEventKey& key, const EventLabelOverrides::Table& overrides,
                    AppLocale::Language language);
QString labelForKey(const TagEvent::MainEventKey& key, const EventLabelOverrides::Table& overrides);
QString labelForKey(const TagEvent::MainEventKey& key, const TagSession* session,
                    AppLocale::Language language);
QString labelForKey(const TagEvent::MainEventKey& key, const TagSession* session);

QString followUpPath(const TagEvent::FollowUpChain& chain,
                     const EventLabelOverrides::Table& overrides, AppLocale::Language language);
QString followUpPath(const TagEvent::FollowUpChain& chain,
                     const EventLabelOverrides::Table& overrides);
QString followUpPath(const TagEvent::FollowUpChain& chain, const TagSession* session,
                     AppLocale::Language language);
QString followUpPath(const TagEvent::FollowUpChain& chain, const TagSession* session);

/// Each step uses the path label for the chain from \p mainEventId through that step.
QString followUpPath(EventCatalog::EventId mainEventId, const TagEvent::FollowUpChain& chain,
                     const EventLabelOverrides::Table& overrides, AppLocale::Language language);
QString followUpPath(EventCatalog::EventId mainEventId, const TagEvent::FollowUpChain& chain,
                     const EventLabelOverrides::Table& overrides);
QString followUpPath(EventCatalog::EventId mainEventId, const TagEvent::FollowUpChain& chain,
                     const TagSession* session, AppLocale::Language language);
QString followUpPath(EventCatalog::EventId mainEventId, const TagEvent::FollowUpChain& chain,
                     const TagSession* session);

QString tagLine(const TagEvent::MainEventKey& key, const TagEvent::FollowUpChain& chain,
                const EventLabelOverrides::Table& overrides, AppLocale::Language language);
QString tagLine(const TagEvent::MainEventKey& key, const TagEvent::FollowUpChain& chain,
                const EventLabelOverrides::Table& overrides);
QString tagLine(const TagEvent::MainEventKey& key, const TagEvent::FollowUpChain& chain,
                const TagSession* session, AppLocale::Language language);
QString tagLine(const TagEvent::MainEventKey& key, const TagEvent::FollowUpChain& chain,
                const TagSession* session);

}  // namespace EventLabelResolver

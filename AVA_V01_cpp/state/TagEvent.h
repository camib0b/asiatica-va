#pragma once

#include "AppLocale.h"
#include "EventCatalog.h"
#include "EventDefaults.h"

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>

#include <array>
#include <optional>

/// Catalog identity stored on a game tag.
///
/// A main event is either one EventCatalog id or an unlisted name (Inicio, TM,
/// Q1–Q4, and any token that is not a catalog main event). Follow-ups are a
/// chain of catalog ids. The team stays on GameTag::team and is not part of
/// the chain. XML and .clips.json still write the canonical English name.
namespace TagEvent {

struct FollowUpChain {
  std::array<EventCatalog::EventId, EventCatalog::kMaxFollowUpDepth> ids{};
  int count = 0;

  int size() const {
    if (count < 0) return 0;
    if (count > EventCatalog::kMaxFollowUpDepth) return EventCatalog::kMaxFollowUpDepth;
    return count;
  }

  bool empty() const { return size() == 0; }

  EventCatalog::EventId at(int index) const {
    if (index < 0 || index >= size()) return EventCatalog::EventId::None;
    return ids[static_cast<std::size_t>(index)];
  }

  bool contains(EventCatalog::EventId id) const {
    const int used = size();
    for (int index = 0; index < used; ++index) {
      if (ids[static_cast<std::size_t>(index)] == id) return true;
    }
    return false;
  }

  /// Empty \p prefix matches every chain. A chain matches when it equals the
  /// prefix or continues past it.
  bool startsWith(const FollowUpChain& prefix) const {
    const int prefixCount = prefix.size();
    if (prefixCount == 0) return true;
    if (prefixCount > size()) return false;
    for (int index = 0; index < prefixCount; ++index) {
      if (at(index) != prefix.at(index)) return false;
    }
    return true;
  }

  bool operator==(const FollowUpChain& other) const {
    const int used = size();
    if (used != other.size()) return false;
    for (int index = 0; index < used; ++index) {
      if (at(index) != other.at(index)) return false;
    }
    return true;
  }

  bool operator!=(const FollowUpChain& other) const { return !(*this == other); }
};

struct MainEventKey {
  EventCatalog::EventId id = EventCatalog::EventId::None;
  /// Set only when \p id is None. Holds time-control codes and any other
  /// token that is not a catalog main event.
  QString unlistedName;

  bool isEmpty() const {
    return id == EventCatalog::EventId::None && unlistedName.isEmpty();
  }

  bool isTimeControl() const {
    return id == EventCatalog::EventId::None && EventDefaults::isTimeControlEvent(unlistedName);
  }

  bool operator==(const MainEventKey& other) const {
    if (id != other.id) return false;
    if (id != EventCatalog::EventId::None) return true;
    return unlistedName == other.unlistedName;
  }

  bool operator!=(const MainEventKey& other) const { return !(*this == other); }
};

inline MainEventKey catalogKey(EventCatalog::EventId id) {
  MainEventKey key;
  key.id = id;
  return key;
}

inline MainEventKey unlistedKey(const QString& name) {
  MainEventKey key;
  key.unlistedName = name;
  return key;
}

inline FollowUpChain chainOf() { return {}; }

template <typename... EventIds>
FollowUpChain chainOf(EventIds... eventIds) {
  static_assert(sizeof...(EventIds) > 0, "use the empty chainOf overload");
  static_assert(sizeof...(EventIds) <= EventCatalog::kMaxFollowUpDepth,
                "follow-up chain is deeper than the catalog");
  FollowUpChain chain;
  const EventCatalog::EventId packed[] = {eventIds...};
  chain.count = static_cast<int>(sizeof...(EventIds));
  for (int index = 0; index < chain.count; ++index) {
    chain.ids[static_cast<std::size_t>(index)] = packed[index];
  }
  return chain;
}

inline bool containsScoredGoal(const FollowUpChain& chain) {
  return chain.contains(EventCatalog::EventId::FollowUpGoal);
}

inline size_t qHash(const FollowUpChain& chain, size_t seed = 0) noexcept {
  const int used = chain.size();
  size_t hash = ::qHash(used, seed);
  for (int index = 0; index < used; ++index) {
    hash = ::qHash(static_cast<int>(chain.at(index)), hash);
  }
  return hash;
}

inline size_t qHash(const MainEventKey& key, size_t seed = 0) noexcept {
  if (key.id != EventCatalog::EventId::None) {
    return ::qHash(static_cast<int>(key.id), seed);
  }
  return ::qHash(key.unlistedName, seed);
}

/// Exact catalog main-event name, or the trimmed text when it is not one.
/// "Pass" is the main event. "shot" is unlisted; lookup is not case-folded.
MainEventKey keyFromCanonicalName(const QString& name);

QString canonicalName(EventCatalog::EventId id);
QString canonicalName(const MainEventKey& key);

/// Canonical English follow-up names joined with the arrow separator.
QString canonicalFollowUpPath(const FollowUpChain& chain);

QString displayLabel(EventCatalog::EventId id, AppLocale::Language language);
QString displayLabel(EventCatalog::EventId id);
QString displayLabel(const MainEventKey& key, AppLocale::Language language);
QString displayLabel(const MainEventKey& key);

QString displayFollowUpPath(const FollowUpChain& chain, AppLocale::Language language);
QString displayFollowUpPath(const FollowUpChain& chain);

QString displayTagLine(const MainEventKey& key, const FollowUpChain& chain,
                       AppLocale::Language language);
QString displayTagLine(const MainEventKey& key, const FollowUpChain& chain);

/// Same bytes FollowUpCatalog::formatPayload writes for these canonical names.
/// Empty when \p mainEventId is None.
QString legacyFollowUpPayload(EventCatalog::EventId mainEventId, const FollowUpChain& chain,
                              const QString& teamLabel);

/// Reads a GameControls payload back into follow-up ids.
///
/// TeamOnly events return an empty chain. TeamThenChain drops a leading team
/// label and ChainThenTeam drops a trailing one. PlainChain keeps every
/// segment, so a team named "On target" cannot eat a Shot path. An empty
/// payload is an empty chain. A segment that is not the next catalog child
/// returns nullopt. Spanish display words are accepted. "No Goal" is not "Goal".
std::optional<FollowUpChain> parseLegacyFollowUpPayload(EventCatalog::EventId mainEventId,
                                                        const QString& payload,
                                                        const QString& homeTeamName,
                                                        const QString& awayTeamName);

/// Maps an exported XML <code> or a bare canonical name back to a main-event key.
/// "OR 16YD+" and "uc goal-" use the short code. "Q1" and "Inicio" stay unlisted.
/// Anything else is keyFromCanonicalName of the trimmed text.
MainEventKey mainEventKeyFromExportedCode(const QString& code);

QList<MainEventKey> sortedByCanonicalName(QList<MainEventKey> keys);

}  // namespace TagEvent

Q_DECLARE_METATYPE(TagEvent::MainEventKey)
Q_DECLARE_METATYPE(TagEvent::FollowUpChain)

#include "EventLabelResolver.h"

#include "EventCatalog.h"
#include "TagSession.h"

namespace EventLabelResolver {
namespace {

const EventLabelOverrides::Table& overridesFor(const TagSession* session) {
  static const EventLabelOverrides::Table emptyOverrides;
  if (!session) return emptyOverrides;
  return session->eventLabelOverrides();
}

QStringList labelsForChain(const TagEvent::FollowUpChain& chain,
                           const EventLabelOverrides::Table& overrides, bool spanish) {
  QStringList labels;
  const int used = chain.size();
  labels.reserve(used);
  const AppLocale::Language language =
      spanish ? AppLocale::Language::Spanish : AppLocale::Language::English;
  for (int index = 0; index < used; ++index) {
    const QString label = labelForId(chain.at(index), overrides, language);
    if (!label.isEmpty()) labels.append(label);
  }
  return labels;
}

}  // namespace

QString labelForId(EventCatalog::EventId id, const EventLabelOverrides::Table& overrides,
                   AppLocale::Language language) {
  if (overrides.hasLabel(id)) return overrides.label(id);
  return EventCatalog::labelForId(id, language == AppLocale::Language::Spanish);
}

QString labelForId(EventCatalog::EventId id, const EventLabelOverrides::Table& overrides) {
  return labelForId(id, overrides, AppLocale::currentLanguage());
}

QString labelForId(EventCatalog::EventId id, const TagSession* session,
                   AppLocale::Language language) {
  return labelForId(id, overridesFor(session), language);
}

QString labelForId(EventCatalog::EventId id, const TagSession* session) {
  return labelForId(id, session, AppLocale::currentLanguage());
}

QString labelForKey(const TagEvent::MainEventKey& key, const EventLabelOverrides::Table& overrides,
                    AppLocale::Language language) {
  if (key.id != EventCatalog::EventId::None) return labelForId(key.id, overrides, language);
  return AppLocale::trEventForLanguage(key.unlistedName, language);
}

QString labelForKey(const TagEvent::MainEventKey& key,
                    const EventLabelOverrides::Table& overrides) {
  return labelForKey(key, overrides, AppLocale::currentLanguage());
}

QString labelForKey(const TagEvent::MainEventKey& key, const TagSession* session,
                    AppLocale::Language language) {
  return labelForKey(key, overridesFor(session), language);
}

QString labelForKey(const TagEvent::MainEventKey& key, const TagSession* session) {
  return labelForKey(key, session, AppLocale::currentLanguage());
}

QString followUpPath(const TagEvent::FollowUpChain& chain,
                     const EventLabelOverrides::Table& overrides, AppLocale::Language language) {
  return labelsForChain(chain, overrides, language == AppLocale::Language::Spanish)
      .join(AppLocale::kCompoundPathSeparator);
}

QString followUpPath(const TagEvent::FollowUpChain& chain,
                     const EventLabelOverrides::Table& overrides) {
  return followUpPath(chain, overrides, AppLocale::currentLanguage());
}

QString followUpPath(const TagEvent::FollowUpChain& chain, const TagSession* session,
                     AppLocale::Language language) {
  return followUpPath(chain, overridesFor(session), language);
}

QString followUpPath(const TagEvent::FollowUpChain& chain, const TagSession* session) {
  return followUpPath(chain, session, AppLocale::currentLanguage());
}

QString tagLine(const TagEvent::MainEventKey& key, const TagEvent::FollowUpChain& chain,
                const EventLabelOverrides::Table& overrides, AppLocale::Language language) {
  const QString mainLabel = labelForKey(key, overrides, language);
  const QString followUpLabel = followUpPath(chain, overrides, language);
  if (followUpLabel.isEmpty()) return mainLabel;
  if (mainLabel.isEmpty()) return followUpLabel;
  return mainLabel + AppLocale::kCompoundPathSeparator + followUpLabel;
}

QString tagLine(const TagEvent::MainEventKey& key, const TagEvent::FollowUpChain& chain,
                const EventLabelOverrides::Table& overrides) {
  return tagLine(key, chain, overrides, AppLocale::currentLanguage());
}

QString tagLine(const TagEvent::MainEventKey& key, const TagEvent::FollowUpChain& chain,
                const TagSession* session, AppLocale::Language language) {
  return tagLine(key, chain, overridesFor(session), language);
}

QString tagLine(const TagEvent::MainEventKey& key, const TagEvent::FollowUpChain& chain,
                const TagSession* session) {
  return tagLine(key, chain, session, AppLocale::currentLanguage());
}

}  // namespace EventLabelResolver

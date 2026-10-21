#include "LocaleNotifier.h"

#include <QCoreApplication>

LocaleNotifier::LocaleNotifier(QObject* parent) : QObject(parent) {}

LocaleNotifier& LocaleNotifier::instance() {
  // QCoreApplication is the only owner. A function-local unique_ptr would
  // delete this object again after ~QCoreApplication has already destroyed
  // its children.
  static LocaleNotifier* const notifier = new LocaleNotifier(QCoreApplication::instance());
  return *notifier;
}

void LocaleNotifier::notifyLanguageChanged() const {
  // MOC signal stubs are non-const even though notification does not mutate state.
  const_cast<LocaleNotifier*>(this)->languageChanged();
}

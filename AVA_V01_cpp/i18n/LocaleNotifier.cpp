#include "LocaleNotifier.h"

#include <QCoreApplication>

LocaleNotifier::LocaleNotifier(QObject* parent) : QObject(parent) {}

LocaleNotifier& LocaleNotifier::instance() {
  // QApplication owns this object. A second static owner would touch it
  // again after that parent had already deleted it during shutdown.
  static LocaleNotifier* notifier = new LocaleNotifier(qApp);
  return *notifier;
}

void LocaleNotifier::notifyLanguageChanged() const {
  // MOC signal stubs are non-const even though notification does not mutate state.
  const_cast<LocaleNotifier*>(this)->languageChanged();
}

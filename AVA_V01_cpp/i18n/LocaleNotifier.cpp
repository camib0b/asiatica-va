#include "LocaleNotifier.h"

#include <QCoreApplication>

LocaleNotifier::LocaleNotifier(QObject* parent) : QObject(parent) {}

LocaleNotifier& LocaleNotifier::instance() {
  // QCoreApplication (the QApplication) is the only owner. A second static
  // owner would delete this object again after that parent had already
  // destroyed its children on shutdown. qApp in this file is
  // QCoreApplication::instance().
  static LocaleNotifier* const notifier = new LocaleNotifier(QCoreApplication::instance());
  return *notifier;
}

void LocaleNotifier::notifyLanguageChanged() const {
  // MOC signal stubs are non-const even though notification does not mutate state.
  const_cast<LocaleNotifier*>(this)->languageChanged();
}

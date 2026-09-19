#include "LocaleNotifier.h"

#include "../ui/QtPtr.h"

#include <QCoreApplication>

LocaleNotifier::LocaleNotifier(QObject* parent) : QObject(parent) {}

LocaleNotifier& LocaleNotifier::instance() {
  static std::unique_ptr<LocaleNotifier, QtParentDeleter> notifier{
      new LocaleNotifier(qApp)};
  return *notifier;
}

void LocaleNotifier::notifyLanguageChanged() const {
  // MOC signal stubs are non-const even though notification does not mutate state.
  const_cast<LocaleNotifier*>(this)->languageChanged();
}

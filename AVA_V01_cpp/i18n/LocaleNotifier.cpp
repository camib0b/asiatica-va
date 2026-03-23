#include "LocaleNotifier.h"

LocaleNotifier::LocaleNotifier(QObject* parent) : QObject(parent) {}

LocaleNotifier& LocaleNotifier::instance() {
  static LocaleNotifier notifier;
  return notifier;
}

void LocaleNotifier::emitLanguageChanged() {
  emit languageChanged();
}

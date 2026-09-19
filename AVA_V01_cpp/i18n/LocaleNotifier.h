#pragma once

#include <QObject>

class LocaleNotifier final : public QObject {
  Q_OBJECT

public:
  static LocaleNotifier& instance();

  void notifyLanguageChanged() const;

signals:
  void languageChanged();

private:
  explicit LocaleNotifier(QObject* parent = nullptr);
};

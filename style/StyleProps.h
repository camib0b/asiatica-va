#pragma once

#include <QWidget>
#include <QStyle>
#include <QVariant>

namespace Style {

inline void repolish(QWidget* w) {
  if (!w) return;
  if (auto* s = w->style()) {
    s->unpolish(w);
    s->polish(w);
  }
  w->update();
}

inline bool setProp(QWidget* w, const char* key, const QVariant& v) {
  if (!w || !key) return false;

  // If unchanged, skip the polish churn.
  if (w->property(key) == v) return false;

  if (auto* s = w->style()) s->unpolish(w);
  w->setProperty(key, v);
  if (auto* s = w->style()) s->polish(w);
  w->update();
  return true;
}

inline void setRole(QWidget* w, const char* role) { setProp(w, "role", role); }
inline void setVariant(QWidget* w, const char* var) { setProp(w, "variant", var); }
inline void setSize(QWidget* w, const char* size) { setProp(w, "size", size); }
inline void setState(QWidget* w, const char* key, bool on) { setProp(w, key, on); }

} // namespace Style

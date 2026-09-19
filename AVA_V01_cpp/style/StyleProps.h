#pragma once

#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QStyleOptionButton>
#include <QVariant>
#include <QWidget>

namespace Style {

/// Minimum width that fits label text in both normal and :focus QSS states.
inline int pushButtonMinimumWidth(const QPushButton* button) {
  if (!button) return 0;

  button->ensurePolished();

  auto widthForFocus = [button](bool focused) {
    QStyleOptionButton option;
    option.initFrom(button);
    if (focused) {
      option.state |= QStyle::State_HasFocus;
    } else {
      option.state &= ~QStyle::State_HasFocus;
    }
    option.text = button->text();
    option.icon = button->icon();
    option.iconSize = button->iconSize();

    const QSize textSize = button->fontMetrics().size(Qt::TextShowMnemonic, button->text());
    return button->style()->sizeFromContents(QStyle::CT_PushButton, &option, textSize, button).width();
  };

  return qMax(widthForFocus(false), widthForFocus(true));
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

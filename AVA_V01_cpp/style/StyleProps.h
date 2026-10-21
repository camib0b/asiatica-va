#pragma once

#include <QLayout>
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

/// Outer width that fits a button label, including stylesheet padding.
/// Text buttons use the style box model. Buttons whose caption is a child
/// label use the widget size hint, which already includes that padding.
inline int buttonWidthFittingContents(const QPushButton* button) {
  if (!button) return 0;
  button->ensurePolished();
  if (!button->text().isEmpty()) return pushButtonMinimumWidth(button);

  // A caption drawn by a child label is not button text. Measure that layout,
  // then add the style's border and padding for both focus states.
  QStyleOptionButton option;
  option.initFrom(button);
  const QSize content = button->layout() ? button->layout()->sizeHint() : QSize(0, 0);
  auto widthForFocus = [&](bool focused) {
    if (focused) {
      option.state |= QStyle::State_HasFocus;
    } else {
      option.state &= ~QStyle::State_HasFocus;
    }
    return button->style()->sizeFromContents(QStyle::CT_PushButton, &option, content, button).width();
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

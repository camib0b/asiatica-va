#pragma once

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

namespace TextInputFocus {

inline bool widgetMatchesTextInput(const QWidget* widget) {
  if (!widget) {
    return false;
  }
  if (qobject_cast<const QLineEdit*>(widget)) {
    return true;
  }
  if (qobject_cast<const QAbstractSpinBox*>(widget)) {
    return true;
  }
  if (qobject_cast<const QPlainTextEdit*>(widget)) {
    return true;
  }
  if (qobject_cast<const QTextEdit*>(widget)) {
    return true;
  }
  if (qobject_cast<const QComboBox*>(widget)) {
    return true;
  }
  return false;
}

/// True when \p widget, or its immediate parent (a text editor's viewport),
/// is a control that consumes typed characters.
inline bool widgetIsTextInput(const QWidget* widget) {
  if (widgetMatchesTextInput(widget)) {
    return true;
  }
  return widget && widgetMatchesTextInput(widget->parentWidget());
}

}  // namespace TextInputFocus

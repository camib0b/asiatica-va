#pragma once

#include "PresentationSelectionBreakdown.h"

#include <QFrame>

#include <functional>

class QScrollArea;
class QVBoxLayout;

/// Hover list of events that have clips checked in the presenting panel.
/// Choosing a row reports that event. The panel sets the Event filter.
class PresentationSelectionPopup final : public QFrame {
 public:
  explicit PresentationSelectionPopup(QWidget* parent = nullptr);

  void setEvents(const QVector<PresentationSelectionBreakdown::EventCount>& events, int popupWidth);
  void setActivationHandler(std::function<void(const TagEvent::MainEventKey&)> handler);

 private:
  void clearRows();

  QWidget* listHost_ = nullptr;
  QVBoxLayout* listLayout_ = nullptr;
  QScrollArea* scrollArea_ = nullptr;
  std::function<void(const TagEvent::MainEventKey&)> activationHandler_;
};

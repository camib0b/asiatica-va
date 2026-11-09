#pragma once

#include <Qt>

/// What a tagging-grid shortcut does once GameControls has already decided the
/// panel is on screen, no modifier is held, and focus is not in a text field.
///
/// GameControls::handleApplicationShortcut is the only caller. It still owns
/// the buttons. This function only chooses the action.
namespace TaggingKeys {

enum class Action {
  /// The key is not a tagging shortcut. Focus movement and the video keep it.
  Ignore,
  /// The key is a tagging shortcut, but this press must not activate it.
  Swallow,
  ActivateMainEvent,
  AdvanceQuarter,
  ChooseFollowUp,
  CancelFollowUp,
};

struct Context {
  bool autoRepeat = false;
  /// True while a main event is showing follow-up choices.
  /// FollowUpState::isIdle() is the inverse: the stage is None, or the main
  /// event name is empty.
  bool followUpPromptOpen = false;
  bool followUpRowVisible = false;
  int followUpChoiceCount = 0;
  /// True when this key is the shortcut of a visible main-event button.
  /// A reassigned G or H selects that event. It does not advance the quarter.
  bool keySelectsMainEvent = false;
};

struct Decision {
  Action action = Action::Ignore;
  /// Index into the visible follow-up buttons. Set only for ChooseFollowUp.
  int followUpIndex = -1;
};

inline bool isQuarterAdvanceKey(int key) {
  return key == Qt::Key_G || key == Qt::Key_H;
}

inline Decision decide(int key, const Context& context) {
  Decision decision;

  if (context.keySelectsMainEvent) {
    decision.action = Action::ActivateMainEvent;
  } else if (isQuarterAdvanceKey(key)) {
    decision.action = Action::AdvanceQuarter;
  } else if (key >= Qt::Key_1 && key <= Qt::Key_9) {
    if (!context.followUpRowVisible) return decision;
    const int followUpIndex = key - Qt::Key_1;
    if (followUpIndex < 0 || followUpIndex >= context.followUpChoiceCount) return decision;
    decision.action = Action::ChooseFollowUp;
    decision.followUpIndex = followUpIndex;
  } else if (key == Qt::Key_Escape) {
    if (!context.followUpPromptOpen) return decision;
    decision.action = Action::CancelFollowUp;
  } else {
    return decision;
  }

  // Holding G or H would close several quarters. A held letter or digit still
  // activates: the letter tags again, and the digit takes the next follow-up.
  if (context.autoRepeat && isQuarterAdvanceKey(key)) {
    decision.action = Action::Swallow;
    decision.followUpIndex = -1;
  }
  return decision;
}

}  // namespace TaggingKeys

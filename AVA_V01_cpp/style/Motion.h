#pragma once

#include <QColor>
#include <QEasingCurve>
#include <QVariant>

class QAbstractButton;
class QObject;
class QWidget;

/// Shared motion timings for AVA. Tagging clicks are never delayed: press
/// feedback is applied immediately, and every helper retargets the single
/// in-flight animation on that object instead of queueing another one.
namespace Motion {

inline constexpr int kPress = 100;
inline constexpr int kHoverIn = 150;
inline constexpr int kHoverOut = 100;
inline constexpr int kEnter = 200;
inline constexpr int kMilestone = 280;
inline constexpr int kStagger = 50;
/// Half of one idle breath. Longer than a transition on purpose: nothing
/// is waiting on it, and a faster loop would nag.
inline constexpr int kAmbientBreath = 1600;

/// Ease-out used for entrances and milestones.
/// cubic-bezier(0.23, 1, 0.32, 1) — starts fast and settles.
QEasingCurve strongOut();

/// Ease-in-out for something already on screen that moves and returns.
/// cubic-bezier(0.77, 0, 0.175, 1) — slow, fast, slow.
QEasingCurve strongInOut();

/// True when the OS asks for reduced motion or the coach turned on
/// "Reducir animaciones" in the gear menu.
bool reduced();

bool reducedMotionPreferred();
void setReducedMotionPreferred(bool preferred);
void loadFromSettings();

/// Fades \p widget in. \p risePixels lifts it that many pixels over the same
/// duration. \p delayMs staggers the start; the widget stays enabled the whole
/// time. Reduced motion jumps to the resting state.
void fadeIn(QWidget* widget, int durationMs = kEnter, int risePixels = 0, int delayMs = 0);

/// Animates a Q_PROPERTY of type QColor. Reduced motion or a non-positive
/// duration assigns \p target immediately. A second call for the same property
/// retargets the running animation.
void animateColor(QObject* object, const char* propertyName, const QColor& target, int durationMs);

/// One peak-and-return on a QColor property, settling on \p resting.
/// Reduced motion assigns \p resting and does not travel to \p peak.
void pulseOnce(QObject* object,
               const char* propertyName,
               const QColor& resting,
               const QColor& peak,
               int durationMs);

/// Animates any Qt property. \p curve is ignored when the change is instant.
void animateProperty(QObject* object,
                     const char* propertyName,
                     const QVariant& target,
                     int durationMs,
                     const QEasingCurve& curve);

struct ButtonColors {
  QColor restBackground;
  QColor restBorder;
  QColor hoverBackground;
  QColor hoverBorder;
  QColor pressedBackground;
  QColor pressedBorder;
  /// When true, keyboard focus keeps the zinc focus ring instead of the hover border.
  bool keepFocusRing = true;
};

/// Hover eases background and border via animateColor. Press snaps. The filter
/// never accepts the event, so the button's action runs on the same click.
void installButtonHover(QAbstractButton* button, const ButtonColors& colors);
void setButtonHoverColors(QAbstractButton* button, const ButtonColors& colors);

/// Instant flash colour, hold for \p holdMs, then ease back over kHoverIn.
void flashButtonColors(QAbstractButton* button,
                       const QColor& flashBackground,
                       const QColor& flashBorder,
                       int holdMs);

/// Idle breath: a soft ring around \p button. Hover and press ease the ring
/// away; press also scales the button to 0.97 and back. Reduced motion skips
/// the breath and the scale. The ring does not animate while \p active is false
/// (hide the host and pass false so a stacked page does not burn CPU).
void installAmbientPulse(QAbstractButton* button);
void setAmbientPulseActive(QAbstractButton* button, bool active);

}  // namespace Motion

#include "Motion.h"

#include "ThemeColors.h"

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QEvent>
#include <QGraphicsEffect>
#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPointF>
#include <QPointer>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QSettings>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include "QtPtr.h"
#include "StyleProps.h"

#include <QtGlobal>

#include <mutex>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MotionPlatform {
bool operatingSystemReducedMotion();
}

namespace {

constexpr char kSettingsGroup[] = "ui";
constexpr char kReduceMotionKey[] = "reduceAnimations";

struct ReduceMotionStore {
  std::mutex mutex;
  bool loaded = false;
  bool preferred = false;
};

ReduceMotionStore& reduceMotionStore() {
  static ReduceMotionStore store;
  return store;
}

void readPreferenceLocked(ReduceMotionStore& store) {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  store.preferred = settings.value(QLatin1String(kReduceMotionKey), false).toBool();
  settings.endGroup();
  store.loaded = true;
}

bool operatingSystemReducedMotion() {
#if defined(Q_OS_MACOS)
  return MotionPlatform::operatingSystemReducedMotion();
#elif defined(Q_OS_WIN)
  BOOL animationsEnabled = TRUE;
  if (SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0) == FALSE) {
    return false;
  }
  return animationsEnabled == FALSE;
#else
  return false;
#endif
}

QString propertyAnimationName(const char* propertyName) {
  return QStringLiteral("motion.property.%1").arg(QLatin1String(propertyName));
}

QString pulseAnimationName(const char* propertyName) {
  return QStringLiteral("motion.pulse.%1").arg(QLatin1String(propertyName));
}

void stopAnimationNamed(QObject* object, const QString& name) {
  if (!object) return;
  QAbstractAnimation* animation =
      object->findChild<QAbstractAnimation*>(name, Qt::FindDirectChildrenOnly);
  if (!animation) return;
  animation->stop();
  animation->setObjectName(QString());
  animation->deleteLater();
}

}  // namespace

class FadeDriver final : public QObject {
  Q_OBJECT
  Q_PROPERTY(qreal fadeOpacity READ fadeOpacity WRITE setFadeOpacity)
  Q_PROPERTY(int risePixels READ risePixels WRITE setRisePixels)

 public:
  explicit FadeDriver(QWidget* widget)
      : QObject(widget),
        widget_(widget),
        baseTopMargin_(widget ? widget->contentsMargins().top() : 0) {
    setObjectName(QStringLiteral("motion.fadeDriver"));
  }

  qreal fadeOpacity() const { return fadeOpacity_; }
  int risePixels() const { return risePixels_; }
  int pendingDurationMs() const { return pendingDurationMs_; }
  int pendingRisePixels() const { return pendingRisePixels_; }

  void setPending(int durationMs, int risePixels) {
    pendingDurationMs_ = durationMs;
    pendingRisePixels_ = risePixels;
  }

  void setSuppressFinish(bool suppress) { suppressFinish_ = suppress; }
  bool suppressFinish() const { return suppressFinish_; }

  void setFadeOpacity(qreal opacity) {
    fadeOpacity_ = opacity;
    ensureEffect();
    if (effect_) effect_->setOpacity(opacity);
  }

  void setRisePixels(int pixels) {
    risePixels_ = pixels;
    if (!widget_) return;
    QMargins margins = widget_->contentsMargins();
    margins.setTop(baseTopMargin_ + pixels);
    widget_->setContentsMargins(margins);
  }

  void clearEffect() {
    fadeOpacity_ = 1.0;
    if (!widget_) {
      effect_ = nullptr;
      return;
    }
    if (widget_->graphicsEffect() == effect_) {
      widget_->setGraphicsEffect(nullptr);
    }
    effect_ = nullptr;
  }

 private:
  void ensureEffect() {
    if (!widget_ || effect_) return;
    auto* effect = new QGraphicsOpacityEffect();
    effect->setOpacity(fadeOpacity_);
    widget_->setGraphicsEffect(effect);
    effect_ = effect;
  }

  QPointer<QWidget> widget_;
  QGraphicsOpacityEffect* effect_ = nullptr;
  int baseTopMargin_ = 0;
  qreal fadeOpacity_ = 1.0;
  int risePixels_ = 0;
  int pendingDurationMs_ = Motion::kEnter;
  int pendingRisePixels_ = 0;
  bool suppressFinish_ = false;
};

class ButtonColorDriver final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
  Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor)

 public:
  explicit ButtonColorDriver(QAbstractButton* button) : QObject(button), button_(button) {
    setObjectName(QStringLiteral("motion.buttonColor"));
  }

  Motion::ButtonColors colors;
  bool pointerInside = false;
  bool pressed = false;
  bool flashHolding = false;

  QColor backgroundColor() const { return backgroundColor_; }
  QColor borderColor() const { return borderColor_; }

  void setBackgroundColor(const QColor& color) {
    if (backgroundColor_ == color) return;
    backgroundColor_ = color;
    publish();
  }

  void setBorderColor(const QColor& color) {
    if (borderColor_ == color) return;
    borderColor_ = color;
    publish();
  }

  void applyInstant(const QColor& background, const QColor& border) {
    Motion::animateColor(this, "backgroundColor", background, 0);
    Motion::animateColor(this, "borderColor", border, 0);
  }

  void moveTo(const QColor& background, const QColor& border, int durationMs) {
    Motion::animateColor(this, "backgroundColor", background, durationMs);
    Motion::animateColor(this, "borderColor", border, durationMs);
  }

  void showCurrentState(int durationMs) {
    if (pressed) {
      applyInstant(colors.pressedBackground, colors.pressedBorder);
      return;
    }
    if (pointerInside) {
      moveTo(colors.hoverBackground, colors.hoverBorder, durationMs);
      return;
    }
    moveTo(colors.restBackground, colors.restBorder, durationMs);
  }

 private:
  void publish() {
    if (!button_) return;
    const QString background = backgroundColor_.name(QColor::HexArgb);
    const QString border = borderColor_.name(QColor::HexArgb);
    QString sheet = QStringLiteral(
                        "QPushButton, QToolButton {"
                        "  background-color: %1 !important;"
                        "  border-color: %2 !important;"
                        "}")
                        .arg(background, border);
    if (colors.keepFocusRing) {
      sheet += QStringLiteral(
                   "QPushButton:focus, QToolButton:focus {"
                   "  border-color: %1 !important;"
                   "}")
                   .arg(Style::ThemeColors::ring().name(QColor::HexRgb));
    }
    if (button_->styleSheet() == sheet) return;
    button_->setStyleSheet(sheet);
  }

  QPointer<QAbstractButton> button_;
  QColor backgroundColor_;
  QColor borderColor_;
};

namespace {

class ButtonHoverFilter final : public QObject {
 public:
  ButtonHoverFilter(ButtonColorDriver* driver, QAbstractButton* button)
      : QObject(button), driver_(driver), button_(button) {
    setObjectName(QStringLiteral("motion.hoverFilter"));
    button->setAttribute(Qt::WA_Hover, true);
    button->installEventFilter(this);
    QObject::connect(button, &QAbstractButton::pressed, this, [this]() {
      if (!driver_) return;
      driver_->flashHolding = false;
      driver_->pressed = true;
      driver_->applyInstant(driver_->colors.pressedBackground, driver_->colors.pressedBorder);
    });
    QObject::connect(button, &QAbstractButton::released, this, [this]() {
      if (!driver_) return;
      driver_->pressed = false;
      if (driver_->flashHolding) return;
      const int duration = driver_->pointerInside ? Motion::kHoverIn : Motion::kHoverOut;
      driver_->showCurrentState(duration);
    });
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (!driver_ || watched != button_) return false;
    if (event->type() == QEvent::HoverEnter) {
      driver_->pointerInside = true;
      if (button_->isEnabled() && !driver_->pressed && !driver_->flashHolding) {
        driver_->moveTo(driver_->colors.hoverBackground, driver_->colors.hoverBorder, Motion::kHoverIn);
      }
    } else if (event->type() == QEvent::HoverLeave || event->type() == QEvent::Hide) {
      driver_->pointerInside = false;
      if (!driver_->pressed && !driver_->flashHolding) {
        driver_->moveTo(driver_->colors.restBackground, driver_->colors.restBorder, Motion::kHoverOut);
      }
    }
    return false;
  }

 private:
  ButtonColorDriver* driver_ = nullptr;
  QPointer<QAbstractButton> button_;
};

FadeDriver* fadeDriverFor(QWidget* widget) {
  auto* driver = widget->findChild<FadeDriver*>(QStringLiteral("motion.fadeDriver"), Qt::FindDirectChildrenOnly);
  if (!driver) driver = new FadeDriver(widget);
  return driver;
}

ButtonColorDriver* buttonColorDriverFor(QAbstractButton* button, bool create) {
  auto* driver =
      button->findChild<ButtonColorDriver*>(QStringLiteral("motion.buttonColor"), Qt::FindDirectChildrenOnly);
  if (!driver && create) {
    driver = new ButtonColorDriver(button);
    new ButtonHoverFilter(driver, button);
  }
  return driver;
}

QPropertyAnimation* ensurePropertyAnimation(QObject* object, const char* propertyName) {
  const QString name = propertyAnimationName(propertyName);
  auto* animation = object->findChild<QPropertyAnimation*>(name, Qt::FindDirectChildrenOnly);
  if (animation) return animation;
  animation = new QPropertyAnimation(object, QByteArray(propertyName), object);
  animation->setObjectName(name);
  return animation;
}

void startFadeGroup(QWidget* widget, FadeDriver* driver, int durationMs) {
  const QString groupName = QStringLiteral("motion.fadeIn");
  auto* group = widget->findChild<QParallelAnimationGroup*>(groupName, Qt::FindDirectChildrenOnly);
  if (!group) {
    group = new QParallelAnimationGroup(widget);
    group->setObjectName(groupName);
    auto* opacityAnimation = new QPropertyAnimation(driver, "fadeOpacity", group);
    opacityAnimation->setObjectName(QStringLiteral("opacity"));
    auto* riseAnimation = new QPropertyAnimation(driver, "risePixels", group);
    riseAnimation->setObjectName(QStringLiteral("rise"));
    group->addAnimation(opacityAnimation);
    group->addAnimation(riseAnimation);
    QPointer<FadeDriver> driverGuard(driver);
    QObject::connect(group, &QAbstractAnimation::finished, widget, [driverGuard]() {
      if (!driverGuard || driverGuard->suppressFinish()) return;
      if (driverGuard->fadeOpacity() >= 0.999 && driverGuard->risePixels() == 0) {
        driverGuard->clearEffect();
      }
    });
  }

  driver->setSuppressFinish(true);
  group->stop();
  driver->setSuppressFinish(false);
  const QEasingCurve curve = Motion::strongOut();
  auto* opacityAnimation =
      group->findChild<QPropertyAnimation*>(QStringLiteral("opacity"), Qt::FindDirectChildrenOnly);
  auto* riseAnimation =
      group->findChild<QPropertyAnimation*>(QStringLiteral("rise"), Qt::FindDirectChildrenOnly);
  if (!opacityAnimation || !riseAnimation) return;

  opacityAnimation->setStartValue(driver->fadeOpacity());
  opacityAnimation->setEndValue(1.0);
  opacityAnimation->setDuration(durationMs);
  opacityAnimation->setEasingCurve(curve);

  riseAnimation->setStartValue(driver->risePixels());
  riseAnimation->setEndValue(0);
  riseAnimation->setDuration(durationMs);
  riseAnimation->setEasingCurve(curve);
  group->start();
}

}  // namespace

// Idle ring + press scale for one button. Parent the controller to the button
// so it dies with it. The ring is a sibling, inset into margins reserved on
// the button's parent, so the entrance fade (an opacity effect on that parent)
// does not have to composite a second effect on the button itself.
class PressScaleEffect final : public QGraphicsEffect {
 public:
  void setScaleFactor(qreal scaleFactor) {
    if (qAbs(scaleFactor_ - scaleFactor) < 0.001) return;
    scaleFactor_ = scaleFactor;
    update();
  }

 protected:
  void draw(QPainter* painter) override {
    QPoint offset;
    const QPixmap pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset);
    if (pixmap.isNull()) {
      drawSource(painter);
      return;
    }

    const qreal devicePixelRatio = pixmap.devicePixelRatio() > 0.0 ? pixmap.devicePixelRatio() : 1.0;
    const QSizeF logicalSize = QSizeF(pixmap.size()) / devicePixelRatio;
    const QPointF topLeft(offset);
    const QPointF center = topLeft + QPointF(logicalSize.width() / 2.0, logicalSize.height() / 2.0);
    painter->save();
    painter->translate(center);
    painter->scale(scaleFactor_, scaleFactor_);
    painter->translate(-center);
    painter->drawPixmap(topLeft, pixmap);
    painter->restore();
  }

 private:
  qreal scaleFactor_ = 1.0;
};

class AmbientPulseController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(qreal ringOpacity READ ringOpacity WRITE setRingOpacity)
  Q_PROPERTY(qreal pressScale READ pressScale WRITE setPressScale)

 public:
  explicit AmbientPulseController(QAbstractButton* button)
      : QObject(button),
        button_(button),
        hostWindow_(button ? button->window() : nullptr) {
    setObjectName(QStringLiteral("motion.ambientPulse"));
    if (!button_) return;

    reserveOutset();
    auto ring = makeQtPtr<QWidget>(button_->parentWidget());
    ring->setObjectName(QStringLiteral("motion.ambientRing"));
    ring->setFocusPolicy(Qt::NoFocus);
    ring->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    ring->setAttribute(Qt::WA_StyledBackground, true);
    ring->setAttribute(Qt::WA_TranslucentBackground, true);
    ring->setAttribute(Qt::WA_NoSystemBackground, true);
    ring->setAutoFillBackground(false);
    Style::setRole(ring.get(), "ambientRing");
    ring->hide();
    ring_ = ring.get();

    button_->setAttribute(Qt::WA_Hover, true);
    button_->installEventFilter(this);
    if (QWidget* host = button_->parentWidget()) host->installEventFilter(this);
    if (hostWindow_ && hostWindow_ != button_) hostWindow_->installEventFilter(this);

    QObject::connect(button_, &QAbstractButton::pressed, this, [this]() { handlePressed(); });
    QObject::connect(button_, &QAbstractButton::released, this, [this]() { handleReleased(); });
    repositionRing();
  }

  qreal ringOpacity() const { return ringOpacity_; }
  qreal pressScale() const { return pressScale_; }

  void setRingOpacity(qreal opacity) {
    ringOpacity_ = qBound(0.0, opacity, 1.0);
    if (!ring_) return;
    if (ringOpacity_ <= 0.001) {
      ringOpacity_ = 0.0;
      ring_->hide();
      return;
    }

    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(ring_->graphicsEffect());
    if (!effect) {
      effect = new QGraphicsOpacityEffect();
      effect->setOpacity(ringOpacity_);
      ring_->setGraphicsEffect(effect);
    } else {
      effect->setOpacity(ringOpacity_);
    }
    if (ring_->isHidden()) {
      repositionRing();
      ring_->show();
      ring_->lower();
    }
  }

  void setPressScale(qreal scale) {
    pressScale_ = scale;
    if (!button_) return;
    if (qAbs(scale - 1.0) < 0.004) {
      pressScale_ = 1.0;
      if (pressScaleEffect_) button_->setGraphicsEffect(nullptr);
      return;
    }

    QGraphicsEffect* existing = button_->graphicsEffect();
    if (existing && existing != pressScaleEffect_.data()) return;

    if (!pressScaleEffect_) {
      auto* effect = new PressScaleEffect();
      effect->setObjectName(QStringLiteral("motion.pressScaleEffect"));
      effect->setScaleFactor(scale);
      pressScaleEffect_ = effect;
      button_->setGraphicsEffect(effect);
      return;
    }
    static_cast<PressScaleEffect*>(pressScaleEffect_.data())->setScaleFactor(scale);
  }

  void setActive(bool active) {
    active_ = active;
    if (!active) {
      breathing_ = false;
      if (ringAnimation_) ringAnimation_->stop();
      setRingOpacity(0.0);
      if (scaleAnimation_) scaleAnimation_->stop();
      setPressScale(1.0);
      return;
    }
    repositionRing();
    resumeBreathIfIdle();
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (!button_ || !event) return false;

    if (watched == hostWindow_) {
      const QEvent::Type type = event->type();
      if (type == QEvent::WindowStateChange || type == QEvent::Hide || type == QEvent::Show) {
        syncHostVisibility();
      }
      return false;
    }

    if (watched == button_) {
      switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
          repositionRing();
          if (event->type() == QEvent::Show && active_) resumeBreathIfIdle();
          break;
        case QEvent::Hide:
          pointerInside_ = false;
          breathing_ = false;
          if (ringAnimation_) ringAnimation_->stop();
          setRingOpacity(0.0);
          if (scaleAnimation_) scaleAnimation_->stop();
          setPressScale(1.0);
          break;
        case QEvent::HoverEnter:
          pointerInside_ = true;
          breathing_ = false;
          animateRingOpacity(0.0, Motion::kHoverIn, Motion::strongOut());
          break;
        case QEvent::HoverLeave:
          pointerInside_ = false;
          resumeBreathIfIdle();
          break;
        case QEvent::EnabledChange:
          if (!button_->isEnabled()) {
            breathing_ = false;
            animateRingOpacity(0.0, Motion::kHoverOut, Motion::strongOut());
            if (scaleAnimation_) scaleAnimation_->stop();
            setPressScale(1.0);
          } else {
            resumeBreathIfIdle();
          }
          break;
        default:
          break;
      }
      return false;
    }

    if (watched == button_->parentWidget()) {
      const QEvent::Type type = event->type();
      if (type == QEvent::Resize || type == QEvent::LayoutRequest || type == QEvent::Move) {
        repositionRing();
      }
    }
    return false;
  }

 private:
  // Button radius in theme_light.qss is 6px. The ring's border-radius is
  // 6 + kAmbientOutsetPx, so this outset and that radius have to stay paired.
  static constexpr int kAmbientOutsetPx = 8;
  static constexpr qreal kAmbientPeakOpacity = 0.35;
  static constexpr qreal kPressedScale = 0.97;

  void reserveOutset() {
    if (!button_) return;
    QWidget* host = button_->parentWidget();
    if (!host || !host->layout()) return;
    QLayout* layout = host->layout();
    const QMargins margins = layout->contentsMargins();
    layout->setContentsMargins(qMax(margins.left(), kAmbientOutsetPx),
                                qMax(margins.top(), kAmbientOutsetPx),
                                qMax(margins.right(), kAmbientOutsetPx),
                                qMax(margins.bottom(), kAmbientOutsetPx));
  }

  void repositionRing() {
    if (!ring_ || !button_) return;
    const QRect next = button_->geometry().adjusted(-kAmbientOutsetPx, -kAmbientOutsetPx,
                                                     kAmbientOutsetPx, kAmbientOutsetPx);
    if (ring_->geometry() != next) ring_->setGeometry(next);
  }

  bool breathHostIsShowing() const {
    if (!button_ || !button_->isVisible()) return false;
    if (!hostWindow_) return false;
    return hostWindow_->isVisible() && !hostWindow_->isMinimized();
  }

  bool canBreath() const {
    if (!active_ || !button_ || !button_->isEnabled()) return false;
    if (pointerInside_ || buttonPressed_ || Motion::reduced()) return false;
    return breathHostIsShowing();
  }

  int breathDuration(qreal from, qreal to) const {
    if (kAmbientPeakOpacity <= 0.0) return Motion::kAmbientBreath;
    const qreal fraction = qBound(0.0, qAbs(to - from) / kAmbientPeakOpacity, 1.0);
    return qMax(Motion::kHoverIn, static_cast<int>(qRound(Motion::kAmbientBreath * fraction)));
  }

  void ensureRingAnimation() {
    if (ringAnimation_) return;
    ringAnimation_ = new QPropertyAnimation(this, "ringOpacity", this);
    ringAnimation_->setObjectName(QStringLiteral("motion.ambientRingOpacity"));
    QObject::connect(ringAnimation_, &QAbstractAnimation::finished, this, [this]() { continueBreath(); });
  }

  void ensureScaleAnimation() {
    if (scaleAnimation_) return;
    scaleAnimation_ = new QPropertyAnimation(this, "pressScale", this);
    scaleAnimation_->setObjectName(QStringLiteral("motion.pressScale"));
  }

  void animateRingOpacity(qreal target, int durationMs, const QEasingCurve& curve) {
    if (!ring_) return;
    ensureRingAnimation();
    ringAnimation_->stop();
    if (Motion::reduced()) {
      breathing_ = false;
      setRingOpacity(0.0);
      return;
    }
    if (qAbs(ringOpacity_ - target) < 0.001 || durationMs <= 0) {
      setRingOpacity(target);
      return;
    }
    ringAnimation_->setStartValue(ringOpacity_);
    ringAnimation_->setEndValue(target);
    ringAnimation_->setDuration(durationMs);
    ringAnimation_->setEasingCurve(curve);
    ringAnimation_->start();
  }

  void animatePressScale(qreal target, int durationMs) {
    if (!button_) return;
    if (Motion::reduced() || durationMs <= 0) {
      setPressScale(target);
      return;
    }
    ensureScaleAnimation();
    scaleAnimation_->stop();
    if (qAbs(pressScale_ - target) < 0.001) {
      setPressScale(target);
      return;
    }
    scaleAnimation_->setStartValue(pressScale_);
    scaleAnimation_->setEndValue(target);
    scaleAnimation_->setDuration(durationMs);
    scaleAnimation_->setEasingCurve(Motion::strongOut());
    scaleAnimation_->start();
  }

  void continueBreath() {
    if (!breathing_ || !canBreath()) {
      breathing_ = false;
      return;
    }
    const qreal target = ringOpacity_ <= (kAmbientPeakOpacity * 0.5) ? kAmbientPeakOpacity : 0.0;
    animateRingOpacity(target, breathDuration(ringOpacity_, target), Motion::strongInOut());
  }

  void resumeBreathIfIdle() {
    if (!canBreath()) {
      breathing_ = false;
      return;
    }
    if (breathing_ && ringAnimation_ && ringAnimation_->state() == QAbstractAnimation::Running) return;
    breathing_ = true;
    animateRingOpacity(kAmbientPeakOpacity, breathDuration(ringOpacity_, kAmbientPeakOpacity),
                       Motion::strongInOut());
  }

  void handlePressed() {
    buttonPressed_ = true;
    breathing_ = false;
    animateRingOpacity(0.0, Motion::kPress, Motion::strongOut());
    if (!Motion::reduced()) animatePressScale(kPressedScale, Motion::kPress);
  }

  void handleReleased() {
    buttonPressed_ = false;
    if (Motion::reduced()) setPressScale(1.0);
    else animatePressScale(1.0, Motion::kPress);
    resumeBreathIfIdle();
  }

  void syncHostVisibility() {
    if (breathHostIsShowing()) {
      if (active_) resumeBreathIfIdle();
      return;
    }
    breathing_ = false;
    if (ringAnimation_) ringAnimation_->stop();
    setRingOpacity(0.0);
    if (scaleAnimation_) scaleAnimation_->stop();
    setPressScale(1.0);
  }

  QPointer<QAbstractButton> button_;
  QPointer<QWidget> ring_;
  QPointer<QWidget> hostWindow_;
  QPointer<QGraphicsEffect> pressScaleEffect_;
  QPropertyAnimation* ringAnimation_ = nullptr;
  QPropertyAnimation* scaleAnimation_ = nullptr;
  qreal ringOpacity_ = 0.0;
  qreal pressScale_ = 1.0;
  bool active_ = false;
  bool breathing_ = false;
  bool pointerInside_ = false;
  bool buttonPressed_ = false;
};

namespace Motion {

QEasingCurve strongOut() {
  QEasingCurve curve(QEasingCurve::BezierSpline);
  curve.addCubicBezierSegment(QPointF(0.23, 1.0), QPointF(0.32, 1.0), QPointF(1.0, 1.0));
  return curve;
}

QEasingCurve strongInOut() {
  QEasingCurve curve(QEasingCurve::BezierSpline);
  curve.addCubicBezierSegment(QPointF(0.77, 0.0), QPointF(0.175, 1.0), QPointF(1.0, 1.0));
  return curve;
}

bool reducedMotionPreferred() {
  ReduceMotionStore& store = reduceMotionStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  if (!store.loaded) readPreferenceLocked(store);
  return store.preferred;
}

void setReducedMotionPreferred(bool preferred) {
  ReduceMotionStore& store = reduceMotionStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  store.loaded = true;
  if (store.preferred == preferred) return;
  store.preferred = preferred;
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.setValue(QLatin1String(kReduceMotionKey), preferred);
  settings.endGroup();
}

void loadFromSettings() {
  ReduceMotionStore& store = reduceMotionStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  readPreferenceLocked(store);
}

bool reduced() {
  return reducedMotionPreferred() || operatingSystemReducedMotion();
}

void animateProperty(QObject* object,
                     const char* propertyName,
                     const QVariant& target,
                     int durationMs,
                     const QEasingCurve& curve) {
  if (!object || propertyName == nullptr || propertyName[0] == '\0') return;

  QPropertyAnimation* animation = ensurePropertyAnimation(object, propertyName);
  animation->stop();
  if (reduced() || durationMs <= 0) {
    object->setProperty(propertyName, target);
    return;
  }

  const QVariant current = object->property(propertyName);
  if (current == target) return;

  animation->setStartValue(current.isValid() ? current : target);
  animation->setEndValue(target);
  animation->setDuration(durationMs);
  animation->setEasingCurve(curve);
  animation->start();
}

void animateColor(QObject* object, const char* propertyName, const QColor& target, int durationMs) {
  if (!object || propertyName == nullptr) return;
  stopAnimationNamed(object, pulseAnimationName(propertyName));
  animateProperty(object, propertyName, QVariant::fromValue(target), durationMs, strongOut());
}

void pulseOnce(QObject* object,
               const char* propertyName,
               const QColor& resting,
               const QColor& peak,
               int durationMs) {
  if (!object || propertyName == nullptr || propertyName[0] == '\0') return;

  stopAnimationNamed(object, pulseAnimationName(propertyName));
  if (QPropertyAnimation* colorAnimation =
          object->findChild<QPropertyAnimation*>(propertyAnimationName(propertyName), Qt::FindDirectChildrenOnly)) {
    colorAnimation->stop();
  }

  if (reduced() || durationMs <= 0) {
    object->setProperty(propertyName, resting);
    return;
  }

  const QVariant currentValue = object->property(propertyName);
  const QColor startColor = currentValue.canConvert<QColor>() ? currentValue.value<QColor>() : resting;
  const int riseDuration = qMax(1, durationMs / 2);
  const int settleDuration = qMax(1, durationMs - riseDuration);
  const QEasingCurve curve = strongOut();

  auto* group = new QSequentialAnimationGroup(object);
  group->setObjectName(pulseAnimationName(propertyName));

  auto* rise = new QPropertyAnimation(object, QByteArray(propertyName), group);
  rise->setStartValue(startColor);
  rise->setEndValue(peak);
  rise->setDuration(riseDuration);
  rise->setEasingCurve(curve);

  auto* settle = new QPropertyAnimation(object, QByteArray(propertyName), group);
  settle->setStartValue(peak);
  settle->setEndValue(resting);
  settle->setDuration(settleDuration);
  settle->setEasingCurve(curve);

  group->addAnimation(rise);
  group->addAnimation(settle);
  QObject::connect(group, &QAbstractAnimation::finished, group, &QObject::deleteLater);
  group->start();
}

void fadeIn(QWidget* widget, int durationMs, int risePixels, int delayMs) {
  if (!widget) return;
  FadeDriver* driver = fadeDriverFor(widget);
  driver->setPending(durationMs, risePixels);

  auto* delayTimer = widget->findChild<QTimer*>(QStringLiteral("motion.fadeDelay"), Qt::FindDirectChildrenOnly);
  if (delayTimer) delayTimer->stop();

  if (reduced() || durationMs <= 0) {
    if (QParallelAnimationGroup* group =
            widget->findChild<QParallelAnimationGroup*>(QStringLiteral("motion.fadeIn"),
                                                        Qt::FindDirectChildrenOnly)) {
      group->stop();
    }
    driver->setRisePixels(0);
    driver->clearEffect();
    return;
  }

  auto* runningGroup =
      widget->findChild<QParallelAnimationGroup*>(QStringLiteral("motion.fadeIn"), Qt::FindDirectChildrenOnly);
  const bool fadeRunning = runningGroup && runningGroup->state() == QAbstractAnimation::Running;
  if (!fadeRunning) {
    driver->setFadeOpacity(0.0);
    driver->setRisePixels(qMax(0, risePixels));
  }

  if (delayMs > 0) {
    if (!delayTimer) {
      delayTimer = new QTimer(widget);
      delayTimer->setObjectName(QStringLiteral("motion.fadeDelay"));
      delayTimer->setSingleShot(true);
      QObject::connect(delayTimer, &QTimer::timeout, widget, [guard = QPointer<QWidget>(widget)]() {
        if (!guard) return;
        FadeDriver* waiting =
            guard->findChild<FadeDriver*>(QStringLiteral("motion.fadeDriver"), Qt::FindDirectChildrenOnly);
        if (!waiting) return;
        fadeIn(guard, waiting->pendingDurationMs(), waiting->pendingRisePixels(), 0);
      });
    }
    delayTimer->start(delayMs);
    return;
  }

  startFadeGroup(widget, driver, durationMs);
}

void setButtonHoverColors(QAbstractButton* button, const ButtonColors& colors) {
  if (!button) return;
  ButtonColorDriver* driver = buttonColorDriverFor(button, true);
  if (!driver) return;
  driver->colors = colors;
  if (driver->flashHolding) return;
  driver->showCurrentState(0);
}

void installButtonHover(QAbstractButton* button, const ButtonColors& colors) {
  setButtonHoverColors(button, colors);
}

void flashButtonColors(QAbstractButton* button,
                       const QColor& flashBackground,
                       const QColor& flashBorder,
                       int holdMs) {
  if (!button) return;
  ButtonColorDriver* driver = buttonColorDriverFor(button, false);
  if (!driver) return;

  driver->flashHolding = true;
  driver->applyInstant(flashBackground, flashBorder);

  auto* holdTimer = button->findChild<QTimer*>(QStringLiteral("motion.flashHold"), Qt::FindDirectChildrenOnly);
  if (!holdTimer) {
    holdTimer = new QTimer(button);
    holdTimer->setObjectName(QStringLiteral("motion.flashHold"));
    holdTimer->setSingleShot(true);
    QObject::connect(holdTimer, &QTimer::timeout, button, [guard = QPointer<QAbstractButton>(button)]() {
      if (!guard) return;
      ButtonColorDriver* waiting = buttonColorDriverFor(guard, false);
      if (!waiting) return;
      waiting->flashHolding = false;
      const int duration = reduced() ? 0 : kHoverIn;
      waiting->showCurrentState(duration);
    });
  }
  holdTimer->start(qMax(0, holdMs));
}

void installAmbientPulse(QAbstractButton* button) {
  if (!button || !button->parentWidget()) return;
  if (button->findChild<AmbientPulseController*>(QStringLiteral("motion.ambientPulse"),
                                                 Qt::FindDirectChildrenOnly)) {
    return;
  }
  new AmbientPulseController(button);
}

void setAmbientPulseActive(QAbstractButton* button, bool active) {
  if (!button) return;
  auto* controller = button->findChild<AmbientPulseController*>(QStringLiteral("motion.ambientPulse"),
                                                                Qt::FindDirectChildrenOnly);
  if (!controller) return;
  controller->setActive(active);
}

}  // namespace Motion

#include "Motion.moc"

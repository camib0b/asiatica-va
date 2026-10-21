#include "TagToastOverlay.h"

#include "../style/Motion.h"
#include "../style/StyleProps.h"
#include "../style/ThemeColors.h"
#include "../ui/QtPtr.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QTimer>
#include <QVariant>

namespace {

constexpr int kAccentWidthPixels = 4;
constexpr int kCornerRadiusPixels = 10;
constexpr int kTrayFillAlpha = 230;
constexpr int kHairlineAlpha = 36;
constexpr int kFallbackToastWidthPixels = 420;

QString singleLine(const QString& text) {
  QString collapsed = text;
  collapsed.replace(QLatin1Char('\n'), QLatin1Char(' '));
  collapsed.replace(QLatin1Char('\r'), QLatin1Char(' '));
  return collapsed.simplified();
}

QColor resolveTeamColor(const QColor& color) {
  if (!color.isValid() || color.alpha() == 0) {
    return Style::ThemeColors::surface();
  }
  return color;
}

QColor readableOn(const QColor& background) {
  // Lightness is 0–255. Yellow kits need dark type; navy kits need light type.
  if (background.lightness() >= 150) {
    return Style::ThemeColors::foreground();
  }
  return QColor(QStringLiteral("#fafafa"));
}

class TagToastKeyChip final : public QWidget {
 public:
  explicit TagToastKeyChip(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QFont keyFont = font();
    keyFont.setBold(true);
    setFont(keyFont);
  }

  void setKey(const QString& key, const QColor& color) {
    key_ = key;
    color_ = color;
    setVisible(!key_.isEmpty());
    updateGeometry();
    update();
  }

  QSize sizeHint() const override {
    const QFontMetrics metrics(font());
    const QString sample = key_.isEmpty() ? QStringLiteral("W") : key_;
    const int textWidth = metrics.horizontalAdvance(sample);
    const int side = qMax(metrics.height() + 8, textWidth + 12);
    return QSize(side, qMax(22, metrics.height() + 8));
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    if (key_.isEmpty()) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 5.0, 5.0);
    painter.fillPath(path, color_);
    painter.setPen(readableOn(color_));
    painter.drawText(rect(), Qt::AlignCenter, key_);
  }

 private:
  QString key_;
  QColor color_ = Style::ThemeColors::surface();
};

}  // namespace

QString TagToastOverlay::chainSeparator() {
  return QStringLiteral(" \u203A ");
}

TagToastOverlay::TagToastOverlay(QWidget* parent)
    : QWidget(parent),
      videoFrame_(nullptr),
      anchorWindow_(nullptr),
      chainLabel_(nullptr),
      keyChip_(nullptr),
      dismissTimer_(nullptr),
      teamColor_(Style::ThemeColors::surface()),
      chainOpen_(false),
      presented_(false),
      repositioning_(false),
      presentationGeneration_(0) {
  setObjectName(QStringLiteral("TagToastOverlay"));
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus |
                 Qt::NoDropShadowWindowHint | Qt::WindowTransparentForInput);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setFocusPolicy(Qt::NoFocus);
  setAutoFillBackground(false);
  setMaximumHeight(kMaximumBodyHeightPixels);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(12, 7, 12, 7);
  layout->setSpacing(8);

  auto keyChip = makeQtPtr<TagToastKeyChip>(this);
  keyChip_ = keyChip.get();
  keyChip_->hide();
  layout->addWidget(keyChip.get(), 0, Qt::AlignVCenter);

  auto chainLabel = makeQtPtr<QLabel>(this);
  chainLabel_ = chainLabel.get();
  chainLabel_->setObjectName(QStringLiteral("TagToastChainLabel"));
  chainLabel_->setTextFormat(Qt::PlainText);
  chainLabel_->setTextInteractionFlags(Qt::NoTextInteraction);
  chainLabel_->setWordWrap(true);
  chainLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  chainLabel_->setFocusPolicy(Qt::NoFocus);
  chainLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  chainLabel_->setMinimumWidth(0);
  chainLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  Style::setRole(chainLabel_, "tagToastChain");
  layout->addWidget(chainLabel_, 1, Qt::AlignVCenter);

  auto dismissTimer = makeQtPtr<QTimer>(this);
  dismissTimer_ = dismissTimer.get();
  dismissTimer_->setSingleShot(true);
  connect(dismissTimer_, &QTimer::timeout, this, [this]() {
    if (chainOpen_) return;
    startDismiss(false);
  });

  hide();
}

void TagToastOverlay::trackVideoFrame(QWidget* videoFrame) {
  if (videoFrame_ == videoFrame) {
    reposition();
    return;
  }
  if (videoFrame_) videoFrame_->removeEventFilter(this);
  if (anchorWindow_) anchorWindow_->removeEventFilter(this);
  videoFrame_ = videoFrame;
  anchorWindow_ = nullptr;
  if (!videoFrame_) return;
  videoFrame_->installEventFilter(this);
  QWidget* window = videoFrame_->window();
  if (window && window != videoFrame_) {
    anchorWindow_ = window;
    anchorWindow_->installEventFilter(this);
  }
  reposition();
}

void TagToastOverlay::beginEvent(const QString& shortcutKey, const QString& eventName,
                                const QColor& teamColor, bool chainOpen) {
  const QString name = singleLine(eventName);
  const QString key = singleLine(shortcutKey);
  if (name.isEmpty() && key.isEmpty()) {
    dismissImmediately();
    return;
  }
  steps_.clear();
  if (!name.isEmpty()) steps_.append(name);
  shortcutKey_ = key;
  teamColor_ = resolveTeamColor(teamColor);
  chainOpen_ = chainOpen;
  present();
}

void TagToastOverlay::appendFollowUp(const QString& answer, bool chainOpen) {
  const QString step = singleLine(answer);
  if (step.isEmpty() || steps_.isEmpty()) return;
  steps_.append(step);
  chainOpen_ = chainOpen;
  present();
}

void TagToastOverlay::dismissImmediately() {
  startDismiss(true);
}

void TagToastOverlay::suspend() {
  if (!presented_) return;
  if (dismissTimer_) dismissTimer_->stop();
  ++presentationGeneration_;
  presented_ = false;
  hide();
  setWindowOpacity(0.0);
}

void TagToastOverlay::resumeOpenChain() {
  if (!chainOpen_ || steps_.isEmpty() || presented_) return;
  present();
}

QString TagToastOverlay::chainText() const {
  return steps_.join(chainSeparator());
}

QString TagToastOverlay::shortcutKey() const {
  return shortcutKey_;
}

QString TagToastOverlay::displayLine() const {
  const QString chain = chainText();
  if (shortcutKey_.isEmpty()) return chain;
  if (chain.isEmpty()) return shortcutKey_;
  return shortcutKey_ + QStringLiteral("  ") + chain;
}

void TagToastOverlay::present() {
  ++presentationGeneration_;
  presented_ = true;
  refreshText();
  scheduleDismiss();
  const bool anchorVisible = !videoFrame_ || videoFrame_->isVisible();
  if (!anchorVisible) {
    hide();
    return;
  }
  const bool wasHidden = !isVisible();
  reposition();
  if (wasHidden) {
    setWindowOpacity(Motion::reduced() ? 1.0 : 0.0);
    show();
    raise();
  }
  if (!Motion::reduced()) {
    Motion::animateProperty(this, "windowOpacity", QVariant(1.0), Motion::kPress, Motion::strongOut());
  } else {
    setWindowOpacity(1.0);
  }
}

void TagToastOverlay::scheduleDismiss() {
  if (!dismissTimer_) return;
  dismissTimer_->stop();
  if (chainOpen_) return;
  const int delay = steps_.size() <= 1 ? kPassingDismissMilliseconds
                                        : kChainCompleteDismissMilliseconds;
  dismissTimer_->start(delay);
}

void TagToastOverlay::refreshText() {
  if (chainLabel_) chainLabel_->setText(chainText());
  if (auto* chip = static_cast<TagToastKeyChip*>(keyChip_)) {
    chip->setKey(shortcutKey_, teamColor_);
  }
  setAccessibleName(displayLine());
  update();
}

void TagToastOverlay::clearSequence() {
  steps_.clear();
  shortcutKey_.clear();
  chainOpen_ = false;
  if (chainLabel_) chainLabel_->clear();
  if (auto* chip = static_cast<TagToastKeyChip*>(keyChip_)) {
    chip->setKey(QString(), teamColor_);
  }
  setAccessibleName(QString());
}

void TagToastOverlay::startDismiss(bool immediate) {
  if (dismissTimer_) dismissTimer_->stop();
  if (!presented_ && steps_.isEmpty()) return;
  ++presentationGeneration_;
  presented_ = false;
  chainOpen_ = false;
  if (immediate || Motion::reduced()) {
    clearSequence();
    Motion::animateProperty(this, "windowOpacity", QVariant(0.0), 0, Motion::strongOut());
    hide();
    return;
  }
  const int generation = presentationGeneration_;
  Motion::animateProperty(this, "windowOpacity", QVariant(0.0), Motion::kHoverOut, Motion::strongOut());
  QTimer::singleShot(Motion::kHoverOut + 30, this, [this, generation]() {
    if (generation != presentationGeneration_ || presented_) return;
    clearSequence();
    hide();
  });
}

void TagToastOverlay::reposition() {
  if (repositioning_ || !chainLabel_) return;
  repositioning_ = true;

  chainLabel_->ensurePolished();
  int availableWidth = kFallbackToastWidthPixels;
  if (videoFrame_ && videoFrame_->width() > 0) {
    availableWidth = qMax(160, videoFrame_->width() - (kTopMarginPixels * 2));
  }
  setMaximumWidth(availableWidth);
  setMaximumHeight(kMaximumBodyHeightPixels);

  const bool chipVisible = keyChip_ && keyChip_->isVisible();
  const int chipWidth = chipVisible ? keyChip_->sizeHint().width() : 0;
  const QMargins margins = contentsMargins();
  const int spacing = layout() ? layout()->spacing() : 0;
  const int chrome = margins.left() + margins.right() + chipWidth + (chipVisible ? spacing : 0);
  const int textBudget = qMax(48, availableWidth - chrome);
  const int naturalTextWidth = chainLabel_->fontMetrics().horizontalAdvance(chainLabel_->text());
  chainLabel_->setFixedWidth(qMax(1, qMin(naturalTextWidth + 2, textBudget)));

  adjustSize();
  if (width() > availableWidth) {
    resize(availableWidth, height());
  }
  if (height() > kMaximumBodyHeightPixels) {
    resize(width(), kMaximumBodyHeightPixels);
  }

  if (videoFrame_ && presented_) {
    const QPoint origin = videoFrame_->mapToGlobal(QPoint(0, 0));
    const int x = origin.x() + qMax(0, (videoFrame_->width() - width()) / 2);
    const int y = origin.y() + kTopMarginPixels;
    move(x, y);
  }

  repositioning_ = false;
}

void TagToastOverlay::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  QPainterPath tray;
  tray.addRoundedRect(bounds, kCornerRadiusPixels, kCornerRadiusPixels);
  painter.fillPath(tray, QColor(9, 9, 11, kTrayFillAlpha));

  painter.save();
  painter.setClipPath(tray);
  painter.fillRect(QRectF(0, 0, kAccentWidthPixels, height()), teamColor_);
  painter.restore();

  painter.setPen(QPen(QColor(255, 255, 255, kHairlineAlpha), 1.0));
  painter.drawPath(tray);
}

bool TagToastOverlay::eventFilter(QObject* watched, QEvent* event) {
  if (!event) return false;
  if (watched != videoFrame_ && watched != anchorWindow_) return false;
  switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::WindowStateChange:
      if (presented_) {
        const bool anchorVisible = videoFrame_ && videoFrame_->isVisible();
        if (videoFrame_ && !anchorVisible) {
          hide();
        } else {
          if (!isVisible()) {
            show();
            raise();
          }
          reposition();
        }
      }
      break;
    default:
      break;
  }
  return false;
}

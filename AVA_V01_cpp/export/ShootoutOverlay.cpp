#include "ShootoutOverlay.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kMinimumOverlayScale = 0.5;

constexpr qreal kDesignPaddingH = 12.0;
constexpr qreal kDesignPaddingV = 8.0;
constexpr qreal kDesignCornerRadius = 8.0;
constexpr qreal kDesignTitleFont = 11.0;
constexpr qreal kDesignRoundFont = 13.0;
constexpr qreal kDesignNameFont = 12.0;
constexpr qreal kDesignScoreFont = 17.0;
constexpr qreal kDesignDot = 13.0;
constexpr qreal kDesignDotGap = 4.0;
// One empty dot of space, so five regulation marks stay a group when every
// dot is the same shape. A 4px gap reads as ordinary dot spacing.
constexpr qreal kDesignSuddenDeathGap = 16.0;
constexpr qreal kDesignSwatchWidth = 4.0;
constexpr qreal kDesignElementGap = 8.0;
constexpr qreal kDesignRowGap = 5.0;
constexpr qreal kDesignHeaderGap = 6.0;
constexpr qreal kDesignMaxNameWidth = 148.0;
constexpr qreal kDesignMinNameWidth = 28.0;

class OverlayScaler {
public:
    explicit OverlayScaler(qreal factor) : factor_(factor > 0.0 ? factor : 1.0) {}

    int pixels(qreal designPixels) const {
        return qMax(1, qRound(designPixels * factor_));
    }

    qreal points(qreal designPoints) const { return designPoints * factor_; }

private:
    qreal factor_;
};

qreal scaleToFitWidth(qreal startScale, int measuredWidth, int maxWidth) {
    if (maxWidth <= 0 || startScale <= 0.0 || measuredWidth <= maxWidth || measuredWidth <= 0) {
        return startScale;
    }
    return qMax(kMinimumOverlayScale * 0.25,
                startScale * static_cast<qreal>(maxWidth) / static_cast<qreal>(measuredWidth));
}

QString sanitizeSingleLine(QString text) {
    return text.simplified();
}

QColor parseTeamColor(const QString& hex, const QColor& fallback) {
    const QString trimmed = hex.trimmed();
    if (trimmed.isEmpty()) return fallback;
    const QString normalized = trimmed.startsWith(QLatin1Char('#'))
        ? trimmed
        : QLatin1Char('#') + trimmed;
    const QColor parsed(normalized);
    return parsed.isValid() ? parsed : fallback;
}

qreal relativeLuminance(const QColor& color) {
    return (0.2126 * color.red()) + (0.7152 * color.green()) + (0.0722 * color.blue());
}

qreal linearSrgbChannel(int value) {
    const qreal encoded = value / 255.0;
    if (encoded <= 0.04045) return encoded / 12.92;
    return std::pow((encoded + 0.055) / 1.055, 2.4);
}

qreal contrastRatio(const QColor& first, const QColor& second) {
    const qreal firstLuminance = (0.2126 * linearSrgbChannel(first.red()))
        + (0.7152 * linearSrgbChannel(first.green()))
        + (0.0722 * linearSrgbChannel(first.blue()));
    const qreal secondLuminance = (0.2126 * linearSrgbChannel(second.red()))
        + (0.7152 * linearSrgbChannel(second.green()))
        + (0.0722 * linearSrgbChannel(second.blue()));
    const qreal lighter = qMax(firstLuminance, secondLuminance);
    const qreal darker = qMin(firstLuminance, secondLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

QFont overlayFont(const OverlayScaler& scaler, qreal designPoints, QFont::Weight weight) {
    QFont font(QStringLiteral("Helvetica"));
    font.setStyleHint(QFont::SansSerif);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setPointSizeF(scaler.points(designPoints));
    font.setWeight(weight);
    return font;
}

struct DotStrip {
    QVector<ShootoutState::MarkerKind> markers;
    bool hasSuddenDeath = false;
    bool ellipsisBeforeSuddenDeath = false;
};

DotStrip makeDotStrip(QVector<ShootoutState::MarkerKind> source, int droppedOldestSuddenDeath) {
    if (source.size() < ShootoutState::kRegulationRounds) {
        source.resize(ShootoutState::kRegulationRounds);
    }
    const int suddenDeathCount = qMax(0, source.size() - ShootoutState::kRegulationRounds);
    const int maximumDrop = qMax(0, suddenDeathCount - 2);
    const int drop = qBound(0, droppedOldestSuddenDeath, maximumDrop);

    DotStrip strip;
    if (drop <= 0) {
        strip.markers = source;
        strip.hasSuddenDeath = suddenDeathCount > 0;
        return strip;
    }

    strip.markers = source.mid(0, ShootoutState::kRegulationRounds);
    const int tailStart = ShootoutState::kRegulationRounds + drop;
    for (int index = tailStart; index < source.size(); ++index) {
        strip.markers.append(source.at(index));
    }
    strip.hasSuddenDeath = !strip.markers.isEmpty()
        && strip.markers.size() > ShootoutState::kRegulationRounds;
    strip.ellipsisBeforeSuddenDeath = true;
    return strip;
}

int dotStripWidth(const DotStrip& strip,
                  int dotDiameter,
                  int dotGap,
                  int suddenDeathGap,
                  int ellipsisWidth) {
    if (strip.markers.isEmpty()) return 0;
    int width = strip.markers.size() * dotDiameter;
    if (strip.markers.size() > 1) {
        width += (strip.markers.size() - 1) * dotGap;
    }
    if (strip.hasSuddenDeath) width += suddenDeathGap;
    if (strip.ellipsisBeforeSuddenDeath) width += ellipsisWidth + dotGap;
    return width;
}

struct PreparedLayout {
    OverlayScaler scaler;
    QFont titleFont;
    QFont roundFont;
    QFont nameFont;
    QFont scoreFont;
    QString homeName;
    QString awayName;
    QString roundLabel;
    QString homeScoreText;
    QString awayScoreText;
    QColor homeColor;
    QColor awayColor;
    DotStrip homeDots;
    DotStrip awayDots;
    int paddingH = 0;
    int paddingV = 0;
    int cornerRadius = 0;
    int elementGap = 0;
    int rowGap = 0;
    int headerGap = 0;
    int dotDiameter = 0;
    int dotGap = 0;
    int suddenDeathGap = 0;
    int swatchWidth = 0;
    int nameColumnWidth = 0;
    int scoreColumnWidth = 0;
    int dotsColumnWidth = 0;
    int ellipsisWidth = 0;
    int contentWidth = 0;
    int headerHeight = 0;
    int rowHeight = 0;
    int imageWidth = 0;
    int imageHeight = 0;
    int titleWidth = 0;
    int roundLabelWidth = 0;

    explicit PreparedLayout(qreal scale)
        : scaler(scale) {}
};

PreparedLayout measureLayout(const ShootoutOverlayFrame& frame,
                             qreal scale,
                             int nameBudget,
                             int droppedOldestSuddenDeath) {
    PreparedLayout layout(scale);
    layout.paddingH = layout.scaler.pixels(kDesignPaddingH);
    layout.paddingV = layout.scaler.pixels(kDesignPaddingV);
    layout.cornerRadius = layout.scaler.pixels(kDesignCornerRadius);
    layout.elementGap = layout.scaler.pixels(kDesignElementGap);
    layout.rowGap = layout.scaler.pixels(kDesignRowGap);
    layout.headerGap = layout.scaler.pixels(kDesignHeaderGap);
    layout.dotDiameter = layout.scaler.pixels(kDesignDot);
    layout.dotGap = layout.scaler.pixels(kDesignDotGap);
    layout.suddenDeathGap = layout.scaler.pixels(kDesignSuddenDeathGap);
    layout.swatchWidth = layout.scaler.pixels(kDesignSwatchWidth);

    layout.titleFont = overlayFont(layout.scaler, kDesignTitleFont, QFont::DemiBold);
    layout.roundFont = overlayFont(layout.scaler, kDesignRoundFont, QFont::Bold);
    layout.nameFont = overlayFont(layout.scaler, kDesignNameFont, QFont::DemiBold);
    layout.scoreFont = overlayFont(layout.scaler, kDesignScoreFont, QFont::Bold);

    const QFontMetrics titleMetrics(layout.titleFont);
    const QFontMetrics roundMetrics(layout.roundFont);
    const QFontMetrics nameMetrics(layout.nameFont);
    const QFontMetrics scoreMetrics(layout.scoreFont);

    const QString title = QStringLiteral("S.O.");
    layout.roundLabel = sanitizeSingleLine(frame.roundLabel);
    layout.titleWidth = titleMetrics.horizontalAdvance(title);
    layout.roundLabelWidth = layout.roundLabel.isEmpty()
        ? 0
        : roundMetrics.horizontalAdvance(layout.roundLabel);
    layout.ellipsisWidth = nameMetrics.horizontalAdvance(QStringLiteral("\u2026"));

    const int safeNameBudget = qMax(layout.scaler.pixels(kDesignMinNameWidth), nameBudget);
    const QString homeSource = sanitizeSingleLine(frame.homeName);
    const QString awaySource = sanitizeSingleLine(frame.awayName);
    layout.homeName = nameMetrics.elidedText(
        homeSource.isEmpty() ? QStringLiteral("\u2014") : homeSource,
        Qt::ElideRight,
        safeNameBudget);
    layout.awayName = nameMetrics.elidedText(
        awaySource.isEmpty() ? QStringLiteral("\u2014") : awaySource,
        Qt::ElideRight,
        safeNameBudget);
    layout.nameColumnWidth = qMax(nameMetrics.horizontalAdvance(layout.homeName),
                                  nameMetrics.horizontalAdvance(layout.awayName));

    layout.homeScoreText = QString::number(qMax(0, frame.homeConverted));
    layout.awayScoreText = QString::number(qMax(0, frame.awayConverted));
    const int scoreDigits = qMax(2, qMax(layout.homeScoreText.size(), layout.awayScoreText.size()));
    layout.scoreColumnWidth = scoreMetrics.horizontalAdvance(QStringLiteral("8")) * scoreDigits;

    layout.homeDots = makeDotStrip(frame.homeMarkers, droppedOldestSuddenDeath);
    layout.awayDots = makeDotStrip(frame.awayMarkers, droppedOldestSuddenDeath);
    layout.dotsColumnWidth = qMax(
        dotStripWidth(layout.homeDots, layout.dotDiameter, layout.dotGap,
                      layout.suddenDeathGap, layout.ellipsisWidth),
        dotStripWidth(layout.awayDots, layout.dotDiameter, layout.dotGap,
                      layout.suddenDeathGap, layout.ellipsisWidth));

    const int rowBody = layout.swatchWidth + layout.elementGap + layout.nameColumnWidth
        + layout.elementGap + layout.dotsColumnWidth + layout.elementGap + layout.scoreColumnWidth;
    const int headerBody = layout.roundLabelWidth == 0
        ? layout.titleWidth
        : layout.titleWidth + layout.elementGap + layout.roundLabelWidth;
    layout.contentWidth = qMax(rowBody, headerBody);

    layout.headerHeight = qMax(titleMetrics.height(), roundMetrics.height()) + layout.scaler.pixels(2);
    layout.rowHeight = qMax(layout.dotDiameter,
                            qMax(nameMetrics.height(), scoreMetrics.height()) + layout.scaler.pixels(2));

    layout.imageWidth = layout.contentWidth + (2 * layout.paddingH);
    layout.imageHeight = layout.paddingV + layout.headerHeight + layout.headerGap
        + layout.rowHeight + layout.rowGap + layout.rowHeight + layout.paddingV;

    layout.homeColor = parseTeamColor(frame.homeColorHex, QColor(96, 165, 250));
    layout.awayColor = parseTeamColor(frame.awayColorHex, QColor(248, 113, 113));
    return layout;
}

PreparedLayout fitLayout(const ShootoutOverlayFrame& frame, qreal scale, int maxImageWidth) {
    const OverlayScaler scaler(scale);
    int nameBudget = scaler.pixels(kDesignMaxNameWidth);
    const int minimumName = scaler.pixels(kDesignMinNameWidth);
    int droppedOldestSuddenDeath = 0;
    PreparedLayout layout = measureLayout(frame, scale, nameBudget, droppedOldestSuddenDeath);

    for (int step = 0; step < 4 && maxImageWidth > 0 && layout.imageWidth > maxImageWidth; ++step) {
        const int overflow = layout.imageWidth - maxImageWidth;
        const int reduced = qMax(minimumName, layout.nameColumnWidth - overflow);
        if (reduced >= nameBudget) break;
        nameBudget = reduced;
        layout = measureLayout(frame, scale, nameBudget, droppedOldestSuddenDeath);
    }

    const int suddenDeathMarkers = qMax(0, qMax(frame.homeMarkers.size(), frame.awayMarkers.size())
        - ShootoutState::kRegulationRounds);
    const int maximumDrop = qMax(0, suddenDeathMarkers - 2);
    while (maxImageWidth > 0 && layout.imageWidth > maxImageWidth && droppedOldestSuddenDeath < maximumDrop) {
        ++droppedOldestSuddenDeath;
        layout = measureLayout(frame, scale, nameBudget, droppedOldestSuddenDeath);
    }
    return layout;
}

void paintMarker(QPainter& painter,
                 const QRectF& bounds,
                 ShootoutState::MarkerKind kind,
                 const QColor& teamColor) {
    const qreal penWidth = qMax(1.0, bounds.width() / 7.0);
    const bool darkFill = relativeLuminance(teamColor) < 70.0;
    const QColor accent = darkFill ? QColor(255, 255, 255) : teamColor;
    const QColor checkColor = relativeLuminance(teamColor) > 180.0
        ? QColor(15, 23, 42)
        : QColor(255, 255, 255);

    painter.save();
    switch (kind) {
    case ShootoutState::MarkerKind::NotTaken: {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 110), penWidth, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(bounds);
        break;
    }
    case ShootoutState::MarkerKind::InProgress: {
        painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 70));
        painter.setPen(QPen(accent, penWidth * 1.55, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(bounds);
        break;
    }
    case ShootoutState::MarkerKind::Converted: {
        const int rimAlpha = darkFill ? 230 : 50;
        painter.setBrush(teamColor);
        painter.setPen(QPen(QColor(255, 255, 255, rimAlpha), qMax(1.0, penWidth * 0.7)));
        painter.drawEllipse(bounds);
        QPainterPath check;
        const QPointF start(bounds.left() + bounds.width() * 0.28,
                            bounds.top() + bounds.height() * 0.52);
        const QPointF mid(bounds.left() + bounds.width() * 0.44,
                          bounds.top() + bounds.height() * 0.70);
        const QPointF end(bounds.left() + bounds.width() * 0.74,
                          bounds.top() + bounds.height() * 0.32);
        check.moveTo(start);
        check.lineTo(mid);
        check.lineTo(end);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(checkColor, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(check);
        break;
    }
    case ShootoutState::MarkerKind::Missed: {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 150), penWidth, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(bounds);
        painter.setPen(QPen(QColor(254, 202, 202), penWidth, Qt::SolidLine, Qt::RoundCap));
        const QPointF topLeft(bounds.left() + bounds.width() * 0.30,
                              bounds.top() + bounds.height() * 0.30);
        const QPointF bottomRight(bounds.left() + bounds.width() * 0.70,
                                  bounds.top() + bounds.height() * 0.70);
        const QPointF topRight(bounds.left() + bounds.width() * 0.70,
                               bounds.top() + bounds.height() * 0.30);
        const QPointF bottomLeft(bounds.left() + bounds.width() * 0.30,
                                 bounds.top() + bounds.height() * 0.70);
        painter.drawLine(topLeft, bottomRight);
        painter.drawLine(topRight, bottomLeft);
        break;
    }
    case ShootoutState::MarkerKind::Unknown: {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 150), penWidth, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(bounds);
        const qreal dashY = bounds.center().y();
        painter.drawLine(QPointF(bounds.left() + bounds.width() * 0.30, dashY),
                         QPointF(bounds.right() - bounds.width() * 0.30, dashY));
        break;
    }
    }
    painter.restore();
}

int paintDotStrip(QPainter& painter,
                  int originX,
                  int rowTop,
                  int rowHeight,
                  const DotStrip& strip,
                  const QColor& teamColor,
                  const PreparedLayout& layout) {
    int cursorX = originX;
    const int regulationCount = qMin(ShootoutState::kRegulationRounds, strip.markers.size());
    const int dotTop = rowTop + qMax(0, (rowHeight - layout.dotDiameter) / 2);

    auto paintAt = [&painter, &teamColor, &layout, dotTop](int x, ShootoutState::MarkerKind kind) {
        const QRectF bounds(x, dotTop, layout.dotDiameter, layout.dotDiameter);
        paintMarker(painter, bounds, kind, teamColor);
    };

    for (int index = 0; index < regulationCount; ++index) {
        paintAt(cursorX, strip.markers.at(index));
        cursorX += layout.dotDiameter;
        const bool moreAfter = index + 1 < strip.markers.size() || strip.ellipsisBeforeSuddenDeath;
        if (moreAfter) cursorX += layout.dotGap;
    }
    if (strip.hasSuddenDeath) cursorX += layout.suddenDeathGap;
    if (strip.ellipsisBeforeSuddenDeath) {
        painter.setFont(layout.nameFont);
        painter.setPen(QColor(255, 255, 255, 170));
        painter.drawText(QRect(cursorX, rowTop, layout.ellipsisWidth, rowHeight),
                         Qt::AlignCenter,
                         QStringLiteral("\u2026"));
        cursorX += layout.ellipsisWidth + layout.dotGap;
    }
    for (int index = regulationCount; index < strip.markers.size(); ++index) {
        paintAt(cursorX, strip.markers.at(index));
        cursorX += layout.dotDiameter;
        if (index + 1 < strip.markers.size()) cursorX += layout.dotGap;
    }
    return cursorX;
}

void paintTeamRow(QPainter& painter,
                  int contentLeft,
                  int rowTop,
                  const QString& name,
                  const QString& scoreText,
                  const QColor& teamColor,
                  const DotStrip& dots,
                  const PreparedLayout& layout) {
    const int centerY = rowTop + (layout.rowHeight / 2);
    const int swatchHeight = qMin(layout.rowHeight - 2, layout.scaler.pixels(16));
    const QRectF swatchRect(contentLeft,
                            centerY - (swatchHeight / 2),
                            layout.swatchWidth,
                            swatchHeight);
    const qreal swatchRadius = layout.scaler.pixels(2);
    painter.setPen(Qt::NoPen);
    // Black and navy kits match the plate and disappear. A light halo behind
    // the swatch keeps the chosen color visible.
    if (contrastRatio(teamColor, QColor(15, 23, 42)) < 3.0) {
        const qreal outset = qMax(1.0, static_cast<qreal>(layout.scaler.pixels(1)));
        painter.setBrush(QColor(255, 255, 255, 220));
        painter.drawRoundedRect(swatchRect.adjusted(-outset, -outset, outset, outset),
                                swatchRadius + outset,
                                swatchRadius + outset);
    }
    painter.setBrush(teamColor);
    painter.drawRoundedRect(swatchRect, swatchRadius, swatchRadius);

    const int nameLeft = contentLeft + layout.swatchWidth + layout.elementGap;
    painter.setFont(layout.nameFont);
    painter.setPen(QColor(255, 255, 255, 230));
    painter.drawText(QRect(nameLeft, rowTop, layout.nameColumnWidth, layout.rowHeight),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     name);

    const int dotsLeft = nameLeft + layout.nameColumnWidth + layout.elementGap;
    paintDotStrip(painter, dotsLeft, rowTop, layout.rowHeight, dots, teamColor, layout);

    const int scoreLeft = contentLeft + layout.contentWidth - layout.scoreColumnWidth;
    painter.setFont(layout.scoreFont);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(QRect(scoreLeft, rowTop, layout.scoreColumnWidth, layout.rowHeight),
                     Qt::AlignRight | Qt::AlignVCenter,
                     scoreText);
}

}  // namespace

namespace ShootoutOverlay {

QImage render(const ShootoutOverlayFrame& frame, qreal overlayScale, int maxImageWidth) {
    qreal scale = overlayScale > 0.0 ? overlayScale : 1.0;
    PreparedLayout layout = fitLayout(frame, scale, maxImageWidth);
    if (maxImageWidth > 0 && layout.imageWidth > maxImageWidth) {
        scale = scaleToFitWidth(scale, layout.imageWidth, maxImageWidth);
        layout = fitLayout(frame, scale, maxImageWidth);
    }
    if (maxImageWidth > 0 && layout.imageWidth > maxImageWidth) {
        scale = scaleToFitWidth(scale, layout.imageWidth, maxImageWidth);
        layout = fitLayout(frame, scale, maxImageWidth);
    }
    if (layout.imageWidth <= 0 || layout.imageHeight <= 0) return {};

    QImage image(layout.imageWidth, layout.imageHeight, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 23, 42, 188));
    painter.drawRoundedRect(image.rect(), layout.cornerRadius, layout.cornerRadius);

    const int contentLeft = layout.paddingH;
    const int contentTop = layout.paddingV;
    const QString title = QStringLiteral("S.O.");

    painter.setFont(layout.titleFont);
    painter.setPen(QColor(255, 255, 255, 170));
    painter.drawText(QRect(contentLeft, contentTop, layout.titleWidth, layout.headerHeight),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     title);
    if (!layout.roundLabel.isEmpty()) {
        painter.setFont(layout.roundFont);
        painter.setPen(QColor(255, 255, 255));
        const int labelLeft = contentLeft + layout.contentWidth - layout.roundLabelWidth;
        painter.drawText(QRect(labelLeft, contentTop, layout.roundLabelWidth, layout.headerHeight),
                         Qt::AlignRight | Qt::AlignVCenter,
                         layout.roundLabel);
    }

    const int firstRowTop = contentTop + layout.headerHeight + layout.headerGap;
    const int secondRowTop = firstRowTop + layout.rowHeight + layout.rowGap;
    paintTeamRow(painter, contentLeft, firstRowTop, layout.homeName, layout.homeScoreText,
                 layout.homeColor, layout.homeDots, layout);
    paintTeamRow(painter, contentLeft, secondRowTop, layout.awayName, layout.awayScoreText,
                 layout.awayColor, layout.awayDots, layout);
    painter.end();
    return image;
}

QString writeImage(const ShootoutOverlayFrame& frame,
                   const QString& outputPath,
                   qreal overlayScale,
                   int maxImageWidth) {
    const QImage image = render(frame, overlayScale, maxImageWidth);
    if (image.isNull()) return {};
    if (!image.save(outputPath, "PNG")) return {};
    return outputPath;
}

QVector<TimedShootoutOverlay> framesForClip(const ShootoutState& state,
                                            const ShootoutState::ClipWindow& clip,
                                            AppLocale::Language language,
                                            const QString& homeName,
                                            const QString& awayName,
                                            const QString& homeColorHex,
                                            const QString& awayColorHex) {
    const QVector<ShootoutState::TimedSnapshot> phases = state.phasesForClip(clip, language);
    QVector<TimedShootoutOverlay> overlays;
    overlays.reserve(phases.size());
    for (const ShootoutState::TimedSnapshot& phase : phases) {
        TimedShootoutOverlay timed;
        timed.activationOffsetSeconds = phase.activationOffsetSeconds;
        timed.frame.homeName = homeName;
        timed.frame.awayName = awayName;
        timed.frame.homeColorHex = homeColorHex;
        timed.frame.awayColorHex = awayColorHex;
        timed.frame.roundLabel = phase.snapshot.roundLabel;
        timed.frame.homeConverted = phase.snapshot.homeConverted;
        timed.frame.awayConverted = phase.snapshot.awayConverted;
        timed.frame.homeMarkers = phase.snapshot.homeMarkers;
        timed.frame.awayMarkers = phase.snapshot.awayMarkers;
        overlays.append(timed);
    }
    return overlays;
}

}  // namespace ShootoutOverlay

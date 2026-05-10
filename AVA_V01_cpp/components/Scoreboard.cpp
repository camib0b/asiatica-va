#include "Scoreboard.h"
#include "../state/TagSession.h"
#include "../state/EventDefaults.h"
#include "../style/StyleProps.h"

#include <algorithm>

#include <QLabel>
#include <QHBoxLayout>
#include <QFont>
#include <QVBoxLayout>

Scoreboard::Scoreboard(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("Scoreboard"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
}

void Scoreboard::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 10, 14, 10);
    rootLayout->setSpacing(2);

    auto* scoreRowLayout = new QHBoxLayout();
    scoreRowLayout->setContentsMargins(0, 0, 0, 0);
    scoreRowLayout->setSpacing(0);

    homeColorSwatch_ = new QWidget(this);
    homeColorSwatch_->setFixedSize(4, 28);
    homeColorSwatch_->setStyleSheet(QStringLiteral("background: #60A5FA; border-radius: 2px;"));

    homeTeamNameLabel_ = new QLabel(QStringLiteral("Home"), this);
    Style::setRole(homeTeamNameLabel_, "scoreboardTeamName");
    homeTeamNameLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    homeScoreLabel_ = new QLabel(QStringLiteral("0"), this);
    Style::setRole(homeScoreLabel_, "scoreboardScore");
    homeScoreLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    homeScoreLabel_->setMinimumWidth(24);

    separatorLabel_ = new QLabel(QStringLiteral("\u2014"), this);
    Style::setRole(separatorLabel_, "scoreboardSeparator");
    separatorLabel_->setAlignment(Qt::AlignCenter);

    awayScoreLabel_ = new QLabel(QStringLiteral("0"), this);
    Style::setRole(awayScoreLabel_, "scoreboardScore");
    awayScoreLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    awayScoreLabel_->setMinimumWidth(24);

    awayTeamNameLabel_ = new QLabel(QStringLiteral("Away"), this);
    Style::setRole(awayTeamNameLabel_, "scoreboardTeamName");
    awayTeamNameLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    awayColorSwatch_ = new QWidget(this);
    awayColorSwatch_->setFixedSize(4, 28);
    awayColorSwatch_->setStyleSheet(QStringLiteral("background: #F87171; border-radius: 2px;"));

    scoreRowLayout->addWidget(homeColorSwatch_);
    scoreRowLayout->addSpacing(10);
    scoreRowLayout->addWidget(homeTeamNameLabel_);
    scoreRowLayout->addStretch(1);
    scoreRowLayout->addWidget(homeScoreLabel_);
    scoreRowLayout->addSpacing(10);
    scoreRowLayout->addWidget(separatorLabel_);
    scoreRowLayout->addSpacing(10);
    scoreRowLayout->addWidget(awayScoreLabel_);
    scoreRowLayout->addStretch(1);
    scoreRowLayout->addWidget(awayTeamNameLabel_);
    scoreRowLayout->addSpacing(10);
    scoreRowLayout->addWidget(awayColorSwatch_);

    currentPeriodLabel_ = new QLabel(this);
    currentPeriodLabel_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    QFont periodFont = currentPeriodLabel_->font();
    periodFont.setPointSizeF(std::max(8.0, periodFont.pointSizeF() - 2.0));
    currentPeriodLabel_->setFont(periodFont);
    currentPeriodLabel_->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.72);"));

    rootLayout->addLayout(scoreRowLayout);
    rootLayout->addWidget(currentPeriodLabel_);
}

void Scoreboard::setTagSession(TagSession* session) {
    if (tagSession_ != session) {
        if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);
        tagSession_ = session;
        if (tagSession_) {
            connect(tagSession_, &TagSession::statsChanged, this, [this]() {
                rebuildGoalTimeline();
                updateScores();
                updateCurrentPeriodIndicator();
            });
        }
    }
    updateTeamDisplay();
    rebuildGoalTimeline();
    updateScores();
    updateCurrentPeriodIndicator();
}

void Scoreboard::setCurrentTimestampMs(qint64 positionMs) {
    if (currentTimestampMs_ == positionMs) return;
    currentTimestampMs_ = positionMs;
    updateScores();
    updateCurrentPeriodIndicator();
}

void Scoreboard::rebuildGoalTimeline() {
    homeGoalTimesMs_.clear();
    awayGoalTimesMs_.clear();
    if (!tagSession_) return;
    for (const auto& tag : tagSession_->tags()) {
        if (tag.mainEvent != QStringLiteral("Goal")) continue;
        if (tag.team == QStringLiteral("Home")) {
            homeGoalTimesMs_.append(tag.positionMs);
        } else if (tag.team == QStringLiteral("Away")) {
            awayGoalTimesMs_.append(tag.positionMs);
        }
    }
    std::sort(homeGoalTimesMs_.begin(), homeGoalTimesMs_.end());
    std::sort(awayGoalTimesMs_.begin(), awayGoalTimesMs_.end());
}

int Scoreboard::countGoalsAtOrBefore(const QVector<qint64>& sortedGoalTimesMs,
                                     qint64 positionMs) const {
    if (sortedGoalTimesMs.isEmpty()) return 0;
    const auto it = std::upper_bound(sortedGoalTimesMs.begin(), sortedGoalTimesMs.end(), positionMs);
    return static_cast<int>(it - sortedGoalTimesMs.begin());
}

void Scoreboard::updateScores() {
    const int homeGoals = countGoalsAtOrBefore(homeGoalTimesMs_, currentTimestampMs_);
    const int awayGoals = countGoalsAtOrBefore(awayGoalTimesMs_, currentTimestampMs_);
    if (homeScoreLabel_) homeScoreLabel_->setText(QString::number(homeGoals));
    if (awayScoreLabel_) awayScoreLabel_->setText(QString::number(awayGoals));
}

QString Scoreboard::currentPeriodForTimestamp(qint64 positionMs) const {
    if (!tagSession_) return QString();

    struct QuarterSpan {
        QString label;
        qint64 startMs = 0;
        qint64 endMs = 0;
    };

    QVector<QuarterSpan> closedQuarterSpans;
    closedQuarterSpans.reserve(4);
    for (const auto& tag : tagSession_->tags()) {
        const bool isQuarterTag =
            tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kQuarter1) ||
            tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kQuarter2) ||
            tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kQuarter3) ||
            tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kQuarter4);
        if (!isQuarterTag) continue;
        closedQuarterSpans.append({tag.mainEvent, tag.startMs, tag.endMs});
    }

    std::sort(closedQuarterSpans.begin(), closedQuarterSpans.end(),
              [](const QuarterSpan& lhs, const QuarterSpan& rhs) {
                  if (lhs.startMs == rhs.startMs) return lhs.endMs < rhs.endMs;
                  return lhs.startMs < rhs.startMs;
              });

    for (const QuarterSpan& quarterSpan : closedQuarterSpans) {
        if (positionMs >= quarterSpan.startMs && positionMs <= quarterSpan.endMs) {
            return quarterSpan.label;
        }
    }

    if (tagSession_->quarterPhase() == TagSession::QuarterPhase::QuarterInProgress) {
        const int quarterIndex = tagSession_->currentQuarterIndex();
        if (quarterIndex >= 0 && quarterIndex < 4 &&
            positionMs >= tagSession_->currentQuarterStartMs()) {
            static const char* kQuarterLabels[4] = {"Q1", "Q2", "Q3", "Q4"};
            return QString::fromLatin1(kQuarterLabels[quarterIndex]);
        }
    }

    return QString();
}

void Scoreboard::updateCurrentPeriodIndicator() {
    if (!currentPeriodLabel_) return;
    const QString periodLabel = currentPeriodForTimestamp(currentTimestampMs_);
    currentPeriodLabel_->setText(periodLabel);
    currentPeriodLabel_->setVisible(!periodLabel.isEmpty());
}

void Scoreboard::updateTeamDisplay() {
    const QString homeName = tagSession_ ? tagSession_->homeTeamName() : QString();
    const QString awayName = tagSession_ ? tagSession_->awayTeamName() : QString();

    if (homeTeamNameLabel_)
        homeTeamNameLabel_->setText(homeName.isEmpty() ? QStringLiteral("Home") : homeName);
    if (awayTeamNameLabel_)
        awayTeamNameLabel_->setText(awayName.isEmpty() ? QStringLiteral("Away") : awayName);

    auto applySwatchColor = [](QWidget* swatch, const QString& colorHex, const QString& fallback) {
        if (!swatch) return;
        QString hex = colorHex.trimmed();
        if (!hex.isEmpty() && !hex.startsWith(QLatin1Char('#'))) hex.prepend(QLatin1Char('#'));
        if (hex.isEmpty() || !QColor(hex).isValid()) hex = fallback;
        swatch->setStyleSheet(
            QStringLiteral("background: %1; border-radius: 2px;").arg(hex));
    };

    const QString homeColor = tagSession_ ? tagSession_->homeTeamColor() : QString();
    const QString awayColor = tagSession_ ? tagSession_->awayTeamColor() : QString();
    applySwatchColor(homeColorSwatch_, homeColor, QStringLiteral("#60A5FA"));
    applySwatchColor(awayColorSwatch_, awayColor, QStringLiteral("#F87171"));
}

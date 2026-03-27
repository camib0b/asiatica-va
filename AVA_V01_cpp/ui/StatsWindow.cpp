#include "StatsWindow.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QButtonGroup>
#include <QToolButton>
#include <algorithm>
#include <cmath>

namespace {
QString formatCountAndPercent(int count, int mainCount) {
    const double pct = (mainCount > 0) ? (100.0 * double(count) / double(mainCount)) : 0.0;
    const QString pctStr = (std::abs(pct - std::round(pct)) < 1e-9)
        ? QString::number(static_cast<int>(std::round(pct)))
        : QString::number(pct, 'f', 1);
    return QString("%1 (%2%)").arg(count).arg(pctStr);
}
} // namespace

StatsWindow::StatsWindow(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
}

void StatsWindow::applyUiStrings() {
    if (headerLabel_) headerLabel_->setText(AppLocale::trUi("stats.header"));
    if (tree_) {
        tree_->setHeaderLabels({AppLocale::trUi("stats.col_event"), AppLocale::trUi("stats.col_count")});
    }
    updateTeamFilterButtonLabels();
    rebuildTree();
}

void StatsWindow::setTagSession(TagSession* session) {
    if (tagSession_ == session) {
        updateTeamFilterButtonLabels();
        if (tagSession_) {
            rebuildTree();
        } else {
            clearTree();
        }
        return;
    }
    if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);

    tagSession_ = session;
    clearTree();

    updateTeamFilterButtonLabels();

    if (!tagSession_) return;

    rebuildTree();

    connect(tagSession_, &TagSession::cleared, this, [this]() {
        clearTree();
    });

    connect(tagSession_, &TagSession::statsChanged, this, [this]() {
        rebuildTree();
    });
}

void StatsWindow::buildUi() {
    setObjectName("StatsPanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    headerLabel_ = new QLabel(this);
    headerLabel_->setWordWrap(true);
    Style::setRole(headerLabel_, "h3");

    teamFilterRow_ = new QWidget(this);
    teamFilterRow_->setObjectName(QStringLiteral("StatsTeamFilterRow"));
    auto* teamFilterLayout = new QHBoxLayout(teamFilterRow_);
    teamFilterLayout->setContentsMargins(0, 0, 0, 0);
    teamFilterLayout->setSpacing(4);

    teamFilterGroup_ = new QButtonGroup(this);
    teamFilterGroup_->setExclusive(true);

    teamFilterHomeBtn_ = new QToolButton(teamFilterRow_);
    teamFilterAwayBtn_ = new QToolButton(teamFilterRow_);
    teamFilterBothBtn_ = new QToolButton(teamFilterRow_);
    for (QToolButton* button : {teamFilterHomeBtn_, teamFilterAwayBtn_, teamFilterBothBtn_}) {
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        Style::setVariant(button, "ghost");
        Style::setSize(button, "sm");
    }

    teamFilterGroup_->addButton(teamFilterHomeBtn_, static_cast<int>(TeamStatsFilter::Home));
    teamFilterGroup_->addButton(teamFilterAwayBtn_, static_cast<int>(TeamStatsFilter::Away));
    teamFilterGroup_->addButton(teamFilterBothBtn_, static_cast<int>(TeamStatsFilter::Both));
    teamFilterBothBtn_->setChecked(true);

    teamFilterLayout->addWidget(teamFilterHomeBtn_, 0);
    teamFilterLayout->addWidget(teamFilterAwayBtn_, 0);
    teamFilterLayout->addWidget(teamFilterBothBtn_, 0);
    teamFilterLayout->addStretch(1);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({AppLocale::trUi("stats.col_event"), AppLocale::trUi("stats.col_count")});
    tree_->setRootIsDecorated(true);
    tree_->setAlternatingRowColors(true);
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    tree_->header()->resizeSection(1, 100);

    headerLabel_->setText(AppLocale::trUi("stats.header"));
    layout->addWidget(headerLabel_);
    layout->addWidget(teamFilterRow_);
    layout->addWidget(tree_, /*stretch=*/1);

    updateTeamFilterButtonLabels();
}

void StatsWindow::wireSignals() {
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, &StatsWindow::onTreeItemDoubleClicked);
    if (teamFilterGroup_) {
        connect(teamFilterGroup_, &QButtonGroup::idClicked, this, [this](int) {
            rebuildTree();
        });
    }
}

void StatsWindow::updateTeamFilterButtonLabels() {
    if (!teamFilterHomeBtn_ || !teamFilterAwayBtn_ || !teamFilterBothBtn_) return;
    teamFilterBothBtn_->setText(AppLocale::trUi("stats.filter_both"));
    if (!tagSession_) {
        teamFilterHomeBtn_->setText(AppLocale::trUi("stats.filter_home_fallback"));
        teamFilterAwayBtn_->setText(AppLocale::trUi("stats.filter_away_fallback"));
        return;
    }
    const QString home = tagSession_->homeTeamName().trimmed();
    const QString away = tagSession_->awayTeamName().trimmed();
    teamFilterHomeBtn_->setText(home.isEmpty() ? AppLocale::trUi("stats.filter_home_fallback") : home);
    teamFilterAwayBtn_->setText(away.isEmpty() ? AppLocale::trUi("stats.filter_away_fallback") : away);
}

StatsWindow::TeamStatsFilter StatsWindow::currentTeamFilter() const {
    if (!teamFilterGroup_) return TeamStatsFilter::Both;
    const int id = teamFilterGroup_->checkedId();
    if (id < 0) return TeamStatsFilter::Both;
    return static_cast<TeamStatsFilter>(id);
}

bool StatsWindow::tagMatchesTeamFilter(const TagSession::GameTag& tag, TeamStatsFilter filter) const {
    if (filter == TeamStatsFilter::Both) return true;
    if (filter == TeamStatsFilter::Home) {
        return tag.team.compare(QStringLiteral("Home"), Qt::CaseInsensitive) == 0;
    }
    if (filter == TeamStatsFilter::Away) {
        return tag.team.compare(QStringLiteral("Away"), Qt::CaseInsensitive) == 0;
    }
    return false;
}

void StatsWindow::onTreeItemDoubleClicked(QTreeWidgetItem* item, int /*column*/) {
    if (!item) return;
    const QString mainEvent = item->data(0, Qt::UserRole).toString();
    const QString followUpEvent = item->data(0, Qt::UserRole + 1).toString();
    if (!mainEvent.isEmpty()) {
        emit filterByPathRequested(mainEvent, followUpEvent);
    }
}

void StatsWindow::clearTree() {
    if (!tree_) return;
    tree_->clear();
}

void StatsWindow::rebuildTree() {
    if (!tree_) return;
    clearTree();
    if (!tagSession_) return;

    const TeamStatsFilter filter = currentTeamFilter();
    QHash<QString, int> mainCounts;
    QHash<QString, QHash<QString, int>> followUpCounts;

    for (const TagSession::GameTag& tag : tagSession_->tags()) {
        if (!tagMatchesTeamFilter(tag, filter)) continue;
        mainCounts[tag.mainEvent] = mainCounts.value(tag.mainEvent, 0) + 1;
        if (!tag.followUpEvent.isEmpty()) {
            auto& inner = followUpCounts[tag.mainEvent];
            inner[tag.followUpEvent] = inner.value(tag.followUpEvent, 0) + 1;
        }
    }

    QStringList mains = mainCounts.keys();
    std::sort(mains.begin(), mains.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    const QString homeName = tagSession_->homeTeamName();
    const QString awayName = tagSession_->awayTeamName();

    for (const QString& mainEvent : mains) {
        const int mainCount = mainCounts.value(mainEvent, 0);

        auto* mainItem = new QTreeWidgetItem(tree_);
        mainItem->setText(0, AppLocale::trEvent(mainEvent));
        mainItem->setText(1, QString::number(mainCount));
        mainItem->setData(0, Qt::UserRole, mainEvent);
        mainItem->setData(0, Qt::UserRole + 1, QString());

        const auto followUpsIt = followUpCounts.find(mainEvent);
        if (followUpsIt == followUpCounts.end()) continue;

        QStringList followUps = followUpsIt.value().keys();
        std::sort(followUps.begin(), followUps.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });

        for (const QString& followUp : followUps) {
            const int followUpCount = followUpsIt.value().value(followUp, 0);
            const QString stripped =
                AppLocale::followUpPathWithoutTeamSegments(followUp, homeName, awayName);
            if (stripped.isEmpty()) continue;

            auto* child = new QTreeWidgetItem(mainItem);
            child->setText(0, QStringLiteral("  ") + AppLocale::translateCompoundPath(stripped));
            child->setText(1, formatCountAndPercent(followUpCount, mainCount));
            child->setData(0, Qt::UserRole, mainEvent);
            child->setData(0, Qt::UserRole + 1, followUp);
        }

        mainItem->setExpanded(true);
    }
}

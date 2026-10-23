#include "StatsWindow.h"
#include "../style/StyleProps.h"
#include "../style/WorkDataView.h"
#include "../i18n/AppLocale.h"
#include "../state/EventLabelResolver.h"
#include "../state/TagEvent.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QButtonGroup>
#include <QToolButton>
#include <algorithm>

namespace {
QString formatCountAndPercent(int count, int mainCount) {
    if (mainCount <= 0) {
        return QStringLiteral("%1 (0%)").arg(count);
    }
    if ((count * 100) % mainCount == 0) {
        return QStringLiteral("%1 (%2%)").arg(count).arg((count * 100) / mainCount);
    }
    const int roundedTenths = (count * 1000 + mainCount / 2) / mainCount;
    return QStringLiteral("%1 (%2.%3%)")
        .arg(count)
        .arg(roundedTenths / 10)
        .arg(roundedTenths % 10);
}
} // namespace

StatsWindow::StatsWindow(QWidget* parent)
    : QWidget(parent),
      tagSession_(nullptr) {
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
}

StatsWindow::~StatsWindow() {
    if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);
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
    if (tagSession_.data() == session) {
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

    connect(tagSession_, &TagSession::cleared, this, &StatsWindow::onSessionCleared);
    connect(tagSession_, &TagSession::tagsChanged, this, &StatsWindow::onSessionTagsChanged);
    connect(tagSession_, &TagSession::gameMetadataChanged, this,
            &StatsWindow::onSessionGameMetadataChanged);
    connect(tagSession_, &TagSession::eventLabelOverridesChanged, this,
            &StatsWindow::onEventLabelOverridesChanged);
}

void StatsWindow::onSessionCleared() { clearTree(); }

void StatsWindow::onSessionTagsChanged() { rebuildTree(); }

void StatsWindow::onSessionGameMetadataChanged() {
    updateTeamFilterButtonLabels();
    rebuildTree();
}

void StatsWindow::onEventLabelOverridesChanged() { rebuildTree(); }

void StatsWindow::onTeamFilterClicked(int /*id*/) { rebuildTree(); }

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
    tree_->setObjectName(QStringLiteral("StatsTree"));
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({AppLocale::trUi("stats.col_event"), AppLocale::trUi("stats.col_count")});
    tree_->setRootIsDecorated(true);
    Style::configureWorkDataView(tree_);
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
        connect(teamFilterGroup_, &QButtonGroup::idClicked, this, &StatsWindow::onTeamFilterClicked);
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

    const TagSession::TeamSide side = TagSession::teamSideFromKey(tag.team);
    if (filter == TeamStatsFilter::Home) return side == TagSession::TeamSide::Home;
    if (filter == TeamStatsFilter::Away) return side == TagSession::TeamSide::Away;
    return false;
}

void StatsWindow::onTreeItemDoubleClicked(QTreeWidgetItem* item, int /*column*/) {
    if (!item) return;
    const TagEvent::MainEventKey mainEvent =
        item->data(0, Qt::UserRole).value<TagEvent::MainEventKey>();
    if (mainEvent.isEmpty()) return;
    const TagEvent::FollowUpChain followUps =
        item->data(0, Qt::UserRole + 1).value<TagEvent::FollowUpChain>();
    emit filterByEventPathRequested(mainEvent, followUps);
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
    QHash<TagEvent::MainEventKey, int> mainCounts;
    QHash<TagEvent::MainEventKey, QHash<TagEvent::FollowUpChain, int>> followUpCounts;

    for (const TagSession::GameTag& tag : tagSession_->tags()) {
        if (!tagMatchesTeamFilter(tag, filter)) continue;
        const TagEvent::MainEventKey mainEvent = tag.mainEventKey();
        mainCounts[mainEvent] = mainCounts.value(mainEvent, 0) + 1;
        const TagEvent::FollowUpChain chain = tag.followUpChain();
        if (!chain.empty()) {
            auto& inner = followUpCounts[mainEvent];
            inner[chain] = inner.value(chain, 0) + 1;
        }
    }

    const QList<TagEvent::MainEventKey> mains =
        TagEvent::sortedByCanonicalName(mainCounts.keys());

    for (const TagEvent::MainEventKey& mainEvent : mains) {
        const int mainCount = mainCounts.value(mainEvent, 0);

        auto* mainItem = new QTreeWidgetItem(tree_);
        mainItem->setText(0, EventLabelResolver::labelForKey(mainEvent, tagSession_));
        mainItem->setText(1, QString::number(mainCount));
        mainItem->setData(0, Qt::UserRole, QVariant::fromValue(mainEvent));
        mainItem->setData(0, Qt::UserRole + 1, QVariant::fromValue(TagEvent::FollowUpChain{}));

        const auto followUpsIt = followUpCounts.find(mainEvent);
        if (followUpsIt == followUpCounts.end()) continue;

        QList<TagEvent::FollowUpChain> followUps = followUpsIt.value().keys();
        std::sort(followUps.begin(), followUps.end(),
                  [](const TagEvent::FollowUpChain& left, const TagEvent::FollowUpChain& right) {
                      return TagEvent::canonicalFollowUpPath(left).compare(
                                 TagEvent::canonicalFollowUpPath(right), Qt::CaseInsensitive) < 0;
                  });

        for (const TagEvent::FollowUpChain& followUp : followUps) {
            const int followUpCount = followUpsIt.value().value(followUp, 0);
            const QString followUpLabel =
                mainEvent.id == EventCatalog::EventId::None
                    ? EventLabelResolver::followUpPath(followUp, tagSession_)
                    : EventLabelResolver::followUpPath(mainEvent.id, followUp, tagSession_);
            if (followUpLabel.isEmpty()) continue;

            auto* child = new QTreeWidgetItem(mainItem);
            child->setText(0, QStringLiteral("  ") + followUpLabel);
            child->setText(1, formatCountAndPercent(followUpCount, mainCount));
            child->setData(0, Qt::UserRole, QVariant::fromValue(mainEvent));
            child->setData(0, Qt::UserRole + 1, QVariant::fromValue(followUp));
        }

        mainItem->setExpanded(true);
    }
}

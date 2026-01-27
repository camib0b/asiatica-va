#include "StatsWindow.h"
#include "../style/StyleProps.h"
#include "../state/TagSession.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QHeaderView>
#include <algorithm>

namespace {
QString formatCountAndPercent(int count, int mainCount) {
    const double pct = (mainCount > 0) ? (100.0 * double(count) / double(mainCount)) : 0.0;
    return QString("%1 (%2%)").arg(count).arg(pct, 0, 'f', 1);
}
} // namespace

StatsWindow::StatsWindow(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
}

void StatsWindow::setTagSession(TagSession* session) {
    if (tagSession_ == session) return;
    if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);

    tagSession_ = session;
    clearTree();

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

    // header:
    headerLabel_ = new QLabel("Stats", this);
    headerLabel_->setWordWrap(true);
    Style::setRole(headerLabel_, "h3");

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({"Event", "Count"});
    tree_->setRootIsDecorated(true);
    tree_->setAlternatingRowColors(true);
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    layout->addWidget(headerLabel_);
    layout->addWidget(tree_, /*stretch=*/1);
}

void StatsWindow::wireSignals() {
}

void StatsWindow::clearTree() {
    if (!tree_) return;
    tree_->clear();
}

void StatsWindow::rebuildTree() {
    if (!tree_) return;
    clearTree();
    if (!tagSession_) return;

    QStringList mains = tagSession_->mainEventCounts().keys();
    std::sort(mains.begin(), mains.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    for (const QString& mainEvent : mains) {
        const int mainCount = tagSession_->mainEventCounts().value(mainEvent, 0);

        auto* mainItem = new QTreeWidgetItem(tree_);
        mainItem->setText(0, mainEvent);
        mainItem->setText(1, QString::number(mainCount));

        const auto followUpsIt = tagSession_->followUpCountsByMainEvent().find(mainEvent);
        if (followUpsIt == tagSession_->followUpCountsByMainEvent().end()) continue;

        QStringList followUps = followUpsIt.value().keys();
        std::sort(followUps.begin(), followUps.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });

        for (const QString& followUp : followUps) {
            const int followUpCount = followUpsIt.value().value(followUp, 0);
            auto* child = new QTreeWidgetItem(mainItem);
            child->setText(0, "  " + followUp);
            child->setText(1, formatCountAndPercent(followUpCount, mainCount));
        }

        mainItem->setExpanded(true);
    }
}
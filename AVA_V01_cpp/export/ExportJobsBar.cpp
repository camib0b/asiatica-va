#include "ExportJobsBar.h"

#include "AppLocale.h"
#include "Motion.h"
#include "QtPtr.h"
#include "StyleProps.h"
#include "ThemeColors.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>

namespace {

void clearLayoutItems(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        } else if (QLayout* childLayout = item->layout()) {
            clearLayoutItems(childLayout);
            delete childLayout;
        }
        delete item;
    }
}

}  // namespace

class ExportJobRow final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor washColor READ washColor WRITE setWashColor)

public:
    explicit ExportJobRow(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("ExportJobRow"));
        setAttribute(Qt::WA_StyledBackground, true);
        washColor_ = Style::ThemeColors::transparent();
    }

    QColor washColor() const { return washColor_; }

    void setWashColor(const QColor& color) {
        if (washColor_ == color) return;
        washColor_ = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);
        if (washColor_.alpha() <= 0) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(washColor_);
        painter.drawRoundedRect(QRectF(rect()), 6.0, 6.0);
    }

private:
    QColor washColor_;
};

class ExportCheckMark final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal markScale READ markScale WRITE setMarkScale)
    Q_PROPERTY(qreal markOpacity READ markOpacity WRITE setMarkOpacity)

public:
    explicit ExportCheckMark(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(22, 22);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    qreal markScale() const { return markScale_; }
    qreal markOpacity() const { return markOpacity_; }

    void setMarkScale(qreal scale) {
        if (qAbs(markScale_ - scale) < 0.001) return;
        markScale_ = scale;
        update();
    }

    void setMarkOpacity(qreal opacity) {
        if (qAbs(markOpacity_ - opacity) < 0.001) return;
        markOpacity_ = opacity;
        update();
    }

    QSize sizeHint() const override { return QSize(22, 22); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setOpacity(markOpacity_);
        const QPointF center = QRectF(rect()).center();
        painter.translate(center);
        painter.scale(markScale_, markScale_);
        painter.translate(-center);

        QPen pen(Style::ThemeColors::success(), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        const QRectF box = QRectF(rect()).adjusted(5.0, 5.0, -5.0, -5.0);
        QPainterPath path;
        path.moveTo(box.left(), box.center().y());
        path.lineTo(box.left() + box.width() * 0.38, box.bottom());
        path.lineTo(box.right(), box.top());
        painter.drawPath(path);
    }

private:
    qreal markScale_ = 1.0;
    qreal markOpacity_ = 1.0;
};

ExportJobsBar::ExportJobsBar(ExportJobManager* manager, QWidget* parent)
    : QWidget(parent)
    , manager_(manager) {
    setObjectName(QStringLiteral("ExportJobsBar"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 8, 12, 8);
    rootLayout->setSpacing(6);

    rowsLayout_ = new QVBoxLayout();
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(6);
    rootLayout->addLayout(rowsLayout_);

    if (manager_) {
        connect(manager_, &ExportJobManager::jobsChanged, this, &ExportJobsBar::syncRows,
                Qt::QueuedConnection);
    }
    hide();
}

void ExportJobsBar::applyUiStrings() const {
    if (manager_) manager_->refreshRunningStatusText();
    for (const JobRowWidgets& widgets : rowsByJobId_) {
        applyRowUiStrings(widgets);
    }
}

void ExportJobsBar::clearAllRows() {
    rowsByJobId_.clear();
    clearLayoutItems(rowsLayout_);
}

void ExportJobsBar::syncRows() {
    if (!manager_ || !manager_->hasJobs()) {
        clearAllRows();
        hide();
        return;
    }

    const QVector<ExportJobSnapshot> snapshots = manager_->snapshots();
    QSet<int> activeJobIds;
    for (const ExportJobSnapshot& snapshot : snapshots) {
        activeJobIds.insert(snapshot.id);
    }

    for (auto iterator = rowsByJobId_.begin(); iterator != rowsByJobId_.end();) {
        if (!activeJobIds.contains(iterator.key())) {
            if (iterator.value().row) {
                rowsLayout_->removeWidget(iterator.value().row);
                iterator.value().row->deleteLater();
            }
            iterator = rowsByJobId_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    for (const ExportJobSnapshot& snapshot : snapshots) {
        JobRowWidgets& widgets = rowsByJobId_[snapshot.id];
        if (widgets.row) {
            updateRow(widgets, snapshot);
            continue;
        }
        widgets = createRow(snapshot);
    }

    show();
}

ExportJobsBar::JobRowWidgets ExportJobsBar::createRow(const ExportJobSnapshot& snapshot) {
    JobRowWidgets widgets;

    auto row = makeQtPtr<ExportJobRow>(this);
    auto rowLayout = makeQtPtr<QHBoxLayout>(row.get());
    rowLayout->setContentsMargins(8, 4, 8, 4);
    rowLayout->setSpacing(8);

    auto checkMark = makeQtPtr<ExportCheckMark>(row.get());
    checkMark->hide();
    rowLayout->addWidget(checkMark.get(), 0, Qt::AlignVCenter);
    widgets.checkMark = checkMark.get();

    auto nameLabel = makeQtPtr<QLabel>(snapshot.displayName, row.get());
    nameLabel->setMinimumWidth(160);
    rowLayout->addWidget(nameLabel.get(), 0);
    widgets.nameLabel = nameLabel.get();

    auto statusLabel = makeQtPtr<QLabel>(snapshot.statusText, row.get());
    Style::setRole(statusLabel.get(), "muted");
    statusLabel->setTextFormat(Qt::PlainText);
    statusLabel->setWordWrap(true);
    statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    rowLayout->addWidget(statusLabel.get(), 1);
    widgets.statusLabel = statusLabel.get();

    auto progressBar = makeQtPtr<QProgressBar>(row.get());
    progressBar->setRange(0, 100);
    progressBar->setFixedWidth(140);
    progressBar->setTextVisible(true);
    rowLayout->addWidget(progressBar.get(), 0);
    widgets.progressBar = progressBar.get();

    auto openFolderButton = makeQtPtr<QPushButton>(AppLocale::trUi("export.open_folder"), row.get());
    openFolderButton->setCursor(Qt::PointingHandCursor);
    openFolderButton->setAutoDefault(false);
    openFolderButton->setDefault(false);
    openFolderButton->setFocusPolicy(Qt::TabFocus);
    Style::setVariant(openFolderButton.get(), "outline");
    Style::setSize(openFolderButton.get(), "sm");
    openFolderButton->hide();
    connect(openFolderButton.get(), &QPushButton::clicked, this, [rowGuard = QPointer<ExportJobRow>(row.get()), this]() {
        if (!rowGuard) return;
        const int jobId = rowGuard->property("jobId").toInt();
        const auto iterator = rowsByJobId_.constFind(jobId);
        if (iterator == rowsByJobId_.cend()) return;
        const QString directory = iterator->outputDirectory;
        if (directory.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });
    rowLayout->addWidget(openFolderButton.get(), 0);
    widgets.openFolderButton = openFolderButton.get();

    auto cancelButton = makeQtPtr<QPushButton>(AppLocale::trUi("export.cancel"), row.get());
    cancelButton->setCursor(Qt::PointingHandCursor);
    Style::setVariant(cancelButton.get(), "destructive");
    Style::setSize(cancelButton.get(), "sm");
    const int jobId = snapshot.id;
    row->setProperty("jobId", jobId);
    connect(cancelButton.get(), &QPushButton::clicked, this,
            [jobId, mgr = QPointer<ExportJobManager>(manager_)]() {
                if (mgr) mgr->cancelJob(jobId);
            });
    rowLayout->addWidget(cancelButton.get(), 0);
    widgets.cancelButton = cancelButton.get();

    auto dismissButton = makeQtPtr<QPushButton>(AppLocale::trUi("export.job_dismiss"), row.get());
    dismissButton->setCursor(Qt::PointingHandCursor);
    Style::setVariant(dismissButton.get(), "ghost");
    Style::setSize(dismissButton.get(), "sm");
    connect(dismissButton.get(), &QPushButton::clicked, this,
            [jobId, mgr = QPointer<ExportJobManager>(manager_)]() {
                if (mgr) mgr->dismissJob(jobId);
            });
    rowLayout->addWidget(dismissButton.get(), 0);
    widgets.dismissButton = dismissButton.get();

    rowsLayout_->addWidget(row.get());
    widgets.row = row.get();

    updateRow(widgets, snapshot);
    return widgets;
}

void ExportJobsBar::playSuccess(JobRowWidgets& widgets) {
    widgets.celebrated = true;
    if (!widgets.checkMark) return;
    widgets.checkMark->setAccessibleName(AppLocale::trUi("export.done"));
    if (Motion::reduced()) {
        widgets.checkMark->setMarkScale(1.0);
        widgets.checkMark->setMarkOpacity(1.0);
        widgets.checkMark->show();
        if (widgets.row) widgets.row->setWashColor(Style::ThemeColors::transparent());
        return;
    }

    widgets.checkMark->setMarkScale(0.9);
    widgets.checkMark->setMarkOpacity(0.0);
    widgets.checkMark->show();
    const QEasingCurve curve = Motion::strongOut();
    Motion::animateProperty(widgets.checkMark, "markScale", 1.0, Motion::kMilestone, curve);
    Motion::animateProperty(widgets.checkMark, "markOpacity", 1.0, Motion::kMilestone, curve);
    if (widgets.row) {
        widgets.row->setWashColor(Style::ThemeColors::transparent());
        Motion::pulseOnce(widgets.row,
                          "washColor",
                          Style::ThemeColors::transparent(),
                          Style::ThemeColors::successWash(),
                          Motion::kMilestone);
    }
}

void ExportJobsBar::syncProgress(const JobRowWidgets& widgets, const ExportJobSnapshot& snapshot) const {
    QProgressBar* progressBar = widgets.progressBar;
    if (!progressBar) return;
    if (!snapshot.running) {
        Motion::animateProperty(progressBar, "value", progressBar->value(), 0, QEasingCurve(QEasingCurve::Linear));
        progressBar->hide();
        return;
    }

    progressBar->show();
    if (snapshot.progressIndeterminate) {
        Motion::animateProperty(progressBar, "value", 0, 0, QEasingCurve(QEasingCurve::Linear));
        progressBar->setRange(0, 0);
        progressBar->setTextVisible(false);
        return;
    }

    if (progressBar->maximum() == 0) {
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
    }
    progressBar->setTextVisible(true);
    Motion::animateProperty(progressBar,
                            "value",
                            snapshot.progressPercent,
                            Motion::kEnter,
                            QEasingCurve(QEasingCurve::Linear));
}

void ExportJobsBar::updateRow(JobRowWidgets& widgets, const ExportJobSnapshot& snapshot) {
    widgets.outputDirectory = snapshot.outputDirectory;
    if (widgets.nameLabel) widgets.nameLabel->setText(snapshot.displayName);
    if (widgets.statusLabel) widgets.statusLabel->setText(snapshot.statusText);

    RowPhase phase = RowPhase::Running;
    const char* jobState = "running";
    const char* statusRole = "muted";
    if (snapshot.succeeded) {
        phase = RowPhase::Succeeded;
        jobState = "succeeded";
    } else if (snapshot.failed) {
        phase = RowPhase::Failed;
        jobState = "failed";
        statusRole = "exportError";
    } else if (snapshot.cancelled) {
        phase = RowPhase::Cancelled;
        jobState = "cancelled";
    }

    if (widgets.row) Style::setProp(widgets.row, "jobState", QString::fromLatin1(jobState));
    if (widgets.statusLabel) Style::setRole(widgets.statusLabel, statusRole);

    const bool showSuccess = phase == RowPhase::Succeeded;
    if (showSuccess && !widgets.celebrated) playSuccess(widgets);
    if (!showSuccess && widgets.checkMark) widgets.checkMark->hide();

    if (widgets.openFolderButton) {
        widgets.openFolderButton->setVisible(showSuccess && !snapshot.outputDirectory.isEmpty());
    }
    if (widgets.cancelButton) widgets.cancelButton->setVisible(snapshot.canCancel);
    if (widgets.dismissButton) widgets.dismissButton->setVisible(snapshot.canDismiss);
    syncProgress(widgets, snapshot);
    widgets.phase = phase;
}

void ExportJobsBar::applyRowUiStrings(const JobRowWidgets& widgets) const {
    if (widgets.cancelButton) {
        widgets.cancelButton->setText(AppLocale::trUi("export.cancel"));
    }
    if (widgets.dismissButton) {
        widgets.dismissButton->setText(AppLocale::trUi("export.job_dismiss"));
    }
    if (widgets.openFolderButton) {
        widgets.openFolderButton->setText(AppLocale::trUi("export.open_folder"));
    }
    if (widgets.checkMark && widgets.celebrated) {
        widgets.checkMark->setAccessibleName(AppLocale::trUi("export.done"));
    }
}

#include "ExportJobsBar.moc"

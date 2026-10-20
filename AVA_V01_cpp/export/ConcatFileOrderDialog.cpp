#include "ConcatFileOrderDialog.h"

#include "QtPtr.h"

#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../style/StyleProps.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLayoutItem>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>

#include <QCursor>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace {

constexpr char kClipIndexMime[] = "application/x-ava-clip-index";
constexpr int kDragAutoScrollMarginPx = 28;
constexpr int kDragAutoScrollStepPx = 18;

class ClipTextLabel final : public QLabel {
public:
    explicit ClipTextLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setTextFormat(Qt::PlainText);
        setTextInteractionFlags(Qt::NoTextInteraction);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    void setClipText(const QString& text) {
        setTextFormat(Qt::PlainText);
        setText(text);
        setTextFormat(Qt::PlainText);
    }

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override { return textHeight(width); }

    QSize minimumSizeHint() const override { return QSize(0, fontMetrics().height()); }

    QSize sizeHint() const override {
        constexpr int preferredWidth = 280;
        return QSize(preferredWidth, textHeight(preferredWidth));
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setFont(font());
        painter.setPen(palette().color(foregroundRole()));
        painter.setClipRect(contentsRect());

        QTextLayout layout(text(), font());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        option.setAlignment(alignment());
        layout.setTextOption(option);

        const QRect bounds = contentsRect();
        const int width = std::max(1, bounds.width());
        qreal y = 0;
        layout.beginLayout();
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(width);
            line.setPosition(QPointF(0, y));
            y += line.height();
        }
        layout.endLayout();
        layout.draw(&painter, QPointF(bounds.topLeft()));
    }

private:
    int textHeight(int width) const {
        const int lineHeight = fontMetrics().height();
        if (text().isEmpty() || width <= 0) return lineHeight;

        QTextLayout layout(text(), font());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        qreal y = 0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(std::max(1, width));
            y += line.height();
        }
        layout.endLayout();
        return std::max(lineHeight, static_cast<int>(std::ceil(y)));
    }
};

class ClipSequenceScrollArea final : public QScrollArea {
public:
    explicit ClipSequenceScrollArea(QWidget* parent = nullptr) : QScrollArea(parent) {}

    QSize minimumSizeHint() const override { return QSize(0, 0); }

    QSize sizeHint() const override { return QSize(480, 280); }
};

class ClipListCanvas final : public QWidget {
public:
    std::function<void(QDragEnterEvent*)> onDragEnter;
    std::function<void(QDragMoveEvent*)> onDragMove;
    std::function<void(QDropEvent*)> onDrop;

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override {
        if (QLayout* box = layout()) return box->heightForWidth(std::max(1, width));
        return QWidget::heightForWidth(width);
    }

    // Keep the full sequence as the minimum height so the scroll area scrolls
    // instead of squeezing wrapped filenames onto one clipped line.
    QSize minimumSizeHint() const override {
        const int width = std::max(width(), 320);
        return QSize(0, heightForWidth(width));
    }

    QSize sizeHint() const override {
        const int width = std::max(width(), 480);
        return QSize(width, heightForWidth(width));
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (event->oldSize().width() != event->size().width()) updateGeometry();
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (onDragEnter) onDragEnter(event);
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        if (onDragMove) onDragMove(event);
    }
    void dropEvent(QDropEvent* event) override {
        if (onDrop) onDrop(event);
    }
};

class ClipRowFrame final : public QFrame {
public:
    explicit ClipRowFrame(QWidget* parent = nullptr) : QFrame(parent) {}

    int index = -1;
    QWidget* canvas = nullptr;
    std::function<void(int)> onSelect;
    std::function<void()> onDragStarting;
    std::function<void(int, int)> onDragOver;
    std::function<void(int, int)> onDropAt;
    std::function<void()> onDragFinished;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pressing_ = true;
            pressPosition_ = event->position().toPoint();
            if (onSelect) onSelect(index);
        }
        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!pressing_ || !(event->buttons() & Qt::LeftButton)) {
            QFrame::mouseMoveEvent(event);
            return;
        }
        if ((event->position().toPoint() - pressPosition_).manhattanLength() <
            QApplication::startDragDistance()) {
            return;
        }
        pressing_ = false;
        if (onDragStarting) onDragStarting();

        const int fromIndex = index;
        {
            QDrag drag(this);
            auto mimeData = std::make_unique<QMimeData>();
            mimeData->setData(QLatin1String(kClipIndexMime), QByteArray::number(fromIndex));
            drag.setMimeData(mimeData.release());
            drag.exec(Qt::MoveAction);
        }
        if (onDragFinished) onDragFinished();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        pressing_ = false;
        QFrame::mouseReleaseEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent* event) override { acceptClipDrag(event); }

    void dragMoveEvent(QDragMoveEvent* event) override {
        const int fromIndex = clipIndexFrom(event->mimeData());
        if (fromIndex < 0) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
        if (onDragOver && canvas) {
            onDragOver(fromIndex, mapTo(canvas, event->position().toPoint()).y());
        }
    }

    void dropEvent(QDropEvent* event) override {
        const int fromIndex = clipIndexFrom(event->mimeData());
        if (fromIndex < 0 || !canvas) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
        if (onDropAt) onDropAt(fromIndex, mapTo(canvas, event->position().toPoint()).y());
    }

private:
    static int clipIndexFrom(const QMimeData* mimeData) {
        if (!mimeData || !mimeData->hasFormat(QLatin1String(kClipIndexMime))) return -1;
        bool ok = false;
        const int index = mimeData->data(QLatin1String(kClipIndexMime)).toInt(&ok);
        return ok ? index : -1;
    }

    static void acceptClipDrag(QDragEnterEvent* event) {
        if (!event->mimeData()->hasFormat(QLatin1String(kClipIndexMime))) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
    }

    bool pressing_ = false;
    QPoint pressPosition_;
};

QLocale localeForUi() {
    return AppLocale::currentLanguage() == AppLocale::Language::Spanish
               ? QLocale(QLocale::Spanish)
               : QLocale(QLocale::English);
}

QString formatCount(int count) {
    return localeForUi().toString(count);
}

QString formatDuration(qint64 milliseconds) {
    if (milliseconds < 0) return {};
    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    const QLocale locale = localeForUi();
    const QString hourText = locale.toString(hours);
    const QString minuteText = locale.toString(minutes);
    const QString secondText = locale.toString(seconds);
    if (hours > 0 && minutes > 0 && seconds > 0) {
        return AppLocale::trUi("concat.duration_hms").arg(hourText, minuteText, secondText);
    }
    if (hours > 0 && minutes > 0) return AppLocale::trUi("concat.duration_hm").arg(hourText, minuteText);
    if (hours > 0 && seconds > 0) return AppLocale::trUi("concat.duration_hs").arg(hourText, secondText);
    if (hours > 0) return AppLocale::trUi("concat.duration_h").arg(hourText);
    if (minutes > 0 && seconds > 0) {
        return AppLocale::trUi("concat.duration_ms").arg(minuteText, secondText);
    }
    if (minutes > 0) return AppLocale::trUi("concat.duration_m").arg(minuteText);
    return AppLocale::trUi("concat.duration_s").arg(secondText);
}

QString headlineForCount(int count) {
    if (count <= 0) return AppLocale::trUi("concat.headline_zero");
    if (count == 1) return AppLocale::trUi("concat.headline_one");
    return AppLocale::trUi("concat.headline_many").arg(formatCount(count));
}

QString outputDetailForClips(const QVector<VideoClip>& clips) {
    const int count = clips.size();
    if (count <= 0) return AppLocale::trUi("concat.output_zero");
    const std::optional<qint64> duration = VideoClipSequence::totalDurationMs(clips);
    if (duration.has_value()) {
        const QString durationText = formatDuration(*duration);
        if (count == 1) return AppLocale::trUi("concat.output_one_duration").arg(durationText);
        return AppLocale::trUi("concat.output_many_duration").arg(formatCount(count), durationText);
    }
    if (count == 1) return AppLocale::trUi("concat.output_one");
    return AppLocale::trUi("concat.output_many").arg(formatCount(count));
}

void passMouseToRow(QWidget* widget) {
    if (!widget) return;
    widget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

int clipIndexFromMime(const QMimeData* mimeData) {
    if (!mimeData || !mimeData->hasFormat(QLatin1String(kClipIndexMime))) return -1;
    bool ok = false;
    const int index = mimeData->data(QLatin1String(kClipIndexMime)).toInt(&ok);
    return ok ? index : -1;
}

bool fixturePreviewEnabled() {
    const QByteArray value = qgetenv("AVA_CONCAT_ORDER_FIXTURES");
    return !value.isEmpty() && value != "0";
}

}  // namespace

ConcatFileOrderDialog::ConcatFileOrderDialog(const QStringList& filePaths, QWidget* parent)
    : QDialog(parent),
      originalFilePaths_(filePaths),
      clips_(VideoClipSequence::chronologicalOrder(VideoClipSequence::clipsFromFilePaths(filePaths))),
      selectedIndex_(clips_.isEmpty() ? -1 : 0),
      rebuildQueued_(false),
      rebuildNeeded_(false),
      insideDrag_(false),
      headlineLabel_(nullptr),
      subtitleLabel_(nullptr),
      scrollArea_(nullptr),
      listCanvas_(nullptr),
      listLayout_(nullptr),
      dropIndicator_(nullptr),
      flowLabel_(nullptr),
      outputTitleLabel_(nullptr),
      outputDetailLabel_(nullptr),
      resetButton_(nullptr),
      cancelButton_(nullptr),
      continueButton_(nullptr),
      fixtureBar_(nullptr) {
    buildUi();
}

QStringList ConcatFileOrderDialog::orderedFilePaths() const {
    return VideoClipSequence::filePathsOf(clips_);
}

void ConcatFileOrderDialog::buildUi() {
    Style::setRole(this, "fileOrder");
    setSizeGripEnabled(true);

    auto rootLayout = makeQtPtr<QVBoxLayout>(this);
    rootLayout->setSpacing(12);
    rootLayout->setContentsMargins(24, 20, 24, 20);

    auto headlineLabel = makeQtPtr<QLabel>(this);
    headlineLabel->setObjectName(QStringLiteral("concatHeadline"));
    headlineLabel->setWordWrap(true);
    headlineLabel->setTextFormat(Qt::PlainText);
    Style::setRole(headlineLabel.get(), "h2");
    headlineLabel_ = headlineLabel.get();
    rootLayout->addWidget(headlineLabel.get());

    auto subtitleLabel = makeQtPtr<QLabel>(this);
    subtitleLabel->setObjectName(QStringLiteral("concatSubtitle"));
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setTextFormat(Qt::PlainText);
    Style::setRole(subtitleLabel.get(), "muted");
    subtitleLabel_ = subtitleLabel.get();
    rootLayout->addWidget(subtitleLabel.get());

    auto scrollArea = makeQtPtr<ClipSequenceScrollArea>(this);
    scrollArea->setObjectName(QStringLiteral("concatScroll"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    Style::setRole(scrollArea.get(), "clipSequence");
    scrollArea_ = scrollArea.get();

    auto listCanvas = makeQtPtr<ClipListCanvas>();
    listCanvas->setObjectName(QStringLiteral("clipListCanvas"));
    listCanvas->setAcceptDrops(true);
    auto listLayout = makeQtPtr<QVBoxLayout>(listCanvas.get());
    listLayout->setContentsMargins(0, 0, 4, 0);
    listLayout->setSpacing(0);
    listLayout->addStretch(1);
    listCanvas_ = listCanvas.get();
    listLayout_ = listLayout.get();

    listCanvas->onDragEnter = [this](QDragEnterEvent* event) {
        if (clipIndexFromMime(event->mimeData()) < 0) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
    };
    listCanvas->onDragMove = [this](QDragMoveEvent* event) {
        const int fromIndex = clipIndexFromMime(event->mimeData());
        if (fromIndex < 0) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
        dragOver(fromIndex, event->position().toPoint().y());
    };
    listCanvas->onDrop = [this](QDropEvent* event) {
        const int fromIndex = clipIndexFromMime(event->mimeData());
        if (fromIndex < 0) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
        dropAt(fromIndex, event->position().toPoint().y());
    };

    auto dropIndicator = makeQtPtr<QFrame>(listCanvas.get());
    dropIndicator->setObjectName(QStringLiteral("clipDropIndicator"));
    Style::setRole(dropIndicator.get(), "clipDrop");
    dropIndicator->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    dropIndicator->setAttribute(Qt::WA_StyledBackground, true);
    dropIndicator->hide();
    dropIndicator_ = dropIndicator.get();

    scrollArea->setWidget(listCanvas.get());
    rootLayout->addWidget(scrollArea.get(), 1);

    auto flowLabel = makeQtPtr<QLabel>(this);
    flowLabel->setObjectName(QStringLiteral("concatFlow"));
    flowLabel->setAlignment(Qt::AlignHCenter);
    flowLabel->setText(QStringLiteral("\u2193"));
    flowLabel->setTextFormat(Qt::PlainText);
    Style::setRole(flowLabel.get(), "clipFlow");
    flowLabel_ = flowLabel.get();
    rootLayout->addWidget(flowLabel.get());

    auto outputCard = makeQtPtr<QFrame>(this);
    outputCard->setObjectName(QStringLiteral("concatOutput"));
    outputCard->setAttribute(Qt::WA_StyledBackground, true);
    Style::setRole(outputCard.get(), "clipOutput");
    auto outputLayout = makeQtPtr<QVBoxLayout>(outputCard.get());
    outputLayout->setContentsMargins(14, 12, 14, 12);
    outputLayout->setSpacing(2);

    auto outputTitleLabel = makeQtPtr<QLabel>(outputCard.get());
    outputTitleLabel->setObjectName(QStringLiteral("concatOutputTitle"));
    outputTitleLabel->setWordWrap(true);
    outputTitleLabel->setTextFormat(Qt::PlainText);
    Style::setRole(outputTitleLabel.get(), "clipOutputTitle");
    outputTitleLabel_ = outputTitleLabel.get();
    outputLayout->addWidget(outputTitleLabel.get());

    auto outputDetailLabel = makeQtPtr<QLabel>(outputCard.get());
    outputDetailLabel->setObjectName(QStringLiteral("concatOutputDetail"));
    outputDetailLabel->setWordWrap(true);
    outputDetailLabel->setTextFormat(Qt::PlainText);
    Style::setRole(outputDetailLabel.get(), "muted");
    outputDetailLabel_ = outputDetailLabel.get();
    outputLayout->addWidget(outputDetailLabel.get());
    rootLayout->addWidget(outputCard.get());

    auto resetButton = makeQtPtr<QPushButton>(this);
    resetButton->setObjectName(QStringLiteral("concatReset"));
    resetButton->setCursor(Qt::PointingHandCursor);
    resetButton->setAutoDefault(false);
    Style::setVariant(resetButton.get(), "outline");
    Style::setSize(resetButton.get(), "sm");
    resetButton_ = resetButton.get();
    connect(resetButton.get(), &QPushButton::clicked, this, [this] { resetChronologicalOrder(); });

    auto resetRow = makeQtPtr<QHBoxLayout>();
    resetRow->setSpacing(8);
    resetRow->addWidget(resetButton.get());
    resetRow->addStretch(1);
    rootLayout->addLayout(resetRow.get());

    auto cancelButton = makeQtPtr<QPushButton>(this);
    cancelButton->setObjectName(QStringLiteral("concatCancel"));
    cancelButton->setCursor(Qt::PointingHandCursor);
    cancelButton->setAutoDefault(false);
    Style::setVariant(cancelButton.get(), "ghost");
    Style::setSize(cancelButton.get(), "md");
    cancelButton_ = cancelButton.get();

    auto continueButton = makeQtPtr<QPushButton>(this);
    continueButton->setObjectName(QStringLiteral("concatContinue"));
    continueButton->setCursor(Qt::PointingHandCursor);
    continueButton->setDefault(true);
    continueButton->setAutoDefault(true);
    Style::setVariant(continueButton.get(), "welcomeImport");
    Style::setSize(continueButton.get(), "lg");
    continueButton_ = continueButton.get();

    auto actionRow = makeQtPtr<QHBoxLayout>();
    actionRow->setSpacing(12);
    actionRow->addStretch(1);
    actionRow->addWidget(cancelButton.get());
    actionRow->addWidget(continueButton.get());
    rootLayout->addLayout(actionRow.get());

    connect(cancelButton.get(), &QPushButton::clicked, this, &QDialog::reject);
    connect(continueButton.get(), &QPushButton::clicked, this, &QDialog::accept);

    auto moveUpShortcut = makeQtPtr<QShortcut>(QKeySequence(Qt::CTRL | Qt::Key_Up), this);
    moveUpShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(moveUpShortcut.get(), &QShortcut::activated, this, [this] {
        const int index = indexForKeyboard();
        moveRow(index, index - 1);
    });
    auto moveDownShortcut = makeQtPtr<QShortcut>(QKeySequence(Qt::CTRL | Qt::Key_Down), this);
    moveDownShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(moveDownShortcut.get(), &QShortcut::activated, this, [this] {
        const int index = indexForKeyboard();
        moveRow(index, index + 1);
    });

    if (fixturePreviewEnabled()) {
        auto fixtureBar = makeQtPtr<QWidget>(this);
        fixtureBar->setObjectName(QStringLiteral("fixtureToggle"));
        fixtureBar->setAttribute(Qt::WA_StyledBackground, true);
        Style::setRole(fixtureBar.get(), "devFixture");
        auto fixtureLayout = makeQtPtr<QHBoxLayout>(fixtureBar.get());
        fixtureLayout->setContentsMargins(4, 4, 4, 4);
        fixtureLayout->setSpacing(2);
        auto fixtureGroup = makeQtPtr<QButtonGroup>(fixtureBar.get());
        fixtureGroup->setExclusive(true);

        const struct FixtureSegment {
            const char* label;
            QStringList filePaths;
        } segments[] = {
            {"Demo", originalFilePaths_},
            {"Worst case", VideoClipSequence::stressFilePaths()},
            {"Empty", {}},
            {"One", {QStringLiteral("/matches/demo/penalty_corner_003_10m55s-11m54s_p72.mp4")}},
        };
        for (int index = 0; index < 4; ++index) {
            auto segmentButton = makeQtPtr<QPushButton>(QString::fromUtf8(segments[index].label), fixtureBar.get());
            segmentButton->setCheckable(true);
            segmentButton->setAutoDefault(false);
            segmentButton->setCursor(Qt::PointingHandCursor);
            if (index == 0) segmentButton->setChecked(true);
            fixtureGroup->addButton(segmentButton.get());
            fixtureLayout->addWidget(segmentButton.get());
            const QStringList filePaths = segments[index].filePaths;
            connect(segmentButton.get(), &QPushButton::clicked, this, [this, filePaths] {
                applyFilePaths(filePaths);
            });
        }
        fixtureBar_ = fixtureBar.get();
        rootLayout->addWidget(fixtureBar.get(), 0, Qt::AlignHCenter);
    }

    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, this, [this] {
        applyStaticCopy();
        rebuildRows();
    });

    applyStaticCopy();
    rebuildRows();
}

void ConcatFileOrderDialog::applyStaticCopy() {
    setWindowTitle(AppLocale::trUi("concat.dialog_title"));
    if (subtitleLabel_) subtitleLabel_->setText(AppLocale::trUi("concat.subtitle"));
    if (flowLabel_) flowLabel_->setAccessibleName(AppLocale::trUi("concat.flow_accessible"));
    if (outputTitleLabel_) outputTitleLabel_->setText(AppLocale::trUi("concat.output_title"));
    if (resetButton_) resetButton_->setText(AppLocale::trUi("concat.reset_order"));
    if (cancelButton_) cancelButton_->setText(AppLocale::trUi("concat.cancel"));
    if (continueButton_) continueButton_->setText(AppLocale::trUi("concat.continue_btn"));
    refreshSummary();
}

void ConcatFileOrderDialog::refreshSummary() {
    if (headlineLabel_) headlineLabel_->setText(headlineForCount(clips_.size()));
    if (outputDetailLabel_) outputDetailLabel_->setText(outputDetailForClips(clips_));
    if (continueButton_) continueButton_->setEnabled(!clips_.isEmpty());
    if (resetButton_) {
        const bool alreadyChronological = VideoClipSequence::isChronological(clips_);
        resetButton_->setEnabled(!clips_.isEmpty() && !alreadyChronological);
        resetButton_->setToolTip(alreadyChronological ? AppLocale::trUi("concat.reset_done")
                                                      : AppLocale::trUi("concat.reset_hint"));
    }
}

void ConcatFileOrderDialog::rebuildRows() {
    clearRows();
    if (selectedIndex_ >= clips_.size()) selectedIndex_ = clips_.size() - 1;
    rows_.reserve(clips_.size());
    for (int index = 0; index < clips_.size(); ++index) {
        rows_.append(createRow(index));
    }
    refreshSummary();
}

void ConcatFileOrderDialog::clearRows() {
    rows_.clear();
    if (!listLayout_) return;
    while (listLayout_->count() > 1) {
        QLayoutItem* item = listLayout_->takeAt(0);
        if (QWidget* widget = item->widget()) unparentAndDelete(widget);
        delete item;
    }
}

QFrame* ConcatFileOrderDialog::createRow(int index) {
    const VideoClip& clip = clips_.at(index);
    auto row = makeQtPtr<ClipRowFrame>(listCanvas_);
    row->index = index;
    row->canvas = listCanvas_;
    row->setObjectName(QStringLiteral("clipRow"));
    row->setProperty("rowIndex", index);
    row->setProperty("filePath", clip.filePath);
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setAttribute(Qt::WA_Hover, true);
    row->setAcceptDrops(true);
    row->setCursor(Qt::OpenHandCursor);
    row->setToolTip(clip.filePath);
    row->setFocusPolicy(Qt::NoFocus);
    Style::setRole(row.get(), "clipRow");
    Style::setState(row.get(), "selected", index == selectedIndex_);

    const QString title = clip.title();
    const QString timeRange = clip.timeRangeText();
    const QString fileLabel = VideoClipSequence::fileLabel(clips_, index);
    if (timeRange.isEmpty()) {
        row->setAccessibleName(AppLocale::trUi("concat.row_accessible_no_time")
                                   .arg(QString::number(index + 1), title));
    } else {
        row->setAccessibleName(
            AppLocale::trUi("concat.row_accessible").arg(QString::number(index + 1), title, timeRange));
    }

    row->onSelect = [this](int rowIndex) { selectRow(rowIndex); };
    row->onDragStarting = [this] { beginDrag(); };
    row->onDragOver = [this](int fromIndex, int canvasY) { dragOver(fromIndex, canvasY); };
    row->onDropAt = [this](int fromIndex, int canvasY) { dropAt(fromIndex, canvasY); };
    row->onDragFinished = [this] { finishDrag(); };

    auto rowLayout = makeQtPtr<QHBoxLayout>(row.get());
    rowLayout->setContentsMargins(8, 6, 8, 0);
    rowLayout->setSpacing(10);

    auto rail = makeQtPtr<QWidget>(row.get());
    auto railLayout = makeQtPtr<QVBoxLayout>(rail.get());
    railLayout->setContentsMargins(0, 2, 0, 0);
    railLayout->setSpacing(4);

    auto indexLabel = makeQtPtr<QLabel>(rail.get());
    indexLabel->setObjectName(QStringLiteral("clipIndex"));
    indexLabel->setText(QString::number(index + 1));
    indexLabel->setAlignment(Qt::AlignCenter);
    indexLabel->setTextFormat(Qt::PlainText);
    Style::setRole(indexLabel.get(), "clipIndex");
    railLayout->addWidget(indexLabel.get(), 0, Qt::AlignHCenter);

    auto connector = makeQtPtr<QFrame>(rail.get());
    connector->setObjectName(QStringLiteral("clipConnector"));
    Style::setRole(connector.get(), "clipConnector");
    connector->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    railLayout->addWidget(connector.get(), 1, Qt::AlignHCenter);
    passMouseToRow(connector.get());
    passMouseToRow(indexLabel.get());
    passMouseToRow(rail.get());
    rowLayout->addWidget(rail.get());

    auto textColumn = makeQtPtr<QWidget>(row.get());
    auto textLayout = makeQtPtr<QVBoxLayout>(textColumn.get());
    textLayout->setContentsMargins(0, 2, 0, 10);
    textLayout->setSpacing(2);

    auto titleLabel = makeQtPtr<ClipTextLabel>(textColumn.get());
    titleLabel->setObjectName(QStringLiteral("clipTitle"));
    titleLabel->setClipText(title);
    Style::setRole(titleLabel.get(), "clipTitle");
    textLayout->addWidget(titleLabel.get());
    passMouseToRow(titleLabel.get());

    if (!timeRange.isEmpty()) {
        auto timeLabel = makeQtPtr<ClipTextLabel>(textColumn.get());
        timeLabel->setObjectName(QStringLiteral("clipTime"));
        timeLabel->setClipText(timeRange);
        Style::setRole(timeLabel.get(), "clipTime");
        textLayout->addWidget(timeLabel.get());
        passMouseToRow(timeLabel.get());
    }

    if (!fileLabel.isEmpty() && fileLabel != title) {
        auto fileNameLabel = makeQtPtr<ClipTextLabel>(textColumn.get());
        fileNameLabel->setObjectName(QStringLiteral("clipFile"));
        fileNameLabel->setClipText(fileLabel);
        Style::setRole(fileNameLabel.get(), "clipFile");
        textLayout->addWidget(fileNameLabel.get());
        passMouseToRow(fileNameLabel.get());
    }
    passMouseToRow(textColumn.get());
    rowLayout->addWidget(textColumn.get(), 1);

    const QString moveUpShortcut =
        QKeySequence(Qt::CTRL | Qt::Key_Up).toString(QKeySequence::NativeText);
    const QString moveDownShortcut =
        QKeySequence(Qt::CTRL | Qt::Key_Down).toString(QKeySequence::NativeText);

    auto moveUpButton = makeQtPtr<QPushButton>(QStringLiteral("\u2191"), row.get());
    moveUpButton->setObjectName(QStringLiteral("clipMoveUp"));
    moveUpButton->setProperty("rowIndex", index);
    moveUpButton->setCursor(Qt::PointingHandCursor);
    moveUpButton->setAutoDefault(false);
    moveUpButton->setEnabled(index > 0);
    moveUpButton->setToolTip(AppLocale::trUi("concat.move_up_shortcut").arg(moveUpShortcut));
    moveUpButton->setAccessibleName(AppLocale::trUi("concat.move_up"));
    Style::setVariant(moveUpButton.get(), "ghost");
    Style::setSize(moveUpButton.get(), "sm");
    Style::setRole(moveUpButton.get(), "clipMove");

    auto moveDownButton = makeQtPtr<QPushButton>(QStringLiteral("\u2193"), row.get());
    moveDownButton->setObjectName(QStringLiteral("clipMoveDown"));
    moveDownButton->setProperty("rowIndex", index);
    moveDownButton->setCursor(Qt::PointingHandCursor);
    moveDownButton->setAutoDefault(false);
    moveDownButton->setEnabled(index + 1 < clips_.size());
    moveDownButton->setToolTip(AppLocale::trUi("concat.move_down_shortcut").arg(moveDownShortcut));
    moveDownButton->setAccessibleName(AppLocale::trUi("concat.move_down"));
    Style::setVariant(moveDownButton.get(), "ghost");
    Style::setSize(moveDownButton.get(), "sm");
    Style::setRole(moveDownButton.get(), "clipMove");

    auto buttonColumn = makeQtPtr<QVBoxLayout>();
    buttonColumn->setSpacing(4);
    buttonColumn->setContentsMargins(0, 2, 0, 8);
    buttonColumn->addWidget(moveUpButton.get());
    buttonColumn->addWidget(moveDownButton.get());
    buttonColumn->addStretch(1);
    rowLayout->addLayout(buttonColumn.get());

    const QString filePath = clip.filePath;
    connect(moveUpButton.get(), &QPushButton::clicked, this, [this, filePath] {
        const int fromIndex = indexOfFilePath(filePath);
        moveRow(fromIndex, fromIndex - 1);
    });
    connect(moveDownButton.get(), &QPushButton::clicked, this, [this, filePath] {
        const int fromIndex = indexOfFilePath(filePath);
        moveRow(fromIndex, fromIndex + 1);
    });

    listLayout_->insertWidget(listLayout_->count() - 1, row.get());
    return row.get();
}

void ConcatFileOrderDialog::queueRebuild() {
    rebuildNeeded_ = true;
    if (insideDrag_ || rebuildQueued_) return;
    rebuildQueued_ = true;
    // Defer until the clicked button has finished its event. Rebuilding now
    // would delete that button on its own stack.
    QTimer::singleShot(0, this, [this] { flushRebuild(); });
}

void ConcatFileOrderDialog::flushRebuild() {
    rebuildQueued_ = false;
    if (insideDrag_ || !rebuildNeeded_) return;
    rebuildNeeded_ = false;
    rebuildRows();
    revealSelectedRow();
}

void ConcatFileOrderDialog::revealSelectedRow() {
    if (!scrollArea_ || selectedIndex_ < 0 || selectedIndex_ >= rows_.size()) return;
    QFrame* row = rows_.at(selectedIndex_);
    if (!row) return;
    scrollArea_->ensureWidgetVisible(row);
}

void ConcatFileOrderDialog::moveRow(int fromIndex, int toIndex) {
    if (!VideoClipSequence::moveClip(clips_, fromIndex, toIndex)) return;
    selectedIndex_ = toIndex;
    queueRebuild();
}

void ConcatFileOrderDialog::resetChronologicalOrder() {
    clips_ = VideoClipSequence::chronologicalOrder(clips_);
    if (selectedIndex_ >= clips_.size()) selectedIndex_ = clips_.isEmpty() ? -1 : 0;
    rebuildNeeded_ = false;
    rebuildRows();
    revealSelectedRow();
}

void ConcatFileOrderDialog::selectRow(int index) {
    if (index < 0 || index >= rows_.size()) return;
    selectedIndex_ = index;
    for (int rowIndex = 0; rowIndex < rows_.size(); ++rowIndex) {
        QFrame* row = rows_.at(rowIndex);
        if (!row) continue;
        Style::setState(row, "selected", rowIndex == selectedIndex_);
    }
}

int ConcatFileOrderDialog::indexForKeyboard() const {
    // After a move, row widgets still show the previous index until the deferred
    // rebuild. Follow the clip we already moved instead of that stale property.
    if (rebuildNeeded_) return selectedIndex_;
    const QWidget* focusWidget = QApplication::focusWidget();
    if (focusWidget && focusWidget->property("rowIndex").isValid()) {
        return focusWidget->property("rowIndex").toInt();
    }
    return selectedIndex_;
}

void ConcatFileOrderDialog::beginDrag() {
    insideDrag_ = true;
}

void ConcatFileOrderDialog::dragOver(int fromIndex, int canvasY) {
    Q_UNUSED(fromIndex);
    if (!listCanvas_ || !dropIndicator_ || rows_.isEmpty()) return;

    if (scrollArea_ && scrollArea_->viewport() && scrollArea_->verticalScrollBar()) {
        const QPoint viewportPosition = scrollArea_->viewport()->mapFromGlobal(QCursor::pos());
        QScrollBar* scrollBar = scrollArea_->verticalScrollBar();
        if (viewportPosition.y() < kDragAutoScrollMarginPx) {
            scrollBar->setValue(scrollBar->value() - kDragAutoScrollStepPx);
        } else if (viewportPosition.y() > scrollArea_->viewport()->height() - kDragAutoScrollMarginPx) {
            scrollBar->setValue(scrollBar->value() + kDragAutoScrollStepPx);
        }
    }

    const int insertBefore = insertBeforeForCanvasY(canvasY);
    int gapY = 0;
    if (insertBefore >= rows_.size()) {
        QFrame* lastRow = rows_.isEmpty() ? nullptr : rows_.last().data();
        gapY = lastRow ? lastRow->geometry().bottom() : 0;
    } else if (rows_.at(insertBefore)) {
        gapY = rows_.at(insertBefore)->geometry().top();
    }
    const int width = std::max(0, listCanvas_->width() - 24);
    dropIndicator_->setGeometry(12, std::max(0, gapY - 1), width, 2);
    dropIndicator_->show();
    dropIndicator_->raise();
}

void ConcatFileOrderDialog::dropAt(int fromIndex, int canvasY) {
    hideDropIndicator();
    const int target =
        VideoClipSequence::finalIndexForDrop(fromIndex, insertBeforeForCanvasY(canvasY), clips_.size());
    if (target < 0 || target == fromIndex) return;
    if (!VideoClipSequence::moveClip(clips_, fromIndex, target)) return;
    selectedIndex_ = target;
    queueRebuild();
}

void ConcatFileOrderDialog::finishDrag() {
    insideDrag_ = false;
    hideDropIndicator();
    // drag.exec() has returned, so a queued rebuild runs after this row's
    // mouse-move handler returns and does not delete the drag source in-flight.
    if (!rebuildNeeded_ || rebuildQueued_) return;
    rebuildQueued_ = true;
    QTimer::singleShot(0, this, [this] { flushRebuild(); });
}

void ConcatFileOrderDialog::hideDropIndicator() {
    if (dropIndicator_) dropIndicator_->hide();
}

int ConcatFileOrderDialog::insertBeforeForCanvasY(int canvasY) const {
    for (int index = 0; index < rows_.size(); ++index) {
        const QFrame* row = rows_.at(index);
        if (!row) continue;
        if (canvasY < row->geometry().center().y()) return index;
    }
    return rows_.size();
}

void ConcatFileOrderDialog::applyFilePaths(const QStringList& filePaths) {
    clips_ = VideoClipSequence::chronologicalOrder(VideoClipSequence::clipsFromFilePaths(filePaths));
    selectedIndex_ = clips_.isEmpty() ? -1 : 0;
    rebuildNeeded_ = false;
    rebuildRows();
    revealSelectedRow();
}

int ConcatFileOrderDialog::indexOfFilePath(const QString& filePath) const {
    for (int index = 0; index < clips_.size(); ++index) {
        if (clips_.at(index).filePath == filePath) return index;
    }
    return -1;
}

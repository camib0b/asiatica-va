#include "WelcomeWindow.h"
#include "QtPtr.h"
#include "../style/Motion.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"

#include <QAction>
#include <QButtonGroup>
#include <QByteArray>
#include <QEvent>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QWidget* mountEntrance(QWidget* parent, QWidget* child, QVBoxLayout* column) {
    auto frame = makeQtPtr<QWidget>(parent);
    auto layout = makeQtPtr<QVBoxLayout>(frame.get());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(child, 0, Qt::AlignHCenter);
    column->addWidget(frame.get(), 0, Qt::AlignHCenter);
    return frame.release();
}

bool fixturePreviewEnabled() {
    const QByteArray value = qgetenv("AVA_WELCOME_FIXTURES");
    return !value.isEmpty() && value != "0";
}

// "&Select" is a shortcut letter. A doubled "&&" is a literal ampersand.
QString textWithoutMnemonic(const QString& text) {
    QString display;
    display.reserve(text.size());
    for (int index = 0; index < text.size(); ++index) {
        const QChar character = text.at(index);
        if (character != QLatin1Char('&')) {
            display.append(character);
            continue;
        }
        if (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('&')) {
            display.append(QLatin1Char('&'));
            ++index;
        }
    }
    return display;
}

}  // namespace

WelcomeWindow::WelcomeWindow(QWidget* parent)
    : QWidget(parent),
      titleLabel_(nullptr),
      importButton_(nullptr),
      titleEntrance_(nullptr),
      importEntrance_(nullptr),
      importMnemonicAction_(nullptr),
      copyFixture_(CopyFixture::Demo),
      entrancePlayed_(false) {
    setObjectName("AppRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
    buildKeyboardShortcuts();
    applyUiStrings();
    setMinimumSize(320, 250);
}

void WelcomeWindow::applyUiStrings() {
    if (titleLabel_) titleLabel_->setText(titleForFixture());
    if (importButton_) {
        const QString labeled = importLabelForFixture();
        importButton_->setText(textWithoutMnemonic(labeled));
        if (importMnemonicAction_) {
            importMnemonicAction_->setShortcut(QKeySequence::mnemonic(labeled));
        }
        syncImportButtonMinimumWidth();
    }
}

QString WelcomeWindow::titleForFixture() const {
    switch (copyFixture_) {
        case CopyFixture::WorstCase:
            return QString::fromUtf8(
                "Đặng Thị Ngọc Hân — Aleksandra Wiśniewska-Kowalczyk\n<script>alert(1)</script> &amp;");
        case CopyFixture::Empty:
            return QString();
        case CopyFixture::One:
            return QStringLiteral("J");
        case CopyFixture::Demo:
            return QStringLiteral("ava");
    }
    return QStringLiteral("ava");
}

QString WelcomeWindow::importLabelForFixture() const {
    switch (copyFixture_) {
        case CopyFixture::WorstCase:
            return QString::fromUtf8(
                "Benachrichtigungseinstellungen bartholomew.fitzgerald@northwind-industries-holdings.example.com &amp;");
        case CopyFixture::Empty:
            return QString();
        case CopyFixture::One:
            return QStringLiteral("J");
        case CopyFixture::Demo:
            return AppLocale::trUi("welcome.import");
    }
    return AppLocale::trUi("welcome.import");
}

void WelcomeWindow::buildUi() {
    auto outerLayout = makeQtPtr<QVBoxLayout>(this);
    outerLayout->setContentsMargins(24, 24, 24, 24);
    outerLayout->addStretch(1);

    auto contentContainer = makeQtPtr<QWidget>(this);
    auto layout = makeQtPtr<QVBoxLayout>(contentContainer.get());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto titleLabel = makeQtPtr<QLabel>(contentContainer.get());
    titleLabel->setWordWrap(false);
    titleLabel->setAlignment(Qt::AlignCenter);
    Style::setRole(titleLabel.get(), "h1");
    titleLabel_ = titleLabel.get();

    auto importButton = makeQtPtr<QPushButton>(contentContainer.get());
    importButton->setCursor(Qt::PointingHandCursor);
    Style::setVariant(importButton.get(), "welcomeImport");
    Style::setSize(importButton.get(), "lg");
    importButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    importButton->setFocusPolicy(Qt::TabFocus); // Allow keyboard focus but don't auto-focus on window open
    importButton_ = importButton.get();

    titleEntrance_ = mountEntrance(contentContainer.get(), titleLabel.get(), layout.get());
    importEntrance_ = mountEntrance(contentContainer.get(), importButton.get(), layout.get());
    Motion::installAmbientPulse(importButton_);

    outerLayout->addWidget(contentContainer.get(), 0, Qt::AlignCenter);
    outerLayout->addStretch(1);
    if (fixturePreviewEnabled()) buildFixtureToggle(outerLayout.get());
}

void WelcomeWindow::buildFixtureToggle(QVBoxLayout* outerLayout) {
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
        CopyFixture fixture;
    } segments[] = {
        {"Demo data", CopyFixture::Demo},
        {"Worst case", CopyFixture::WorstCase},
        {"Empty", CopyFixture::Empty},
        {"One", CopyFixture::One},
    };
    for (const FixtureSegment& segment : segments) {
        auto segmentButton = makeQtPtr<QPushButton>(QString::fromUtf8(segment.label), fixtureBar.get());
        segmentButton->setCheckable(true);
        segmentButton->setAutoDefault(false);
        segmentButton->setFocusPolicy(Qt::NoFocus);
        segmentButton->setCursor(Qt::PointingHandCursor);
        if (segment.fixture == CopyFixture::Demo) segmentButton->setChecked(true);
        fixtureGroup->addButton(segmentButton.get());
        fixtureLayout->addWidget(segmentButton.get());
        const CopyFixture fixture = segment.fixture;
        connect(segmentButton.get(), &QPushButton::clicked, this, [this, fixture]() {
            copyFixture_ = fixture;
            applyUiStrings();
        });
    }
    outerLayout->addWidget(fixtureBar.get(), 0, Qt::AlignHCenter);
}

void WelcomeWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    const bool playIntro = !entrancePlayed_;
    if (playIntro) {
        entrancePlayed_ = true;
        playEntrance();
    }
    const int delayMs = (playIntro && !Motion::reduced()) ? (Motion::kEnter + Motion::kStagger) : 0;
    scheduleAmbientPulse(delayMs);
}

void WelcomeWindow::hideEvent(QHideEvent* event) {
    if (QTimer* timer = findChild<QTimer*>(QStringLiteral("welcome.pulseDelay"), Qt::FindDirectChildrenOnly)) {
        timer->stop();
    }
    Motion::setAmbientPulseActive(importButton_, false);
    QWidget::hideEvent(event);
}

void WelcomeWindow::scheduleAmbientPulse(int delayMs) {
    if (!importButton_) return;
    auto* timer = findChild<QTimer*>(QStringLiteral("welcome.pulseDelay"), Qt::FindDirectChildrenOnly);
    if (delayMs <= 0 || !isVisible()) {
        if (timer) timer->stop();
        Motion::setAmbientPulseActive(importButton_, isVisible());
        return;
    }
    if (!timer) {
        auto created = makeQtPtr<QTimer>(this);
        created->setObjectName(QStringLiteral("welcome.pulseDelay"));
        created->setSingleShot(true);
        connect(created.get(), &QTimer::timeout, this, [guard = QPointer<WelcomeWindow>(this)]() {
            if (!guard || !guard->isVisible()) return;
            Motion::setAmbientPulseActive(guard->importButton_, true);
        });
        timer = created.get();
    }
    timer->start(delayMs);
}

void WelcomeWindow::playEntrance() {
    // The import button is a child of its entrance frame, so it stays enabled
    // and clickable while the frame is still transparent.
    Motion::fadeIn(titleEntrance_, Motion::kEnter, 8, 0);
    Motion::fadeIn(importEntrance_, Motion::kEnter, 8, Motion::kStagger);
}

void WelcomeWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (!event) return;
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
        syncImportButtonMinimumWidth();
    }
}

void WelcomeWindow::syncImportButtonMinimumWidth() {
    if (!importButton_) return;
    importButton_->setMinimumWidth(Style::pushButtonMinimumWidth(importButton_));
}

void WelcomeWindow::wireSignals() {
    connect(importButton_, &QPushButton::clicked, this, &WelcomeWindow::videoImportRequested);
}

void WelcomeWindow::activateImportButton() {
    if (importButton_ && importButton_->isEnabled()) importButton_->click();
}

void WelcomeWindow::buildKeyboardShortcuts() {
    auto importShortcut = makeQtPtr<QAction>(this);
    importShortcut->setShortcut(QKeySequence(Qt::Key_S));
    // Only while this stacked page has focus; Window/Application would still fire on WorkWindow.
    importShortcut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(importShortcut.get(), &QAction::triggered, this, &WelcomeWindow::activateImportButton);
    addAction(importShortcut.get());

    // The visible label has no mnemonic underline. The "&" in welcome.import
    // still chooses the letter (Alt+S in English, Alt+E in Spanish).
    auto importMnemonic = makeQtPtr<QAction>(this);
    importMnemonic->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(importMnemonic.get(), &QAction::triggered, this, &WelcomeWindow::activateImportButton);
    importMnemonicAction_ = importMnemonic.get();
    addAction(importMnemonic.get());
}

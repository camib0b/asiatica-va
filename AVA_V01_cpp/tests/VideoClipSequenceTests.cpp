#include "AppLocale.h"
#include "ConcatFileOrderDialog.h"
#include "VideoClipSequence.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QLayout>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>

#include <algorithm>

namespace {

QStringList penaltyCornerPathsReversed() {
    return {
        QStringLiteral("/videos/penalty_corner_006_27m47s-29m42s_p73.mp4"),
        QStringLiteral("/videos/penalty_corner_005_19m59s-25m14s_p74.mp4"),
        QStringLiteral("/videos/penalty_corner_004_15m31s-17m34s_p63.mp4"),
        QStringLiteral("/videos/penalty_corner_003_10m55s-11m54s_p72.mp4"),
    };
}

QStringList penaltyCornerPathsChronological() {
    QStringList paths = penaltyCornerPathsReversed();
    std::reverse(paths.begin(), paths.end());
    return paths;
}

class LanguageGuard {
public:
    explicit LanguageGuard(AppLocale::Language language)
        : previous_(AppLocale::currentLanguage()) {
        AppLocale::setLanguage(language);
    }
    ~LanguageGuard() { AppLocale::setLanguage(previous_); }

private:
    AppLocale::Language previous_;
};

QString themeStylesheet() {
    QFile file(QString::fromUtf8(AVA_THEME_QSS_PATH));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

void settle(QWidget* widget) {
    widget->ensurePolished();
    widget->show();
    if (QLayout* layout = widget->layout()) layout->activate();
    QCoreApplication::processEvents();
    QTest::qWait(20);
}

QRect mappedRect(const QWidget* widget, const QWidget* ancestor) {
    return QRect(widget->mapTo(ancestor, QPoint(0, 0)), widget->size());
}

QVector<QPushButton*> buttonsNamed(QWidget* root, const char* name) {
    const QList<QPushButton*> found = root->findChildren<QPushButton*>(QString::fromUtf8(name));
    QVector<QPushButton*> buttons;
    buttons.reserve(found.size());
    for (QPushButton* button : found) buttons.append(button);
    std::sort(buttons.begin(), buttons.end(), [](const QPushButton* left, const QPushButton* right) {
        return left->property("rowIndex").toInt() < right->property("rowIndex").toInt();
    });
    return buttons;
}

QFrame* rowWithPath(QWidget* root, const QString& fragment) {
    const QList<QFrame*> rows = root->findChildren<QFrame*>(QStringLiteral("clipRow"));
    for (QFrame* row : rows) {
        if (row->property("filePath").toString().contains(fragment)) return row;
    }
    return nullptr;
}

}  // namespace

class VideoClipSequenceTest : public QObject {
    Q_OBJECT

private slots:
    void parsesPenaltyCornerFilename();
    void parsesCaseAndKnownExtension();
    void parsesHoursMinutesAndSeconds();
    void keepsConceptDigitsOutOfTheSequenceNumber();
    void sentenceCasesDiacriticsEmojiAndMarkup();
    void rejectsIncompleteOrOverflowingTimes();
    void fallsBackToTheRawFileName();
    void sortsChronologicallyAndKeepsUntimedClips();
    void moveClipUsesFinalIndex();
    void dropIndexAccountsForRemoval();
    void totalDurationRequiresEverySpan();
    void fileLabelDisambiguatesDuplicateNames();
    void stressSetParsesWithoutLosingIdentity();
    void sortsOneThousandClips();
};

void VideoClipSequenceTest::parsesPenaltyCornerFilename() {
    const VideoClip clip = VideoClipSequence::clipFromFilePath(
        QStringLiteral("/Users/coach/Videos/penalty_corner_003_10m55s-11m54s_p72.mp4"));
    QVERIFY(clip.parsed);
    QCOMPARE(clip.fileName, QStringLiteral("penalty_corner_003_10m55s-11m54s_p72.mp4"));
    QCOMPARE(clip.concept, QStringLiteral("penalty_corner"));
    QCOMPARE(clip.sequenceLabel, QStringLiteral("3"));
    QCOMPARE(clip.title(), QStringLiteral("Penalty corner 3"));
    QCOMPARE(clip.timeRangeText(), QStringLiteral("10m55s\u201311m54s"));
    QCOMPARE(clip.startMs, (10 * 60 + 55) * 1000);
    QCOMPARE(clip.endMs, (11 * 60 + 54) * 1000);
    QVERIFY(clip.hasDuration());
    QCOMPARE(clip.durationMs(), 59 * 1000);
}

void VideoClipSequenceTest::parsesCaseAndKnownExtension() {
    const VideoClip clip = VideoClipSequence::clipFromFilePath(
        QStringLiteral("/v/PENALTY_CORNER_003_10M55S-11m54s_P72.MOV"));
    QVERIFY(clip.parsed);
    QCOMPARE(clip.title(), QStringLiteral("Penalty corner 3"));
    QCOMPARE(clip.timeRangeText(), QStringLiteral("10m55s\u201311m54s"));
}

void VideoClipSequenceTest::parsesHoursMinutesAndSeconds() {
    QCOMPARE(VideoClipSequence::compactClock(0), QStringLiteral("0s"));
    QCOMPARE(VideoClipSequence::compactClock(1000), QStringLiteral("1s"));
    QCOMPARE(VideoClipSequence::compactClock(59 * 1000), QStringLiteral("59s"));
    QCOMPARE(VideoClipSequence::compactClock(60 * 1000), QStringLiteral("1m00s"));
    QCOMPARE(VideoClipSequence::compactClock((10 * 60 + 55) * 1000), QStringLiteral("10m55s"));
    QCOMPARE(VideoClipSequence::compactClock((3600 + 2 * 60 + 3) * 1000), QStringLiteral("1h02m03s"));
    QCOMPARE(VideoClipSequence::compactClock(-1), QString());

    const VideoClip hours = VideoClipSequence::clipFromFilePath(
        QStringLiteral("match_001_1h02m03s-1h10m00s_p1.mp4"));
    QVERIFY(hours.parsed);
    QCOMPARE(hours.timeRangeText(), QStringLiteral("1h02m03s\u20131h10m00s"));
    QCOMPARE(hours.durationMs(), (8 * 60 - 3) * 1000);

    const VideoClip minutesOnly =
        VideoClipSequence::clipFromFilePath(QStringLiteral("clip_001_10m-11m_p1.mp4"));
    QVERIFY(minutesOnly.parsed);
    QCOMPARE(minutesOnly.timeRangeText(), QStringLiteral("10m00s\u201311m00s"));

    const VideoClip secondsOnly =
        VideoClipSequence::clipFromFilePath(QStringLiteral("clip_002_5s-45s_p1.webm"));
    QVERIFY(secondsOnly.parsed);
    QCOMPARE(secondsOnly.title(), QStringLiteral("Clip 2"));
    QCOMPARE(secondsOnly.timeRangeText(), QStringLiteral("5s\u201345s"));
}

void VideoClipSequenceTest::keepsConceptDigitsOutOfTheSequenceNumber() {
    const VideoClip clip = VideoClipSequence::clipFromFilePath(
        QStringLiteral("game_2024_003_10m00s-11m00s_p1.mp4"));
    QVERIFY(clip.parsed);
    QCOMPARE(clip.concept, QStringLiteral("game_2024"));
    QCOMPARE(clip.sequenceLabel, QStringLiteral("3"));
    QCOMPARE(clip.title(), QStringLiteral("Game 2024 3"));

    const VideoClip hyphenated = VideoClipSequence::clipFromFilePath(
        QString::fromUtf8("wiśniewska-kowalczyk_002_3m00s-4m00s_p1.m4v"));
    QVERIFY(hyphenated.parsed);
    QCOMPARE(hyphenated.title(), QString::fromUtf8("Wiśniewska-kowalczyk 2"));
}

void VideoClipSequenceTest::sentenceCasesDiacriticsEmojiAndMarkup() {
    const VideoClip vietnamese = VideoClipSequence::clipFromFilePath(
        QString::fromUtf8("/matches/vn/đặng_thị_ngọc_hân_008_0m30s-1m00s_p4.mp4"));
    QVERIFY(vietnamese.parsed);
    QCOMPARE(vietnamese.title(), QString::fromUtf8("Đặng thị ngọc hân 8"));

    const VideoClip emoji = VideoClipSequence::clipFromFilePath(
        QString::fromUtf8("/matches/emoji/🦊_fox_005_1m00s-1m00s_p9.mp4"));
    QVERIFY(emoji.parsed);
    QCOMPARE(emoji.title(), QString::fromUtf8("🦊 fox 5"));
    QVERIFY(emoji.hasDuration());
    QCOMPARE(emoji.durationMs(), 0);

    // A slash inside "</b>" is a path separator, so the file name is the last
    // segment. Angle brackets that stay in that segment remain literal text.
    const VideoClip markup = VideoClipSequence::clipFromFilePath(
        QStringLiteral("/matches/markup/<b>bold</b>_012_8m00s-9m00s_p3.mp4"));
    QVERIFY(markup.parsed);
    QCOMPARE(markup.fileName, QStringLiteral("b>_012_8m00s-9m00s_p3.mp4"));
    QCOMPARE(markup.title(), QStringLiteral("B> 12"));

    const VideoClip literalMarkup = VideoClipSequence::clipFromFilePath(
        QStringLiteral("/matches/markup/<b>bold<b>_012_8m00s-9m00s_p3.mp4"));
    QVERIFY(literalMarkup.parsed);
    QCOMPARE(literalMarkup.title(), QStringLiteral("<b>bold<b> 12"));

    const VideoClip script = VideoClipSequence::clipFromFilePath(
        QStringLiteral("/matches/script/<script>alert(1)</script>_016_30m00s-31m00s_p7.mp4"));
    QVERIFY(script.parsed);
    QCOMPARE(script.fileName, QStringLiteral("script>_016_30m00s-31m00s_p7.mp4"));
    QCOMPARE(script.title(), QStringLiteral("Script> 16"));

    const VideoClip literalScript = VideoClipSequence::clipFromFilePath(
        QStringLiteral("/matches/script/<script>alert(1)<script>_016_30m00s-31m00s_p7.mp4"));
    QVERIFY(literalScript.parsed);
    QVERIFY(literalScript.title().contains(QStringLiteral("<script>alert(1)<script>")));

    const VideoClip ampersand = VideoClipSequence::clipFromFilePath(
        QStringLiteral("/matches/entity/club_&amp;_001_1m00s-2m00s_p1.mp4"));
    QVERIFY(ampersand.parsed);
    QCOMPARE(ampersand.title(), QStringLiteral("Club &amp; 1"));

    const VideoClip arabic = VideoClipSequence::clipFromFilePath(
        QString::fromUtf8("/matches/rtl/نور_الهدى_011_6m00s-7m00s_p2.mp4"));
    QVERIFY(arabic.parsed);
    QCOMPARE(arabic.title(), QString::fromUtf8("نور الهدى 11"));
}

void VideoClipSequenceTest::rejectsIncompleteOrOverflowingTimes() {
    const VideoClip missingSuffix = VideoClipSequence::clipFromFilePath(
        QStringLiteral("clip_001_10m55-11m54s_p1.mp4"));
    QVERIFY(!missingSuffix.parsed);
    QCOMPARE(missingSuffix.title(), QStringLiteral("clip_001_10m55-11m54s_p1.mp4"));

    const VideoClip missingPart = VideoClipSequence::clipFromFilePath(
        QStringLiteral("penalty_corner_003_10m55s-11m54s.mp4"));
    QVERIFY(!missingPart.parsed);

    const VideoClip reversed = VideoClipSequence::clipFromFilePath(
        QStringLiteral("/matches/bad/clip_013_12m00s-11m00s_p1.mp4"));
    QVERIFY(reversed.parsed);
    QVERIFY(reversed.hasStart);
    QVERIFY(!reversed.hasDuration());
    QCOMPARE(reversed.timeRangeText(), QStringLiteral("12m00s\u201311m00s"));

    const VideoClip overflow = VideoClipSequence::clipFromFilePath(
        QStringLiteral("clip_001_999999999999999h00m00s-1s_p1.mp4"));
    QVERIFY(!overflow.parsed);
    QCOMPARE(overflow.title(), QStringLiteral("clip_001_999999999999999h00m00s-1s_p1.mp4"));
}

void VideoClipSequenceTest::fallsBackToTheRawFileName() {
    const VideoClip notes = VideoClipSequence::clipFromFilePath(QStringLiteral("/tmp/notes.mp4"));
    QVERIFY(!notes.parsed);
    QCOMPARE(notes.title(), QStringLiteral("notes.mp4"));
    QVERIFY(notes.timeRangeText().isEmpty());

    const VideoClip camera = VideoClipSequence::clipFromFilePath(
        QStringLiteral("/matches/camera/IMG_20250914_183022_HDR_portrait_edited_edited.HEIC"));
    QVERIFY(!camera.parsed);
    QCOMPARE(camera.title(), QStringLiteral("IMG_20250914_183022_HDR_portrait_edited_edited.HEIC"));

    const VideoClip deck = VideoClipSequence::clipFromFilePath(
        QStringLiteral("/matches/deck/Q3 Board Deck — FINAL (revised) v12 [approved by legal].mp4"));
    QVERIFY(!deck.parsed);
    QCOMPARE(deck.title(),
             QStringLiteral("Q3 Board Deck — FINAL (revised) v12 [approved by legal].mp4"));

    const VideoClip brokenLine =
        VideoClipSequence::clipFromFilePath(QString::fromUtf8("/matches/odd/line_one\nline_two.mp4"));
    QVERIFY(!brokenLine.parsed);
    QCOMPARE(brokenLine.title(), QStringLiteral("line_one line_two.mp4"));
    QVERIFY(!brokenLine.title().contains(QLatin1Char('\n')));

    const VideoClip empty = VideoClipSequence::clipFromFilePath(QString());
    QVERIFY(!empty.parsed);
    QCOMPARE(empty.title(), QString());
}

void VideoClipSequenceTest::sortsChronologicallyAndKeepsUntimedClips() {
    const QStringList input = {
        QStringLiteral("/v/notes.mp4"),
        QStringLiteral("/v/penalty_corner_006_27m47s-29m42s_p73.mp4"),
        QStringLiteral("/v/same_002_5m00s-6m00s_p2.mp4"),
        QStringLiteral("/v/penalty_corner_003_10m55s-11m54s_p72.mp4"),
        QStringLiteral("/v/same_001_5m00s-6m00s_p1.mp4"),
        QStringLiteral("/v/later-notes.mp4"),
    };
    const QVector<VideoClip> ordered =
        VideoClipSequence::chronologicalOrder(VideoClipSequence::clipsFromFilePaths(input));
    const QStringList paths = VideoClipSequence::filePathsOf(ordered);
    QCOMPARE(paths, QStringList({
        QStringLiteral("/v/same_002_5m00s-6m00s_p2.mp4"),
        QStringLiteral("/v/same_001_5m00s-6m00s_p1.mp4"),
        QStringLiteral("/v/penalty_corner_003_10m55s-11m54s_p72.mp4"),
        QStringLiteral("/v/penalty_corner_006_27m47s-29m42s_p73.mp4"),
        QStringLiteral("/v/notes.mp4"),
        QStringLiteral("/v/later-notes.mp4"),
    }));
    QVERIFY(VideoClipSequence::isChronological(ordered));

    // Equal start times stay chronological in either order. A later start
    // moved ahead of an earlier one does not.
    QVector<VideoClip> tied = ordered;
    QVERIFY(VideoClipSequence::moveClip(tied, 0, 1));
    QVERIFY(VideoClipSequence::isChronological(tied));

    QVector<VideoClip> swapped = ordered;
    QVERIFY(VideoClipSequence::moveClip(swapped, 0, 2));
    QVERIFY(!VideoClipSequence::isChronological(swapped));
    QVERIFY(VideoClipSequence::isChronological(VideoClipSequence::chronologicalOrder(swapped)));
    QVERIFY(VideoClipSequence::isChronological({}));
}

void VideoClipSequenceTest::moveClipUsesFinalIndex() {
    QVector<VideoClip> clips = VideoClipSequence::clipsFromFilePaths(
        {QStringLiteral("/v/a.mp4"), QStringLiteral("/v/b.mp4"), QStringLiteral("/v/c.mp4"),
         QStringLiteral("/v/d.mp4")});
    QVERIFY(VideoClipSequence::moveClip(clips, 0, 3));
    QCOMPARE(VideoClipSequence::filePathsOf(clips),
             QStringList({QStringLiteral("/v/b.mp4"), QStringLiteral("/v/c.mp4"),
                          QStringLiteral("/v/d.mp4"), QStringLiteral("/v/a.mp4")}));

    QVERIFY(VideoClipSequence::moveClip(clips, 3, 0));
    QCOMPARE(clips.first().filePath, QStringLiteral("/v/a.mp4"));

    const QStringList before = VideoClipSequence::filePathsOf(clips);
    QVERIFY(VideoClipSequence::moveClip(clips, 1, 1));
    QCOMPARE(VideoClipSequence::filePathsOf(clips), before);
    QVERIFY(!VideoClipSequence::moveClip(clips, -1, 0));
    QVERIFY(!VideoClipSequence::moveClip(clips, 0, 4));
    QCOMPARE(VideoClipSequence::filePathsOf(clips), before);
}

void VideoClipSequenceTest::dropIndexAccountsForRemoval() {
    QCOMPARE(VideoClipSequence::finalIndexForDrop(0, 3, 4), 2);
    QCOMPARE(VideoClipSequence::finalIndexForDrop(0, 4, 4), 3);
    QCOMPARE(VideoClipSequence::finalIndexForDrop(3, 0, 4), 0);
    QCOMPARE(VideoClipSequence::finalIndexForDrop(2, 2, 4), 2);
    QCOMPARE(VideoClipSequence::finalIndexForDrop(2, 3, 4), 2);
    QCOMPARE(VideoClipSequence::finalIndexForDrop(1, 0, 3), 0);
    QCOMPARE(VideoClipSequence::finalIndexForDrop(-1, 0, 4), -1);
    QCOMPARE(VideoClipSequence::finalIndexForDrop(0, 0, 0), -1);

    QVector<VideoClip> clips = VideoClipSequence::clipsFromFilePaths(
        {QStringLiteral("/v/a.mp4"), QStringLiteral("/v/b.mp4"), QStringLiteral("/v/c.mp4"),
         QStringLiteral("/v/d.mp4")});
    const int target = VideoClipSequence::finalIndexForDrop(0, 3, clips.size());
    QVERIFY(VideoClipSequence::moveClip(clips, 0, target));
    QCOMPARE(VideoClipSequence::filePathsOf(clips),
             QStringList({QStringLiteral("/v/b.mp4"), QStringLiteral("/v/c.mp4"),
                          QStringLiteral("/v/a.mp4"), QStringLiteral("/v/d.mp4")}));
}

void VideoClipSequenceTest::totalDurationRequiresEverySpan() {
    const QVector<VideoClip> corners = VideoClipSequence::chronologicalOrder(
        VideoClipSequence::clipsFromFilePaths(penaltyCornerPathsReversed()));
    const std::optional<qint64> total = VideoClipSequence::totalDurationMs(corners);
    QVERIFY(total.has_value());
    QCOMPARE(*total, 612 * 1000);

    QVector<VideoClip> withGap = corners;
    withGap.append(VideoClipSequence::clipFromFilePath(QStringLiteral("/v/notes.mp4")));
    QVERIFY(!VideoClipSequence::totalDurationMs(withGap).has_value());

    QVector<VideoClip> reversedSpan = {
        VideoClipSequence::clipFromFilePath(QStringLiteral("/v/clip_013_12m00s-11m00s_p1.mp4"))};
    QVERIFY(!VideoClipSequence::totalDurationMs(reversedSpan).has_value());
    QVERIFY(!VideoClipSequence::totalDurationMs({}).has_value());

    const QVector<VideoClip> zero = {
        VideoClipSequence::clipFromFilePath(QStringLiteral("/v/whistle_001_1m00s-1m00s_p1.mp4"))};
    QCOMPARE(VideoClipSequence::totalDurationMs(zero), std::optional<qint64>(0));
}

void VideoClipSequenceTest::fileLabelDisambiguatesDuplicateNames() {
    const QVector<VideoClip> clips = VideoClipSequence::clipsFromFilePaths(
        {QStringLiteral("/matches/cam-b/penalty_corner_004_2m00s-3m30s_p63.mp4"),
         QStringLiteral("/matches/cam-a/penalty_corner_004_2m00s-3m30s_p63.mp4"),
         QStringLiteral("/matches/only/penalty_corner_003_10m55s-11m54s_p72.mp4")});
    QCOMPARE(VideoClipSequence::fileLabel(clips, 0),
             QStringLiteral("cam-b/penalty_corner_004_2m00s-3m30s_p63.mp4"));
    QCOMPARE(VideoClipSequence::fileLabel(clips, 1),
             QStringLiteral("cam-a/penalty_corner_004_2m00s-3m30s_p63.mp4"));
    QCOMPARE(VideoClipSequence::fileLabel(clips, 2),
             QStringLiteral("penalty_corner_003_10m55s-11m54s_p72.mp4"));
    QCOMPARE(VideoClipSequence::fileLabel(clips, 9), QString());
}

void VideoClipSequenceTest::stressSetParsesWithoutLosingIdentity() {
    const QStringList input = VideoClipSequence::stressFilePaths();
    const QVector<VideoClip> clips = VideoClipSequence::clipsFromFilePaths(input);
    QCOMPARE(clips.size(), input.size());
    for (int index = 0; index < clips.size(); ++index) {
        QCOMPARE(clips.at(index).filePath, input.at(index));
        QVERIFY(!clips.at(index).title().contains(QLatin1Char('\n')));
        QVERIFY(!clips.at(index).title().contains(QChar(0x200B)));
    }

    const QStringList ordered =
        VideoClipSequence::filePathsOf(VideoClipSequence::chronologicalOrder(clips));
    QCOMPARE(ordered, QStringList({
        QStringLiteral("/matches/short/a_001_0m01s-0m02s_p1.mp4"),
        QString::fromUtf8("/matches/vn/đặng_thị_ngọc_hân_008_0m30s-1m00s_p4.mp4"),
        QString::fromUtf8("/matches/emoji/🦊_fox_005_1m00s-1m00s_p9.mp4"),
        QStringLiteral("/matches/cam-b/penalty_corner_004_2m00s-3m30s_p63.mp4"),
        QStringLiteral("/matches/cam-a/penalty_corner_004_2m00s-3m30s_p63.mp4"),
        QString::fromUtf8("/matches/long/aleksandra_wiśniewska-kowalczyk_categoría_sub_17_010_4m00s-5m15s_p72.mp4"),
        QString::fromUtf8("/matches/rtl/نور_الهدى_011_6m00s-7m00s_p2.mp4"),
        QStringLiteral("/matches/markup/<b>bold</b>_012_8m00s-9m00s_p3.mp4"),
        QStringLiteral("/matches/bad/clip_013_12m00s-11m00s_p1.mp4"),
        QStringLiteral("/matches/script/<script>alert(1)</script>_016_30m00s-31m00s_p7.mp4"),
        QStringLiteral("/matches/hours/archivo_015_1h05m00s-1h08m30s_p1.mp4"),
        QStringLiteral("/matches/inbox/bartholomew.fitzgerald@northwind-industries-holdings.example.com.mp4"),
        QStringLiteral("/matches/camera/IMG_20250914_183022_HDR_portrait_edited_edited.HEIC"),
        QStringLiteral("/matches/deck/Q3 Board Deck — FINAL (revised) v12 [approved by legal].mp4"),
        QString::fromUtf8("/matches/odd/line_one\nline_two.mp4"),
        QStringLiteral("/matches/blank/untitled.mp4"),
    }));
    QVERIFY(!VideoClipSequence::totalDurationMs(VideoClipSequence::chronologicalOrder(clips)).has_value());
}

void VideoClipSequenceTest::sortsOneThousandClips() {
    QStringList paths;
    paths.reserve(1000);
    for (int index = 0; index < 1000; ++index) {
        const int startMinute = 1000 - index;
        paths.append(QStringLiteral("/v/clip_%1_%2m00s-%3m00s_p1.mp4")
                         .arg(index, 3, 10, QChar(u'0'))
                         .arg(startMinute)
                         .arg(startMinute + 1));
    }
    const QVector<VideoClip> ordered =
        VideoClipSequence::chronologicalOrder(VideoClipSequence::clipsFromFilePaths(paths));
    QCOMPARE(ordered.size(), 1000);
    QVERIFY(ordered.first().startMs < ordered.last().startMs);
    QVERIFY(VideoClipSequence::isChronological(ordered));
    for (int index = 1; index < ordered.size(); ++index) {
        QVERIFY(ordered.at(index - 1).startMs <= ordered.at(index).startMs);
    }
}

class ConcatFileOrderDialogTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void opensInChronologicalOrder();
    void moveButtonsAndKeyboardReorderThenReset();
    void singularEmptyAndSpanishCopy();
    void worstCaseKeepsTextReadableAndActionsPinned();
    void largeCountUsesGroupingAndStillScrolls();
    void fixtureToggleStaysHiddenUnlessRequested();

private:
    QString previousStyleSheet_;
};

void ConcatFileOrderDialogTest::init() {
    previousStyleSheet_ = qApp->styleSheet();
    const QString stylesheet = themeStylesheet();
    QVERIFY(!stylesheet.isEmpty());
    qApp->setStyleSheet(stylesheet);
    AppLocale::setLanguage(AppLocale::Language::English);
}

void ConcatFileOrderDialogTest::cleanup() {
    qApp->setStyleSheet(previousStyleSheet_);
    qunsetenv("AVA_CONCAT_ORDER_FIXTURES");
    AppLocale::setLanguage(AppLocale::Language::English);
}

void ConcatFileOrderDialogTest::opensInChronologicalOrder() {
    ConcatFileOrderDialog dialog(penaltyCornerPathsReversed());
    QCOMPARE(dialog.orderedFilePaths(), penaltyCornerPathsChronological());
    settle(&dialog);

    QCOMPARE(dialog.windowTitle(), QStringLiteral("Arrange Video Files"));
    auto* headline = dialog.findChild<QLabel*>(QStringLiteral("concatHeadline"));
    auto* detail = dialog.findChild<QLabel*>(QStringLiteral("concatOutputDetail"));
    QVERIFY(headline);
    QVERIFY(detail);
    QCOMPARE(headline->text(), QStringLiteral("Join 4 clips into one video"));
    QCOMPARE(detail->text(), QStringLiteral("4 clips · 10 min 12 s"));

    const QList<QLabel*> indexes = dialog.findChildren<QLabel*>(QStringLiteral("clipIndex"));
    QCOMPARE(indexes.size(), 4);
    QStringList numbers;
    for (const QLabel* index : indexes) numbers.append(index->text());
    std::sort(numbers.begin(), numbers.end());
    QCOMPARE(numbers, QStringList({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                                   QStringLiteral("4")}));

    QFrame* first = rowWithPath(&dialog, QStringLiteral("penalty_corner_003"));
    QVERIFY(first);
    QCOMPARE(first->toolTip(), penaltyCornerPathsChronological().first());
    auto* title = first->findChild<QLabel*>(QStringLiteral("clipTitle"));
    QVERIFY(title);
    QCOMPARE(title->text(), QStringLiteral("Penalty corner 3"));
    QCOMPARE(title->textFormat(), Qt::PlainText);
    QVERIFY(first->property("selected").toBool());
}

void ConcatFileOrderDialogTest::moveButtonsAndKeyboardReorderThenReset() {
    ConcatFileOrderDialog dialog(penaltyCornerPathsReversed());
    settle(&dialog);
    dialog.resize(dialog.minimumSize().expandedTo(QSize(520, 640)));
    QTest::qWait(20);

    const QVector<QPushButton*> moveDown = buttonsNamed(&dialog, "clipMoveDown");
    QCOMPARE(moveDown.size(), 4);
    QVERIFY(!buttonsNamed(&dialog, "clipMoveUp").first()->isEnabled());
    QVERIFY(moveDown.first()->isEnabled());
    QVERIFY(!moveDown.last()->isEnabled());

    QTest::mouseClick(moveDown.first(), Qt::LeftButton);
    QTest::qWait(30);
    QCOMPARE(dialog.orderedFilePaths().at(0), penaltyCornerPathsChronological().at(1));
    QCOMPARE(dialog.orderedFilePaths().at(1), penaltyCornerPathsChronological().at(0));

    auto* resetButton = dialog.findChild<QPushButton*>(QStringLiteral("concatReset"));
    QVERIFY(resetButton);
    QVERIFY(resetButton->isEnabled());
    QTest::mouseClick(resetButton, Qt::LeftButton);
    QTest::qWait(20);
    QCOMPARE(dialog.orderedFilePaths(), penaltyCornerPathsChronological());
    QVERIFY(!resetButton->isEnabled());

    const QVector<QPushButton*> moveDownAgain = buttonsNamed(&dialog, "clipMoveDown");
    QVERIFY(!moveDownAgain.isEmpty());
    moveDownAgain.first()->setFocus();
    QTest::keyClick(moveDownAgain.first(), Qt::Key_Down, Qt::ControlModifier);
    QTest::qWait(30);
    QCOMPARE(dialog.orderedFilePaths().at(0), penaltyCornerPathsChronological().at(1));
}

void ConcatFileOrderDialogTest::singularEmptyAndSpanishCopy() {
    {
        LanguageGuard spanish(AppLocale::Language::Spanish);
        ConcatFileOrderDialog dialog(penaltyCornerPathsReversed());
        auto* headline = dialog.findChild<QLabel*>(QStringLiteral("concatHeadline"));
        auto* resetButton = dialog.findChild<QPushButton*>(QStringLiteral("concatReset"));
        auto* subtitle = dialog.findChild<QLabel*>(QStringLiteral("concatSubtitle"));
        QVERIFY(headline);
        QVERIFY(resetButton);
        QVERIFY(subtitle);
        QCOMPARE(headline->text(), QStringLiteral("Unir 4 clips en un solo video"));
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Ordenar archivos de video"));
        QCOMPARE(resetButton->text(), QStringLiteral("Restablecer orden cronológico"));
        QVERIFY(subtitle->text().contains(QStringLiteral("flechas")));
    }

    ConcatFileOrderDialog one({QStringLiteral("/videos/penalty_corner_003_10m55s-11m54s_p72.mp4")});
    auto* oneHeadline = one.findChild<QLabel*>(QStringLiteral("concatHeadline"));
    auto* oneDetail = one.findChild<QLabel*>(QStringLiteral("concatOutputDetail"));
    QVERIFY(oneHeadline);
    QVERIFY(oneDetail);
    QCOMPARE(oneHeadline->text(), QStringLiteral("Join 1 clip into one video"));
    QCOMPARE(oneDetail->text(), QStringLiteral("1 clip · 59 s"));
    QVERIFY(one.findChild<QPushButton*>(QStringLiteral("concatContinue"))->isEnabled());
    QVERIFY(!buttonsNamed(&one, "clipMoveUp").first()->isEnabled());
    QVERIFY(!buttonsNamed(&one, "clipMoveDown").first()->isEnabled());
    QVERIFY(!one.findChild<QPushButton*>(QStringLiteral("concatReset"))->isEnabled());

    ConcatFileOrderDialog empty({});
    QCOMPARE(empty.findChild<QLabel*>(QStringLiteral("concatHeadline"))->text(),
             QStringLiteral("No clips to join"));
    QCOMPARE(empty.findChild<QLabel*>(QStringLiteral("concatOutputDetail"))->text(),
             QStringLiteral("Nothing to join"));
    QVERIFY(!empty.findChild<QPushButton*>(QStringLiteral("concatContinue"))->isEnabled());
    QVERIFY(!empty.findChild<QPushButton*>(QStringLiteral("concatReset"))->isEnabled());
    QCOMPARE(empty.findChildren<QFrame*>(QStringLiteral("clipRow")).size(), 0);
}

void ConcatFileOrderDialogTest::worstCaseKeepsTextReadableAndActionsPinned() {
    ConcatFileOrderDialog dialog(VideoClipSequence::stressFilePaths());
    settle(&dialog);
    dialog.resize(dialog.minimumSize().expandedTo(QSize(480, 560)));
    QTest::qWait(30);

    QVERIFY(dialog.minimumWidth() >= 480);
    QVERIFY(dialog.minimumHeight() >= 560);
    QVERIFY(dialog.findChild<QWidget*>(QStringLiteral("fixtureToggle")) == nullptr);

    auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("concatScroll"));
    auto* canvas = dialog.findChild<QWidget*>(QStringLiteral("clipListCanvas"));
    auto* output = dialog.findChild<QWidget*>(QStringLiteral("concatOutput"));
    auto* continueButton = dialog.findChild<QPushButton*>(QStringLiteral("concatContinue"));
    auto* cancelButton = dialog.findChild<QPushButton*>(QStringLiteral("concatCancel"));
    auto* resetButton = dialog.findChild<QPushButton*>(QStringLiteral("concatReset"));
    QVERIFY(scroll);
    QVERIFY(canvas);
    QVERIFY(output);
    QVERIFY(continueButton);
    QVERIFY(cancelButton);
    QVERIFY(resetButton);

    const QRect dialogRect = dialog.rect();
    QVERIFY(dialogRect.contains(mappedRect(continueButton, &dialog)));
    QVERIFY(dialogRect.contains(mappedRect(cancelButton, &dialog)));
    QVERIFY(dialogRect.contains(mappedRect(resetButton, &dialog)));
    QVERIFY(dialogRect.contains(mappedRect(output, &dialog)));
    QVERIFY(mappedRect(continueButton, &dialog).top() >= mappedRect(scroll, &dialog).bottom() - 1);
    QVERIFY(mappedRect(output, &dialog).top() >= mappedRect(scroll, &dialog).bottom() - 1);
    QCOMPARE(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QVERIFY(canvas->height() > scroll->viewport()->height());

    QFrame* email = rowWithPath(&dialog, QStringLiteral("bartholomew.fitzgerald"));
    QVERIFY(email);
    auto* emailTitle = email->findChild<QLabel*>(QStringLiteral("clipTitle"));
    QVERIFY(emailTitle);
    QCOMPARE(emailTitle->textFormat(), Qt::PlainText);
    QCOMPARE(emailTitle->text(),
             QStringLiteral("bartholomew.fitzgerald@northwind-industries-holdings.example.com.mp4"));
    QVERIFY(emailTitle->height() >= emailTitle->heightForWidth(emailTitle->width()));
    QVERIFY(emailTitle->height() > emailTitle->fontMetrics().lineSpacing());
    QCOMPARE(email->toolTip(),
             QStringLiteral("/matches/inbox/bartholomew.fitzgerald@northwind-industries-holdings.example.com.mp4"));

    const QString markupPath =
        QStringLiteral("/matches/markup/<b>bold</b>_012_8m00s-9m00s_p3.mp4");
    QFrame* markup = rowWithPath(&dialog, QStringLiteral("<b>bold</b>"));
    QVERIFY(markup);
    auto* markupTitle = markup->findChild<QLabel*>(QStringLiteral("clipTitle"));
    QVERIFY(markupTitle);
    QCOMPARE(markupTitle->textFormat(), Qt::PlainText);
    QCOMPARE(markupTitle->text(), QStringLiteral("B> 12"));
    QCOMPARE(markup->toolTip(), markupPath.toHtmlEscaped());
    QVERIFY(markup->toolTip().contains(QStringLiteral("&lt;b&gt;bold")));

    const QString scriptPath =
        QStringLiteral("/matches/script/<script>alert(1)</script>_016_30m00s-31m00s_p7.mp4");
    QFrame* script = rowWithPath(&dialog, QStringLiteral("<script>alert(1)</script>"));
    QVERIFY(script);
    auto* scriptTitle = script->findChild<QLabel*>(QStringLiteral("clipTitle"));
    QVERIFY(scriptTitle);
    QCOMPARE(scriptTitle->textFormat(), Qt::PlainText);
    QCOMPARE(scriptTitle->text(), QStringLiteral("Script> 16"));
    QCOMPARE(script->toolTip(), scriptPath.toHtmlEscaped());
    QVERIFY(script->toolTip().contains(QStringLiteral("&lt;script&gt;alert(1)")));

    QFrame* vietnamese = rowWithPath(&dialog, QString::fromUtf8("đặng_thị"));
    QVERIFY(vietnamese);
    auto* vietnameseTitle = vietnamese->findChild<QLabel*>(QStringLiteral("clipTitle"));
    QVERIFY(vietnameseTitle);
    QCOMPARE(vietnameseTitle->text(), QString::fromUtf8("Đặng thị ngọc hân 8"));
    QVERIFY(vietnameseTitle->height() >= vietnameseTitle->fontMetrics().lineSpacing());

    QFrame* duplicate = rowWithPath(&dialog, QStringLiteral("/matches/cam-a/"));
    QVERIFY(duplicate);
    auto* duplicateFile = duplicate->findChild<QLabel*>(QStringLiteral("clipFile"));
    QVERIFY(duplicateFile);
    QCOMPARE(duplicateFile->text(), QStringLiteral("cam-a/penalty_corner_004_2m00s-3m30s_p63.mp4"));

    QFrame* brokenLine = rowWithPath(&dialog, QStringLiteral("line_one"));
    QVERIFY(brokenLine);
    auto* brokenTitle = brokenLine->findChild<QLabel*>(QStringLiteral("clipTitle"));
    QVERIFY(brokenTitle);
    QVERIFY(!brokenTitle->text().contains(QLatin1Char('\n')));

    const QVector<QPushButton*> moveUp = buttonsNamed(&dialog, "clipMoveUp");
    const QVector<QPushButton*> moveDown = buttonsNamed(&dialog, "clipMoveDown");
    QCOMPARE(moveUp.size(), VideoClipSequence::stressFilePaths().size());
    QVERIFY(!moveUp.first()->isEnabled());
    QVERIFY(moveUp.last()->isEnabled());
    QVERIFY(moveDown.first()->isEnabled());
    QVERIFY(!moveDown.last()->isEnabled());

    QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("concatOutputDetail"))->text(),
             QStringLiteral("16 clips"));
}

void ConcatFileOrderDialogTest::largeCountUsesGroupingAndStillScrolls() {
    QStringList paths;
    constexpr int count = 1284;
    paths.reserve(count);
    for (int index = 0; index < count; ++index) {
        paths.append(QStringLiteral("/v/segment_%1.mp4").arg(index));
    }
    ConcatFileOrderDialog dialog(paths);
    settle(&dialog);
    dialog.resize(QSize(520, 640));
    QTest::qWait(20);

    const QString expectedCount = QLocale(QLocale::English).toString(count);
    auto* headline = dialog.findChild<QLabel*>(QStringLiteral("concatHeadline"));
    auto* detail = dialog.findChild<QLabel*>(QStringLiteral("concatOutputDetail"));
    QVERIFY(headline);
    QVERIFY(detail);
    QCOMPARE(headline->text(), QStringLiteral("Join %1 clips into one video").arg(expectedCount));
    QCOMPARE(detail->text(), QStringLiteral("%1 clips").arg(expectedCount));
    QVERIFY(!expectedCount.isEmpty());
    QVERIFY(expectedCount != QString::number(count));

    auto* continueButton = dialog.findChild<QPushButton*>(QStringLiteral("concatContinue"));
    auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("concatScroll"));
    QVERIFY(continueButton);
    QVERIFY(scroll);
    QVERIFY(dialog.rect().contains(mappedRect(continueButton, &dialog)));
    QVERIFY(scroll->verticalScrollBar()->maximum() > 0);

    const QList<QLabel*> indexes = dialog.findChildren<QLabel*>(QStringLiteral("clipIndex"));
    QLabel* lastIndex = nullptr;
    for (QLabel* index : indexes) {
        if (index->text() == QString::number(count)) lastIndex = index;
    }
    QVERIFY(lastIndex);
    QVERIFY(lastIndex->width() >= lastIndex->fontMetrics().horizontalAdvance(lastIndex->text()));
}

void ConcatFileOrderDialogTest::fixtureToggleStaysHiddenUnlessRequested() {
    ConcatFileOrderDialog hidden(penaltyCornerPathsReversed());
    QVERIFY(hidden.findChild<QWidget*>(QStringLiteral("fixtureToggle")) == nullptr);

    qputenv("AVA_CONCAT_ORDER_FIXTURES", "1");
    ConcatFileOrderDialog dialog(penaltyCornerPathsReversed());
    auto* toggle = dialog.findChild<QWidget*>(QStringLiteral("fixtureToggle"));
    QVERIFY(toggle);
    QPushButton* worstCase = nullptr;
    QPushButton* empty = nullptr;
    QPushButton* one = nullptr;
    const QList<QPushButton*> segments = toggle->findChildren<QPushButton*>();
    for (QPushButton* segment : segments) {
        if (segment->text() == QStringLiteral("Worst case")) worstCase = segment;
        if (segment->text() == QStringLiteral("Empty")) empty = segment;
        if (segment->text() == QStringLiteral("One")) one = segment;
    }
    QVERIFY(worstCase);
    QVERIFY(empty);
    QVERIFY(one);

    QTest::mouseClick(worstCase, Qt::LeftButton);
    QTest::qWait(20);
    QCOMPARE(dialog.orderedFilePaths(),
             VideoClipSequence::filePathsOf(VideoClipSequence::chronologicalOrder(
                 VideoClipSequence::clipsFromFilePaths(VideoClipSequence::stressFilePaths()))));

    QTest::mouseClick(one, Qt::LeftButton);
    QTest::qWait(20);
    QCOMPARE(dialog.orderedFilePaths(),
             QStringList({QStringLiteral("/matches/demo/penalty_corner_003_10m55s-11m54s_p72.mp4")}));
    QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("concatHeadline"))->text(),
             QStringLiteral("Join 1 clip into one video"));

    QTest::mouseClick(empty, Qt::LeftButton);
    QTest::qWait(20);
    QVERIFY(dialog.orderedFilePaths().isEmpty());
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("concatContinue"))->isEnabled());
}

#include "VideoClipSequenceTests.moc"

int runVideoClipSequenceTests(int argc, char** argv) {
    int status = 0;
    {
        VideoClipSequenceTest parsing;
        status |= QTest::qExec(&parsing, argc, argv);
    }
    {
        ConcatFileOrderDialogTest dialog;
        status |= QTest::qExec(&dialog, argc, argv);
    }
    return status;
}

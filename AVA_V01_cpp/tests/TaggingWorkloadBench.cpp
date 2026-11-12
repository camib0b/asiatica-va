#include "EventCatalog.h"
#include "MatchSessionFile.h"
#include "TagEvent.h"
#include "TagSession.h"
#include "TagsTableModel.h"
#include "TimelineBar.h"
#include "WorkWindow.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QSlider>
#include <QTemporaryDir>

#include <iostream>

namespace {

constexpr int kMatchTagCount = 400;
constexpr qint64 kMatchSpanMs = 70 * 60 * 1000;

const EventCatalog::EventId kMatchEvents[] = {
    EventCatalog::EventId::Goal,           EventCatalog::EventId::Shot,
    EventCatalog::EventId::SixteenYard,    EventCatalog::EventId::Pass,
    EventCatalog::EventId::CircleEntry,    EventCatalog::EventId::Turnover,
    EventCatalog::EventId::PenaltyCorner,  EventCatalog::EventId::Card,
    EventCatalog::EventId::FiftyYard,      EventCatalog::EventId::SeventyFiveYard,
};

TagSession::GameTag matchTagAt(int index) {
  TagSession::GameTag tag;
  const EventCatalog::EventId eventId = kMatchEvents[index % 10];
  tag.setMainEventKey(TagEvent::catalogKey(eventId));
  tag.markMs = 5000 + (static_cast<qint64>(index) * kMatchSpanMs) / kMatchTagCount;
  tag.startMs = tag.markMs > 3000 ? tag.markMs - 3000 : 0;
  tag.endMs = tag.markMs + 5000;
  tag.team = (index % 2 == 0) ? QStringLiteral("Home") : QStringLiteral("Away");
  tag.period = QStringLiteral("Q1");
  if (eventId == EventCatalog::EventId::Shot) {
    TagEvent::FollowUpChain chain;
    chain.count = 1;
    chain.ids[0] = EventCatalog::EventId::OnTarget;
    tag.setFollowUpChain(chain);
  }
  return tag;
}

void addMatchTags(TagSession& session, int count) {
  for (int index = 0; index < count; ++index) {
    session.addTag(matchTagAt(index));
  }
}

void prepareMatchSession(TagSession& session) {
  session.setGameTeams(QStringLiteral("Las Leonas"), QStringLiteral("Red Lions"),
                       QStringLiteral("#7f1d1d"), QStringLiteral("#1e3a8a"));
  session.setGameMetadata(QDate(2026, 3, 14), QStringLiteral("LEO"), QStringLiteral("RED"));
}

QByteArray tagFingerprint(const TagSession& session) {
  QString payload;
  payload.reserve(session.tags().size() * 48);
  for (const TagSession::GameTag& tag : session.tags()) {
    payload += QString::number(tag.id);
    payload += QLatin1Char(':');
    payload += QString::number(tag.markMs);
    payload += QLatin1Char(':');
    payload += QString::number(tag.startMs);
    payload += QLatin1Char(':');
    payload += QString::number(tag.endMs);
    payload += QLatin1Char(':');
    payload += QString::number(static_cast<int>(tag.mainEventId));
    payload += QLatin1Char(':');
    payload += QString::number(tag.followUpCount);
    payload += QLatin1Char(':');
    payload += tag.team;
    payload += QLatin1Char('\n');
  }
  return QCryptographicHash::hash(payload.toUtf8(), QCryptographicHash::Sha256).toHex();
}

qint64 timeAddTagsWithWindow(int count) {
  TagSession session;
  prepareMatchSession(session);
  WorkWindow window;
  window.setTagSession(&session);
  window.resize(1280, 800);
  window.show();
  QApplication::processEvents();

  QElapsedTimer timer;
  timer.start();
  addMatchTags(session, count);
  return timer.elapsed();
}

qint64 timeSessionOnly(int count) {
  TagSession session;
  prepareMatchSession(session);
  QElapsedTimer timer;
  timer.start();
  addMatchTags(session, count);
  return timer.elapsed();
}

qint64 timePlayhead(int rowCount, int samples) {
  TagsTableModel model;
  QVector<TagsTableModel::Row> rows;
  rows.reserve(rowCount);
  for (int index = 0; index < rowCount; ++index) {
    TagsTableModel::Row row;
    row.tagId = static_cast<quint64>(index + 1);
    row.tagSessionIndex = index;
    row.markMs = 5000 + (static_cast<qint64>(index) * kMatchSpanMs) / rowCount;
    row.teamKey = QStringLiteral("Home");
    row.timeText = QString::number(row.markMs);
    row.teamText = QStringLiteral("Las Leonas");
    row.eventText = QStringLiteral("Shot · On target");
    rows.append(row);
  }
  model.setRows(rows);

  QElapsedTimer timer;
  timer.start();
  for (int sample = 0; sample < samples; ++sample) {
    const qint64 positionMs = (static_cast<qint64>(sample) * 2100) % kMatchSpanMs;
    model.setPlayheadMs(positionMs);
  }
  return timer.elapsed();
}

qint64 timeTimelineReports(int samples) {
  TimelineBar bar;
  bar.setDurationMs(kMatchSpanMs);
  bar.setPositionMs(0);
  auto* slider = bar.findChild<QSlider*>(QStringLiteral("TimelineSlider"));
  QElapsedTimer timer;
  timer.start();
  for (int sample = 0; sample < samples; ++sample) {
    const int dropMs = static_cast<int>((5000 + static_cast<qint64>(sample) * 1000) % kMatchSpanMs);
    if (slider) {
      slider->setSliderDown(true);
      slider->setSliderPosition(dropMs);
      bar.setPositionMs(0);
      slider->setSliderDown(false);
    }
    bar.setPositionMs(dropMs);
  }
  return timer.elapsed();
}

struct SaveLoadSample {
  qint64 elapsedMs = -1;
  QByteArray restoredFingerprint;
};

SaveLoadSample timeSaveAndLoad(const TagSession& session) {
  SaveLoadSample sample;
  QTemporaryDir directory;
  if (!directory.isValid()) return sample;

  const QString videoPath = directory.filePath(QStringLiteral("match.mp4"));
  QFile videoFile(videoPath);
  if (!videoFile.open(QIODevice::WriteOnly) || videoFile.write("match") < 0) return sample;
  videoFile.close();

  const MatchSessionFile::Document document =
      MatchSessionFile::capture(session, nullptr, nullptr, {videoPath});
  const QString sessionPath = directory.filePath(QStringLiteral("session.json"));

  QElapsedTimer timer;
  timer.start();
  const MatchSessionFile::SaveResult saved = MatchSessionFile::saveFile(
      sessionPath, document, MatchSessionFile::ExistingFileGuard::Replace);
  const MatchSessionFile::LoadResult loaded = MatchSessionFile::loadFile(sessionPath);
  TagSession restored;
  if (loaded.status == MatchSessionFile::LoadStatus::Loaded) {
    MatchSessionFile::apply(loaded.document, restored, nullptr, nullptr, false);
  }
  sample.elapsedMs = timer.elapsed();
  sample.restoredFingerprint = tagFingerprint(restored);
  if (saved.status != MatchSessionFile::SaveStatus::Saved ||
      loaded.status != MatchSessionFile::LoadStatus::Loaded) {
    sample.elapsedMs = -1;
  }
  return sample;
}

void printSample(const char* name, qint64 elapsedMs) {
  std::cout << "BENCH " << name << ' ' << elapsedMs << '\n';
}

}  // namespace

int runTaggingWorkloadBench(int argc, char** argv) {
  Q_UNUSED(argc);
  Q_UNUSED(argv);
  const qint64 warmupMs = timeAddTagsWithWindow(kMatchTagCount);
  printSample("warmup_add_tags_ui_ms", warmupMs);

  const qint64 addTagsMs = timeAddTagsWithWindow(kMatchTagCount);
  const qint64 sessionOnlyMs = timeSessionOnly(kMatchTagCount);
  const qint64 playheadMs = timePlayhead(kMatchTagCount, 2000);
  const qint64 timelineMs = timeTimelineReports(200);

  TagSession session;
  prepareMatchSession(session);
  addMatchTags(session, kMatchTagCount);
  const QByteArray fingerprint = tagFingerprint(session);
  const SaveLoadSample saveLoad = timeSaveAndLoad(session);
  const bool sameOutput = saveLoad.elapsedMs >= 0 && saveLoad.restoredFingerprint == fingerprint;

  printSample("add_tags_ui_ms", addTagsMs);
  printSample("session_only_ms", sessionOnlyMs);
  printSample("playhead_ms", playheadMs);
  printSample("timeline_scrub_ms", timelineMs);
  printSample("save_load_ms", saveLoad.elapsedMs);
  std::cout << "BENCH tag_count " << session.tags().size() << '\n';
  std::cout << "BENCH tag_fingerprint " << fingerprint.constData() << '\n';
  std::cout << "BENCH restored_fingerprint " << saveLoad.restoredFingerprint.constData() << '\n';
  std::cout << "BENCH same_output " << (sameOutput ? 1 : 0) << '\n';
  std::cout.flush();
  return sameOutput ? 0 : 1;
}

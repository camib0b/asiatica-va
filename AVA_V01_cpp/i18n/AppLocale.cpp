#include "AppLocale.h"
#include "LocaleNotifier.h"

#include <QHash>
#include <QSettings>
#include <QStringList>

namespace {

constexpr char kSettingsGroup[] = "ui";
constexpr char kLanguageKey[] = "language";

AppLocale::Language g_language = AppLocale::Language::English;

const QHash<QString, QString>& spanishEventMap() {
  static const QHash<QString, QString> map = {
      // Main grid
      {QStringLiteral("Circle Entry"), QStringLiteral("Ingreso área")},
      {QStringLiteral("Shot"), QStringLiteral("Tiro")},
      {QStringLiteral("Goal"), QStringLiteral("Gol")},
      {QStringLiteral("PC"), QStringLiteral("Corto")},
      {QStringLiteral("PS"), QStringLiteral("Penal")},
      {QStringLiteral("S.O."), QStringLiteral("S.O.")},
      {QStringLiteral("Pass"), QStringLiteral("Pase")},
      {QStringLiteral("Turnover"), QStringLiteral("Pérdida")},
      {QStringLiteral("Card"), QStringLiteral("Tarjeta")},
      {QStringLiteral("PC Foul"), QStringLiteral("Falta PC")},

      // First-level follow-ups
      {QStringLiteral("On target"), QStringLiteral("Al arco")},
      {QStringLiteral("Off target"), QStringLiteral("Afuera")},
      {QStringLiteral("Blocked"), QStringLiteral("Bloqueado")},
      {QStringLiteral("For"), QStringLiteral("A favor")},
      {QStringLiteral("Against"), QStringLiteral("En contra")},
      {QStringLiteral("Direct shot"), QStringLiteral("Directo")},
      {QStringLiteral("Variant"), QStringLiteral("Variante")},
      {QStringLiteral("Ruined"), QStringLiteral("Arruinado")},
      {QStringLiteral("Forward"), QStringLiteral("Hacia adelante")},
      {QStringLiteral("Sideways"), QStringLiteral("Hacia el lado")},
      {QStringLiteral("Back"), QStringLiteral("Hacia atrás")},
      {QStringLiteral("Green"), QStringLiteral("Verde")},
      {QStringLiteral("Yellow"), QStringLiteral("Amarilla")},
      {QStringLiteral("Red"), QStringLiteral("Roja")},
      {QStringLiteral("Flick"), QStringLiteral("Flick")},
      {QStringLiteral("Push"), QStringLiteral("Push")},
      {QStringLiteral("Sweep"), QStringLiteral("Barrida")},
      {QStringLiteral("Hit"), QStringLiteral("Pegada")},
      {QStringLiteral("Good"), QStringLiteral("Positivo")},
      {QStringLiteral("Bad"), QStringLiteral("Negativo")},
      {QStringLiteral("Neutral"), QStringLiteral("Neutro")},
      {QStringLiteral("Referee"), QStringLiteral("Arbitraje")},
      {QStringLiteral("Off"), QStringLiteral("Ofensiva")},
      {QStringLiteral("Def"), QStringLiteral("Defensiva")},

      // Second / third level
      {QStringLiteral("Saved"), QStringLiteral("Atajado")},
      {QStringLiteral("Post"), QStringLiteral("Palo")},
      {QStringLiteral("Closeby"), QStringLiteral("Cerca")},
      {QStringLiteral("Not close"), QStringLiteral("Lejos")},
      {QStringLiteral("Swept"), QStringLiteral("Barrida")},
      {QStringLiteral("Dragflick"), QStringLiteral("Arrastre")},
      {QStringLiteral("New PC"), QStringLiteral("Nuevo corto")},
      {QStringLiteral("Dribling"), QStringLiteral("Conducción")},
      {QStringLiteral("Deflection"), QStringLiteral("Desvío")},
      {QStringLiteral("Completed"), QStringLiteral("Completado")},
      {QStringLiteral("Failed"), QStringLiteral("Fallido")},
      {QStringLiteral("Interception"), QStringLiteral("Intercepción")},
      {QStringLiteral("Tackle"), QStringLiteral("Quite")},
      {QStringLiteral("Pressure"), QStringLiteral("Presión")},
      {QStringLiteral("Unforced error"), QStringLiteral("Error")},
      {QStringLiteral("Foot"), QStringLiteral("Pie")},
      {QStringLiteral("Stick"), QStringLiteral("Palo")},
      {QStringLiteral("Danger"), QStringLiteral("Peligro")},
      {QStringLiteral("Other"), QStringLiteral("Otro")},
      {QStringLiteral("Converted"), QStringLiteral("convertido")},
      {QStringLiteral("Missed"), QStringLiteral("no convertido")},
      {QStringLiteral("Replay"), QStringLiteral("repite")},
      {QStringLiteral("Left"), QStringLiteral("Izquierda")},
      {QStringLiteral("Middle"), QStringLiteral("Centro")},
      {QStringLiteral("Right"), QStringLiteral("Derecha")},
      // Default follow-up team labels (when names empty)
      {QStringLiteral("home"), QStringLiteral("Local")},
      {QStringLiteral("away"), QStringLiteral("Visita")},
  };
  return map;
}

QString translateEventForLanguage(const QString& canonicalToken, AppLocale::Language language) {
  const QString key = canonicalToken.trimmed();
  if (key.isEmpty()) return key;
  if (key == QStringLiteral("Special")) {
    return QStringLiteral("☆");
  }
  if (language != AppLocale::Language::Spanish) return key;
  const auto& map = spanishEventMap();
  const auto it = map.find(key);
  if (it != map.end()) return it.value();
  return key;
}

} // namespace

namespace AppLocale {

Language currentLanguage() {
  return g_language;
}

void setLanguage(Language language) {
  if (g_language == language) return;
  g_language = language;
  saveToSettings();
  LocaleNotifier::instance().emitLanguageChanged();
}

void loadFromSettings() {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  const QString v = settings.value(QLatin1String(kLanguageKey), QStringLiteral("en")).toString();
  settings.endGroup();
  g_language = (v == QLatin1String("es")) ? Language::Spanish : Language::English;
}

void saveToSettings() {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.setValue(QLatin1String(kLanguageKey),
                    g_language == Language::Spanish ? QStringLiteral("es") : QStringLiteral("en"));
  settings.endGroup();
}

QString trEvent(const QString& canonicalToken) {
  return translateEventForLanguage(canonicalToken, g_language);
}

QString trEventForLanguage(const QString& canonicalToken, Language language) {
  return translateEventForLanguage(canonicalToken, language);
}

QString translateCompoundPath(const QString& canonicalPath) {
  if (canonicalPath.isEmpty()) return canonicalPath;
  const QString sep = QStringLiteral(" → ");
  const QStringList parts = canonicalPath.split(sep, Qt::KeepEmptyParts);
  QStringList translated;
  translated.reserve(parts.size());
  for (const QString& part : parts) {
    translated.append(trEvent(part.trimmed()));
  }
  return translated.join(sep);
}

QString followUpPathWithoutTeamSegments(const QString& followUpEvent, const QString& homeTeamName,
                                        const QString& awayTeamName) {
  if (followUpEvent.isEmpty()) return followUpEvent;
  const QString sep = QStringLiteral(" → ");
  const QStringList parts = followUpEvent.split(sep, Qt::KeepEmptyParts);
  QStringList filtered;
  const QString homeLbl = homeTeamName.trimmed().isEmpty() ? QStringLiteral("home") : homeTeamName.trimmed();
  const QString awayLbl = awayTeamName.trimmed().isEmpty() ? QStringLiteral("away") : awayTeamName.trimmed();
  for (const QString& segment : parts) {
    const QString trimmed = segment.trimmed();
    if (QString::compare(trimmed, homeLbl, Qt::CaseInsensitive) == 0) continue;
    if (QString::compare(trimmed, awayLbl, Qt::CaseInsensitive) == 0) continue;
    filtered.append(trimmed);
  }
  return filtered.join(sep);
}

QString trDisplayTagLine(const QString& mainEvent, const QString& followUpEvent) {
  QString line = trEvent(mainEvent);
  if (!followUpEvent.isEmpty()) {
    line += QStringLiteral(" → ") + translateCompoundPath(followUpEvent);
  }
  return line;
}

QString trUi(const char* key) {
  if (!key) return QString();
  if (g_language != Language::Spanish) {
    static const QHash<QString, QString> en = {
        {QStringLiteral("app.title"), QStringLiteral("AVA | Camila Escudero")},
        {QStringLiteral("welcome.import"), QStringLiteral("&Select video file(s)")},
        {QStringLiteral("setup.title"), QStringLiteral("Set up teams")},
        {QStringLiteral("setup.home_team"), QStringLiteral("Home team:")},
        {QStringLiteral("setup.away_team"), QStringLiteral("Away team:")},
        {QStringLiteral("setup.home_color"), QStringLiteral("Home color:")},
        {QStringLiteral("setup.away_color"), QStringLiteral("Away color:")},
        {QStringLiteral("setup.placeholder_team"), QStringLiteral("e.g. Lakers")},
        {QStringLiteral("setup.placeholder_hex"), QStringLiteral("#RRGGBB")},
        {QStringLiteral("setup.placeholder_abbrev"), QStringLiteral("3 letters")},
        {QStringLiteral("setup.placeholder_competition"), QStringLiteral("e.g. World Cup")},
        {QStringLiteral("setup.pick"), QStringLiteral("Pick")},
        {QStringLiteral("setup.back"), QStringLiteral("&Back")},
        {QStringLiteral("setup.continue"), QStringLiteral("&Continue")},
        {QStringLiteral("setup.lang_label"), QStringLiteral("Language:")},
        {QStringLiteral("setup.lang_en"), QStringLiteral("English")},
        {QStringLiteral("setup.lang_es"), QStringLiteral("Español")},
        {QStringLiteral("setup.competition"), QStringLiteral("Competition:")},
        {QStringLiteral("setup.date"), QStringLiteral("Date:")},
        {QStringLiteral("setup.home_abbrev"), QStringLiteral("Home abbrev:")},
        {QStringLiteral("setup.away_abbrev"), QStringLiteral("Away abbrev:")},
        {QStringLiteral("gamecontrols.start_game"), QStringLiteral("Start game (G)")},
        {QStringLiteral("gamecontrols.start_q1"), QStringLiteral("Start Q1")},
        {QStringLiteral("gamecontrols.start_q2"), QStringLiteral("Start Q2 (H)")},
        {QStringLiteral("gamecontrols.start_q3"), QStringLiteral("Start Q3 (H)")},
        {QStringLiteral("gamecontrols.start_q4"), QStringLiteral("Start Q4 (H)")},
        {QStringLiteral("gamecontrols.end_game"), QStringLiteral("End game (H)")},
        {QStringLiteral("gamecontrols.quarter_not_started"), QStringLiteral("--")},
        {QStringLiteral("gamecontrols.quarter_ended"), QStringLiteral("end")},
        {QStringLiteral("dialog.pick_home_color"), QStringLiteral("Home team color")},
        {QStringLiteral("dialog.pick_away_color"), QStringLiteral("Away team color")},
        {QStringLiteral("file.select_video"), QStringLiteral("Select a video file")},
        {QStringLiteral("file.video_filter"),
         QStringLiteral("Video files (*.mp4 *.mov *.m4v *.mkv *.avi *.mts *.MTS);;All files (*.*)")},
        {QStringLiteral("file.xml_filter"),
         QStringLiteral("XML files (*.xml);;All files (*.*)")},
        {QStringLiteral("mode.tagging"), QStringLiteral("Tagging")},
        {QStringLiteral("mode.analyzing"), QStringLiteral("Analyzing")},
        {QStringLiteral("tooltip.mode_tagging"), QStringLiteral("Eyes on video, hands on keyboard (M)")},
        {QStringLiteral("tooltip.mode_analyzing"), QStringLiteral("Stats and notes (M)")},
        {QStringLiteral("tooltip.video_menu"), QStringLiteral("Video Manager")},
        {QStringLiteral("menu.replace_video"), QStringLiteral("Replace video with another one")},
        {QStringLiteral("menu.close_video"), QStringLiteral("Close current video")},
        {QStringLiteral("tags.header"), QStringLiteral("Tags")},
        {QStringLiteral("tags.filter"), QStringLiteral("Filter")},
        {QStringLiteral("tags.remove_filters"), QStringLiteral("Remove filters")},
        {QStringLiteral("tags.undo"), QStringLiteral("Undo")},
        {QStringLiteral("tags.undo_tooltip"), QStringLiteral("Ctrl+Z  Remove most recent tag")},
        {QStringLiteral("tags.note_placeholder"), QStringLiteral("Note for selected tag…")},
        {QStringLiteral("tags.col_time"), QStringLiteral("Time")},
        {QStringLiteral("tags.col_team"), QStringLiteral("Team")},
        {QStringLiteral("tags.col_event"), QStringLiteral("Event")},
        {QStringLiteral("filter.select_all"), QStringLiteral("Select all")},
        {QStringLiteral("filter.select_none"), QStringLiteral("Select none")},
        {QStringLiteral("filter.indicator_path"), QStringLiteral("Filtered by: ")},
        {QStringLiteral("filter.indicator_list"), QStringLiteral("Filtered by: ")},
        {QStringLiteral("stats.header"), QStringLiteral("Stats")},
        {QStringLiteral("stats.col_event"), QStringLiteral("Event")},
        {QStringLiteral("stats.col_count"), QStringLiteral("Count")},
        {QStringLiteral("stats.filter_home_fallback"), QStringLiteral("Home")},
        {QStringLiteral("stats.filter_away_fallback"), QStringLiteral("Away")},
        {QStringLiteral("stats.filter_both"), QStringLiteral("Both")},
        {QStringLiteral("stats.overlay_title"), QStringLiteral("Stats — Tag taxonomy")},
        {QStringLiteral("stats_overlay.tooltip"), QStringLiteral("Stats overlay (,)")},
        {QStringLiteral("vc.play"), QStringLiteral("Play")},
        {QStringLiteral("vc.pause"), QStringLiteral("Pause")},
        {QStringLiteral("vc.back"), QStringLiteral("⟵ 2s")},
        {QStringLiteral("vc.forward"), QStringLiteral("2s ⟶")},
        {QStringLiteral("vc.slower"), QStringLiteral("Slower")},
        {QStringLiteral("vc.reset_speed"), QStringLiteral("Reset 1.0x")},
        {QStringLiteral("vc.faster"), QStringLiteral("Faster")},
        {QStringLiteral("vc.mute"), QStringLiteral("Mute")},
        {QStringLiteral("vc.unmute"), QStringLiteral("Unmute")},
        {QStringLiteral("vc.speed_label"), QStringLiteral("Speed: %1")},
        {QStringLiteral("vc.tt.play"), QStringLiteral("space  Play")},
        {QStringLiteral("vc.tt.pause"), QStringLiteral("space  Pause")},
        {QStringLiteral("vc.tt.back"), QStringLiteral("⟵  Back")},
        {QStringLiteral("vc.tt.forward"), QStringLiteral("⟶  Forward")},
        {QStringLiteral("vc.tt.slower"), QStringLiteral("-  Slower")},
        {QStringLiteral("vc.tt.faster"), QStringLiteral("+  Faster")},
        {QStringLiteral("vc.tt.reset"), QStringLiteral("}  Reset speed")},
        {QStringLiteral("menu.export_clips"), QStringLiteral("Export clips…")},
        {QStringLiteral("menu.import_xml"), QStringLiteral("Import XML…")},
        {QStringLiteral("menu.clip_durations"), QStringLiteral("Clip durations…")},
        {QStringLiteral("clip_durations.title"), QStringLiteral("Clip Durations")},
        {QStringLiteral("clip_durations.subtitle"),
         QStringLiteral("Default lead and lag times around each tag when creating clips. Changes apply "
                        "immediately to untrimmed tags.")},
        {QStringLiteral("clip_durations.col_event"), QStringLiteral("Event")},
        {QStringLiteral("clip_durations.col_lead"), QStringLiteral("Lead (s)")},
        {QStringLiteral("clip_durations.col_lag"), QStringLiteral("Lag (s)")},
        {QStringLiteral("clip_durations.col_total"), QStringLiteral("Total")},
        {QStringLiteral("clip_durations.reset"), QStringLiteral("Reset all to defaults")},
        {QStringLiteral("clip_durations.close"), QStringLiteral("Close")},
        {QStringLiteral("export.title"), QStringLiteral("Export Clips")},
        {QStringLiteral("export.subtitle"), QStringLiteral("Create a video compilation of all clips for a selected event type.")},
        {QStringLiteral("export.event_type"), QStringLiteral("Event type:")},
        {QStringLiteral("export.clips_label"), QStringLiteral("clips to export")},
        {QStringLiteral("export.team_label"), QStringLiteral("Team:")},
        {QStringLiteral("export.team_all"), QStringLiteral("All teams")},
        {QStringLiteral("export.team_home_default"), QStringLiteral("Home")},
        {QStringLiteral("export.team_away_default"), QStringLiteral("Away")},
        {QStringLiteral("export.sort_order"), QStringLiteral("Sort order:")},
        {QStringLiteral("export.sort_chronological"), QStringLiteral("Chronological")},
        {QStringLiteral("export.sort_by_team"), QStringLiteral("By team, then chronological")},
        {QStringLiteral("export.overlay_language"), QStringLiteral("Overlay language:")},
        {QStringLiteral("export.include_bottom_overlay"), QStringLiteral("Include bottom tag overlay")},
        {QStringLiteral("export.include_scoreboard_overlay"), QStringLiteral("Include scoreboard overlay")},
        {QStringLiteral("export.include_audio_track"), QStringLiteral("Include clip audio track")},
        {QStringLiteral("export.include_ava_overlay"), QStringLiteral("Show AVA overlay in top-right corner")},
        {QStringLiteral("export.before_tag"), QStringLiteral("Before tag:")},
        {QStringLiteral("export.after_tag"), QStringLiteral("After tag:")},
        {QStringLiteral("export.event_durations_header"), QStringLiteral("Per-event clip durations")},
        {QStringLiteral("export.event_duration_pre_prefix"), QStringLiteral("pre ")},
        {QStringLiteral("export.event_duration_post_prefix"), QStringLiteral("post ")},
        {QStringLiteral("export.output_format"), QStringLiteral("Output format:")},
        {QStringLiteral("export.format_mp4"), QStringLiteral("MP4 video")},
        {QStringLiteral("export.format_xml"), QStringLiteral("XML report")},
        {QStringLiteral("export.format_both"), QStringLiteral("MP4 video + XML report")},
        {QStringLiteral("export.export_xml_now"), QStringLiteral("Export XML \u2192")},
        {QStringLiteral("export.xml_instances_label"), QStringLiteral("tags exported in XML report")},
        {QStringLiteral("export.xml_filters_ignored_tooltip"), QStringLiteral("XML reports export every tag in the session; event and team filters only affect MP4 clips.")},
        {QStringLiteral("export.xml_report_filename_segment"), QStringLiteral("XML report")},
        {QStringLiteral("export.xml_success"), QStringLiteral("XML report exported successfully.")},
        {QStringLiteral("export.xml_failed"), QStringLiteral("Failed to write the XML report.")},
        {QStringLiteral("export.output_placeholder_xml"), QStringLiteral("Choose output .xml file…")},
        {QStringLiteral("export.output_placeholder_both"), QStringLiteral("Choose output .mp4 file (.xml saved alongside)…")},
        {QStringLiteral("export.save_to"), QStringLiteral("Save to:")},
        {QStringLiteral("export.output_placeholder"), QStringLiteral("Choose output file…")},
        {QStringLiteral("export.browse"), QStringLiteral("Browse…")},
        {QStringLiteral("export.save_dialog_title"), QStringLiteral("Save exported video")},
        {QStringLiteral("export.export"), QStringLiteral("Export")},
        {QStringLiteral("export.cancel"), QStringLiteral("Cancel")},
        {QStringLiteral("export.close"), QStringLiteral("Close")},
        {QStringLiteral("export.review_clips"), QStringLiteral("Review clips \u2192")},
        {QStringLiteral("export.back"), QStringLiteral("\u2190 Back")},
        {QStringLiteral("export.clip_label"), QStringLiteral("Clip")},
        {QStringLiteral("export.discard_clip"), QStringLiteral("Discard this clip")},
        {QStringLiteral("export.all_clips_discarded"), QStringLiteral("All clips have been discarded.")},
        {QStringLiteral("export.include_note"), QStringLiteral("Include note in overlay")},
        {QStringLiteral("export.note_placeholder"), QStringLiteral("Note text\u2026")},
        {QStringLiteral("export.starting"), QStringLiteral("Starting export…")},
        {QStringLiteral("export.progress_prefix"), QStringLiteral("Exporting clip")},
        {QStringLiteral("export.done"), QStringLiteral("Export complete.")},
        {QStringLiteral("export.success"), QStringLiteral("Clips exported successfully!")},
        {QStringLiteral("export.no_output_path"), QStringLiteral("Please choose an output file path.")},
        {QStringLiteral("export.ffmpeg_not_found"), QStringLiteral("FFmpeg was not found on this system.\nPlease install FFmpeg to use clip export.\n\nhttps://ffmpeg.org")},
        {QStringLiteral("export.upload_to_youtube"), QStringLiteral("Upload exported video to YouTube")},
        {QStringLiteral("export.youtube_account"), QStringLiteral("YouTube account:")},
        {QStringLiteral("export.youtube_playlist"), QStringLiteral("Match playlist:")},
        {QStringLiteral("export.youtube_connect"), QStringLiteral("Connect YouTube account")},
        {QStringLiteral("export.youtube_disconnect"), QStringLiteral("Disconnect")},
        {QStringLiteral("export.youtube_not_configured"), QStringLiteral("OAuth client ID not configured. See config/youtube_oauth.json.example.")},
        {QStringLiteral("export.youtube_not_connected"), QStringLiteral("Not connected")},
        {QStringLiteral("export.youtube_connected"), QStringLiteral("Connected as %1")},
        {QStringLiteral("export.youtube_playlist_target"), QStringLiteral("%1")},
        {QStringLiteral("export.youtube_requires_mp4"), QStringLiteral("YouTube upload requires MP4 or MP4 + XML export format.")},
        {QStringLiteral("export.youtube_sign_in_required"), QStringLiteral("Connect your YouTube account before exporting with upload enabled.")},
        {QStringLiteral("export.youtube_resolving_playlist"), QStringLiteral("Resolving match playlist on YouTube…")},
        {QStringLiteral("export.youtube_uploading"), QStringLiteral("Uploading to YouTube…")},
        {QStringLiteral("export.youtube_upload_done"), QStringLiteral("YouTube upload complete.")},
        {QStringLiteral("export.youtube_upload_success"), QStringLiteral("Video uploaded to the match playlist on YouTube.")},
        {QStringLiteral("export.youtube_upload_success_with_link"), QStringLiteral("Video uploaded to the match playlist on YouTube.\n\n%1")},
        {QStringLiteral("export.youtube_open_video"), QStringLiteral("Open on YouTube")},
        {QStringLiteral("export.youtube_upload_failed"), QStringLiteral("YouTube upload failed.")},
        {QStringLiteral("export.youtube_description_footer"), QStringLiteral("Exported from AVA.")},
        {QStringLiteral("concat.dialog_title"), QStringLiteral("Arrange Video Files")},
        {QStringLiteral("concat.move_left"), QStringLiteral("\u2190 Move Left")},
        {QStringLiteral("concat.move_right"), QStringLiteral("Move Right \u2192")},
        {QStringLiteral("concat.continue_btn"), QStringLiteral("&Continue")},
        {QStringLiteral("concat.cancel"), QStringLiteral("Cancel")},
        {QStringLiteral("concat.preparing"), QStringLiteral("Combining video files\u2026")},
        {QStringLiteral("concat.error_ffmpeg"), QStringLiteral("FFmpeg is required to combine multiple video files.\nPlease install FFmpeg to continue.\n\nhttps://ffmpeg.org")},
        {QStringLiteral("concat.error_failed"), QStringLiteral("Failed to combine video files.")},
        {QStringLiteral("xml_import.title"), QStringLiteral("Import XML")},
        {QStringLiteral("xml_import.select_file"), QStringLiteral("Select XML file to import")},
        {QStringLiteral("xml_import.cancel"), QStringLiteral("Cancel")},
        {QStringLiteral("xml_import.continue"), QStringLiteral("Continue")},
        {QStringLiteral("xml_import.import"), QStringLiteral("Import")},
        {QStringLiteral("xml_import.conflict_title"), QStringLiteral("Existing tags found")},
        {QStringLiteral("xml_import.conflict_message"),
         QStringLiteral("This session already has tagged events. Replace them with the imported XML, merge both sets, or cancel.")},
        {QStringLiteral("xml_import.conflict_replace"), QStringLiteral("Replace all tags")},
        {QStringLiteral("xml_import.conflict_merge"), QStringLiteral("Merge with existing tags")},
        {QStringLiteral("xml_import.sync_title"), QStringLiteral("Align XML timeline")},
        {QStringLiteral("xml_import.sync_instructions"),
         QStringLiteral("Scrub the video timeline to the moment that matches the XML start anchor, then continue. The playhead position is used as the sync point.")},
        {QStringLiteral("xml_import.sync_fallback_warning"),
         QStringLiteral("No Inicio tag was found in the XML. Using the first quarter or earliest event as the sync anchor.")},
        {QStringLiteral("xml_import.sync_xml_anchor"), QStringLiteral("XML anchor (%2): %1")},
        {QStringLiteral("xml_import.sync_video_anchor"), QStringLiteral("Video anchor: %1")},
        {QStringLiteral("xml_import.sync_use_playhead"), QStringLiteral("Use current playhead")},
        {QStringLiteral("xml_import.sync_offset"), QStringLiteral("Offset: %1 s")},
        {QStringLiteral("xml_import.sync_preview_ok"), QStringLiteral("%1 instances will be imported.")},
        {QStringLiteral("xml_import.sync_preview_clamp"),
         QStringLiteral("%1 instances will be imported. %2 will start before video zero; %3 will extend past the video end (timestamps will be clamped).")},
        {QStringLiteral("xml_import.mapping_title"), QStringLiteral("Map XML event codes")},
        {QStringLiteral("xml_import.mapping_instructions"),
         QStringLiteral("Review how each XML code maps to AVA events. Adjust any row before importing.")},
        {QStringLiteral("xml_import.mapping_abbrev_header"), QStringLiteral("Map XML team abbreviations to session teams:")},
        {QStringLiteral("xml_import.mapping_home_abbrev"), QStringLiteral("Home abbrev in XML:")},
        {QStringLiteral("xml_import.mapping_away_abbrev"), QStringLiteral("Away abbrev in XML:")},
        {QStringLiteral("xml_import.mapping_col_code"), QStringLiteral("XML code")},
        {QStringLiteral("xml_import.mapping_col_count"), QStringLiteral("Count")},
        {QStringLiteral("xml_import.mapping_col_event"), QStringLiteral("Map to event")},
        {QStringLiteral("xml_import.mapping_col_team"), QStringLiteral("Team")},
        {QStringLiteral("xml_import.mapping_col_action"), QStringLiteral("Action")},
        {QStringLiteral("xml_import.mapping_team_none"), QStringLiteral("(none)")},
        {QStringLiteral("xml_import.mapping_action_import"), QStringLiteral("Import")},
        {QStringLiteral("xml_import.mapping_action_skip"), QStringLiteral("Skip")},
        {QStringLiteral("xml_import.mapping_none_selected"), QStringLiteral("No instances are set to import.")},
        {QStringLiteral("xml_import.mapping_missing_event"), QStringLiteral("Choose an event for code: %1")},
        {QStringLiteral("xml_import.mapping_missing_team"), QStringLiteral("Choose a team for code: %1")},
        {QStringLiteral("xml_import.complete_summary"),
         QStringLiteral("Imported %1 events (%2 skipped, %3 clamped to video bounds).")},
    };
    return en.value(QLatin1String(key), QLatin1String(key));
  }

  static const QHash<QString, QString> es = {
      {QStringLiteral("app.title"), QStringLiteral("AVA | Camila Escudero")},
      {QStringLiteral("welcome.import"), QStringLiteral("&Elegir video(s)")},
      {QStringLiteral("setup.title"), QStringLiteral("Configurar equipos")},
      {QStringLiteral("setup.home_team"), QStringLiteral("Equipo local:")},
      {QStringLiteral("setup.away_team"), QStringLiteral("Equipo visitante:")},
      {QStringLiteral("setup.home_color"), QStringLiteral("Color local:")},
      {QStringLiteral("setup.away_color"), QStringLiteral("Color visitante:")},
      {QStringLiteral("setup.placeholder_team"), QStringLiteral("ej. Lakers")},
      {QStringLiteral("setup.placeholder_hex"), QStringLiteral("#RRGGBB")},
      {QStringLiteral("setup.placeholder_abbrev"), QStringLiteral("3 letras")},
      {QStringLiteral("setup.placeholder_competition"), QStringLiteral("ej. Mundial")},
      {QStringLiteral("setup.pick"), QStringLiteral("Elegir")},
      {QStringLiteral("setup.back"), QStringLiteral("&Atrás")},
      {QStringLiteral("setup.continue"), QStringLiteral("&Continuar")},
      {QStringLiteral("setup.lang_label"), QStringLiteral("Idioma:")},
      {QStringLiteral("setup.lang_en"), QStringLiteral("English")},
      {QStringLiteral("setup.lang_es"), QStringLiteral("Español")},
      {QStringLiteral("setup.competition"), QStringLiteral("Competición:")},
      {QStringLiteral("setup.date"), QStringLiteral("Fecha:")},
      {QStringLiteral("setup.home_abbrev"), QStringLiteral("Sigla local:")},
      {QStringLiteral("setup.away_abbrev"), QStringLiteral("Sigla visita:")},
      {QStringLiteral("gamecontrols.start_game"), QStringLiteral("Iniciar partido (G)")},
      {QStringLiteral("gamecontrols.start_q1"), QStringLiteral("Iniciar Q1")},
      {QStringLiteral("gamecontrols.start_q2"), QStringLiteral("Iniciar Q2 (H)")},
      {QStringLiteral("gamecontrols.start_q3"), QStringLiteral("Iniciar Q3 (H)")},
      {QStringLiteral("gamecontrols.start_q4"), QStringLiteral("Iniciar Q4 (H)")},
      {QStringLiteral("gamecontrols.end_game"), QStringLiteral("Fin del partido (H)")},
      {QStringLiteral("gamecontrols.quarter_not_started"), QStringLiteral("--")},
      {QStringLiteral("gamecontrols.quarter_ended"), QStringLiteral("fin")},
      {QStringLiteral("dialog.pick_home_color"), QStringLiteral("Color del equipo local")},
      {QStringLiteral("dialog.pick_away_color"), QStringLiteral("Color del equipo visitante")},
      {QStringLiteral("file.select_video"), QStringLiteral("Seleccionar archivo de video")},
      {QStringLiteral("file.video_filter"),
       QStringLiteral("Vídeo (*.mp4 *.mov *.m4v *.mkv *.avi *.mts *.MTS);;Todos los archivos (*.*)")},
      {QStringLiteral("file.xml_filter"),
       QStringLiteral("Archivos XML (*.xml);;Todos los archivos (*.*)")},
      {QStringLiteral("mode.tagging"), QStringLiteral("Etiquetado")},
      {QStringLiteral("mode.analyzing"), QStringLiteral("Análisis")},
      {QStringLiteral("tooltip.mode_tagging"), QStringLiteral("Ojos en el vídeo, manos en el teclado (M)")},
      {QStringLiteral("tooltip.mode_analyzing"), QStringLiteral("Estadísticas y notas (M)")},
      {QStringLiteral("tooltip.video_menu"), QStringLiteral("Video")},
      {QStringLiteral("menu.replace_video"), QStringLiteral("Sustituir video por otro")},
      {QStringLiteral("menu.close_video"), QStringLiteral("Cerrar video actual")},
      {QStringLiteral("tags.header"), QStringLiteral("Marcas")},
      {QStringLiteral("tags.filter"), QStringLiteral("Filtrar")},
      {QStringLiteral("tags.remove_filters"), QStringLiteral("Quitar filtros")},
      {QStringLiteral("tags.undo"), QStringLiteral("Deshacer")},
      {QStringLiteral("tags.undo_tooltip"), QStringLiteral("Ctrl+Z  Quitar la última marca")},
      {QStringLiteral("tags.note_placeholder"), QStringLiteral("Nota de la marca seleccionada…")},
      {QStringLiteral("tags.col_time"), QStringLiteral("Tiempo")},
      {QStringLiteral("tags.col_team"), QStringLiteral("Equipo")},
      {QStringLiteral("tags.col_event"), QStringLiteral("Evento")},
      {QStringLiteral("filter.select_all"), QStringLiteral("Seleccionar todo")},
      {QStringLiteral("filter.select_none"), QStringLiteral("Seleccionar ninguno")},
      {QStringLiteral("filter.indicator_path"), QStringLiteral("Filtrado por: ")},
      {QStringLiteral("filter.indicator_list"), QStringLiteral("Filtrado por: ")},
      {QStringLiteral("stats.header"), QStringLiteral("Estadísticas")},
      {QStringLiteral("stats.col_event"), QStringLiteral("Evento")},
      {QStringLiteral("stats.col_count"), QStringLiteral("Cantidad")},
      {QStringLiteral("stats.filter_home_fallback"), QStringLiteral("Local")},
      {QStringLiteral("stats.filter_away_fallback"), QStringLiteral("Visitante")},
      {QStringLiteral("stats.filter_both"), QStringLiteral("Ambos")},
      {QStringLiteral("stats.overlay_title"), QStringLiteral("Estadísticas")},
      {QStringLiteral("stats_overlay.tooltip"), QStringLiteral("Superposición de estadísticas (,)")},
      {QStringLiteral("vc.play"), QStringLiteral("Reproducir")},
      {QStringLiteral("vc.pause"), QStringLiteral("Pausa")},
      {QStringLiteral("vc.back"), QStringLiteral("⟵ 2s")},
      {QStringLiteral("vc.forward"), QStringLiteral("2s ⟶")},
      {QStringLiteral("vc.slower"), QStringLiteral("Más lento")},
      {QStringLiteral("vc.reset_speed"), QStringLiteral("Restablecer 1.0x")},
      {QStringLiteral("vc.faster"), QStringLiteral("Más rápido")},
      {QStringLiteral("vc.mute"), QStringLiteral("Silenciar")},
      {QStringLiteral("vc.unmute"), QStringLiteral("Activar audio")},
      {QStringLiteral("vc.speed_label"), QStringLiteral("Velocidad: %1")},
      {QStringLiteral("vc.tt.play"), QStringLiteral("espacio  Reproducir")},
      {QStringLiteral("vc.tt.pause"), QStringLiteral("espacio  Pausa")},
      {QStringLiteral("vc.tt.back"), QStringLiteral("⟵  Atrás")},
      {QStringLiteral("vc.tt.forward"), QStringLiteral("⟶  Adelante")},
        {QStringLiteral("vc.tt.slower"), QStringLiteral("-  Más lento")},
        {QStringLiteral("vc.tt.faster"), QStringLiteral("+  Más rápido")},
        {QStringLiteral("vc.tt.reset"), QStringLiteral("}  Restablecer velocidad")},
      {QStringLiteral("menu.export_clips"), QStringLiteral("Exportar clips…")},
      {QStringLiteral("menu.import_xml"), QStringLiteral("Importar XML…")},
      {QStringLiteral("menu.clip_durations"), QStringLiteral("Duraciones de clip…")},
      {QStringLiteral("clip_durations.title"), QStringLiteral("Duraciones de clip")},
      {QStringLiteral("clip_durations.subtitle"),
       QStringLiteral("Tiempos de anticipación y retardo predeterminados alrededor de cada marca al "
                      "crear clips. Los cambios se aplican de inmediato a marcas sin recorte.")},
      {QStringLiteral("clip_durations.col_event"), QStringLiteral("Evento")},
      {QStringLiteral("clip_durations.col_lead"), QStringLiteral("Anticipación (s)")},
      {QStringLiteral("clip_durations.col_lag"), QStringLiteral("Retardo (s)")},
      {QStringLiteral("clip_durations.col_total"), QStringLiteral("Total")},
      {QStringLiteral("clip_durations.reset"), QStringLiteral("Restablecer valores predeterminados")},
      {QStringLiteral("clip_durations.close"), QStringLiteral("Cerrar")},
      {QStringLiteral("export.title"), QStringLiteral("Exportar clips")},
      {QStringLiteral("export.subtitle"), QStringLiteral("Crear un video con todos los clips de un tipo de evento seleccionado.")},
      {QStringLiteral("export.event_type"), QStringLiteral("Tipo de evento:")},
      {QStringLiteral("export.clips_label"), QStringLiteral("clips a exportar")},
      {QStringLiteral("export.team_label"), QStringLiteral("Equipo:")},
      {QStringLiteral("export.team_all"), QStringLiteral("Todos los equipos")},
      {QStringLiteral("export.team_home_default"), QStringLiteral("Local")},
      {QStringLiteral("export.team_away_default"), QStringLiteral("Visitante")},
      {QStringLiteral("export.sort_order"), QStringLiteral("Orden:")},
      {QStringLiteral("export.sort_chronological"), QStringLiteral("Cronológico")},
      {QStringLiteral("export.sort_by_team"), QStringLiteral("Por equipo, luego cronológico")},
      {QStringLiteral("export.overlay_language"), QStringLiteral("Idioma del overlay:")},
        {QStringLiteral("export.include_bottom_overlay"), QStringLiteral("Incluir overlay de etiqueta inferior")},
        {QStringLiteral("export.include_scoreboard_overlay"), QStringLiteral("Incluir overlay de marcador")},
        {QStringLiteral("export.include_audio_track"), QStringLiteral("Incluir audio del clip")},
        {QStringLiteral("export.include_ava_overlay"), QStringLiteral("Mostrar overlay de AVA en la esquina superior derecha")},
        {QStringLiteral("export.before_tag"), QStringLiteral("Antes de la marca:")},
      {QStringLiteral("export.after_tag"), QStringLiteral("Después de la marca:")},
      {QStringLiteral("export.event_durations_header"), QStringLiteral("Duraciones por tipo de evento")},
      {QStringLiteral("export.event_duration_pre_prefix"), QStringLiteral("antes ")},
      {QStringLiteral("export.event_duration_post_prefix"), QStringLiteral("después ")},
      {QStringLiteral("export.output_format"), QStringLiteral("Formato de salida:")},
      {QStringLiteral("export.format_mp4"), QStringLiteral("Video MP4")},
      {QStringLiteral("export.format_xml"), QStringLiteral("Reporte XML")},
      {QStringLiteral("export.format_both"), QStringLiteral("Video MP4 + reporte XML")},
      {QStringLiteral("export.export_xml_now"), QStringLiteral("Exportar XML \u2192")},
      {QStringLiteral("export.xml_instances_label"), QStringLiteral("marcas exportadas en el reporte XML")},
      {QStringLiteral("export.xml_filters_ignored_tooltip"), QStringLiteral("Los reportes XML exportan todas las marcas de la sesión; los filtros de evento y equipo solo afectan los clips MP4.")},
      {QStringLiteral("export.xml_report_filename_segment"), QStringLiteral("reporte XML")},
      {QStringLiteral("export.xml_success"), QStringLiteral("Reporte XML exportado correctamente.")},
      {QStringLiteral("export.xml_failed"), QStringLiteral("No se pudo escribir el reporte XML.")},
      {QStringLiteral("export.output_placeholder_xml"), QStringLiteral("Elegir archivo .xml de salida…")},
      {QStringLiteral("export.output_placeholder_both"), QStringLiteral("Elegir archivo .mp4 (el .xml se guarda al lado)…")},
      {QStringLiteral("export.save_to"), QStringLiteral("Guardar en:")},
      {QStringLiteral("export.output_placeholder"), QStringLiteral("Elegir archivo de salida…")},
      {QStringLiteral("export.browse"), QStringLiteral("Buscar…")},
      {QStringLiteral("export.save_dialog_title"), QStringLiteral("Guardar video exportado")},
      {QStringLiteral("export.export"), QStringLiteral("Exportar")},
      {QStringLiteral("export.cancel"), QStringLiteral("Cancelar")},
      {QStringLiteral("export.close"), QStringLiteral("Cerrar")},
      {QStringLiteral("export.review_clips"), QStringLiteral("Revisar clips \u2192")},
      {QStringLiteral("export.back"), QStringLiteral("\u2190 Volver")},
      {QStringLiteral("export.clip_label"), QStringLiteral("Clip")},
      {QStringLiteral("export.discard_clip"), QStringLiteral("Descartar este clip")},
      {QStringLiteral("export.all_clips_discarded"), QStringLiteral("Todos los clips han sido descartados.")},
      {QStringLiteral("export.include_note"), QStringLiteral("Incluir nota en overlay")},
      {QStringLiteral("export.note_placeholder"), QStringLiteral("Texto de la nota\u2026")},
      {QStringLiteral("export.starting"), QStringLiteral("Iniciando exportación…")},
      {QStringLiteral("export.progress_prefix"), QStringLiteral("Exportando clip")},
      {QStringLiteral("export.done"), QStringLiteral("Exportación completa.")},
      {QStringLiteral("export.success"), QStringLiteral("¡Clips exportados exitosamente!")},
      {QStringLiteral("export.no_output_path"), QStringLiteral("Por favor elija una ruta de archivo de salida.")},
      {QStringLiteral("export.ffmpeg_not_found"), QStringLiteral("FFmpeg no fue encontrado en este sistema.\nPor favor instale FFmpeg para exportar clips.\n\nhttps://ffmpeg.org")},
      {QStringLiteral("export.upload_to_youtube"), QStringLiteral("Subir el video exportado a YouTube")},
      {QStringLiteral("export.youtube_account"), QStringLiteral("Cuenta de YouTube:")},
      {QStringLiteral("export.youtube_playlist"), QStringLiteral("Lista del partido:")},
      {QStringLiteral("export.youtube_connect"), QStringLiteral("Conectar cuenta de YouTube")},
      {QStringLiteral("export.youtube_disconnect"), QStringLiteral("Desconectar")},
      {QStringLiteral("export.youtube_not_configured"), QStringLiteral("ID de cliente OAuth no configurado. Vea config/youtube_oauth.json.example.")},
      {QStringLiteral("export.youtube_not_connected"), QStringLiteral("No conectado")},
      {QStringLiteral("export.youtube_connected"), QStringLiteral("Conectado como %1")},
      {QStringLiteral("export.youtube_playlist_target"), QStringLiteral("%1")},
      {QStringLiteral("export.youtube_requires_mp4"), QStringLiteral("La subida a YouTube requiere formato MP4 o MP4 + XML.")},
      {QStringLiteral("export.youtube_sign_in_required"), QStringLiteral("Conecte su cuenta de YouTube antes de exportar con subida activada.")},
      {QStringLiteral("export.youtube_resolving_playlist"), QStringLiteral("Resolviendo lista del partido en YouTube…")},
      {QStringLiteral("export.youtube_uploading"), QStringLiteral("Subiendo a YouTube…")},
      {QStringLiteral("export.youtube_upload_done"), QStringLiteral("Subida a YouTube completa.")},
      {QStringLiteral("export.youtube_upload_success"), QStringLiteral("Video subido a la lista del partido en YouTube.")},
      {QStringLiteral("export.youtube_upload_success_with_link"), QStringLiteral("Video subido a la lista del partido en YouTube.\n\n%1")},
      {QStringLiteral("export.youtube_open_video"), QStringLiteral("Abrir en YouTube")},
      {QStringLiteral("export.youtube_upload_failed"), QStringLiteral("Falló la subida a YouTube.")},
      {QStringLiteral("export.youtube_description_footer"), QStringLiteral("Exportado desde AVA.")},
      {QStringLiteral("concat.dialog_title"), QStringLiteral("Ordenar archivos de video")},
      {QStringLiteral("concat.move_left"), QStringLiteral("\u2190 Mover izq.")},
      {QStringLiteral("concat.move_right"), QStringLiteral("Mover der. \u2192")},
      {QStringLiteral("concat.continue_btn"), QStringLiteral("&Continuar")},
      {QStringLiteral("concat.cancel"), QStringLiteral("Cancelar")},
      {QStringLiteral("concat.preparing"), QStringLiteral("Combinando archivos de video\u2026")},
      {QStringLiteral("concat.error_ffmpeg"), QStringLiteral("Se necesita FFmpeg para combinar m\u00faltiples archivos de video.\nPor favor instale FFmpeg para continuar.\n\nhttps://ffmpeg.org")},
      {QStringLiteral("concat.error_failed"), QStringLiteral("Error al combinar archivos de video.")},
      {QStringLiteral("xml_import.title"), QStringLiteral("Importar XML")},
      {QStringLiteral("xml_import.select_file"), QStringLiteral("Seleccionar archivo XML para importar")},
      {QStringLiteral("xml_import.cancel"), QStringLiteral("Cancelar")},
      {QStringLiteral("xml_import.continue"), QStringLiteral("Continuar")},
      {QStringLiteral("xml_import.import"), QStringLiteral("Importar")},
      {QStringLiteral("xml_import.conflict_title"), QStringLiteral("Marcas existentes")},
      {QStringLiteral("xml_import.conflict_message"),
       QStringLiteral("Esta sesión ya tiene eventos etiquetados. ¿Reemplazarlos con el XML importado, combinar ambos conjuntos o cancelar?")},
      {QStringLiteral("xml_import.conflict_replace"), QStringLiteral("Reemplazar todas las marcas")},
      {QStringLiteral("xml_import.conflict_merge"), QStringLiteral("Combinar con marcas existentes")},
      {QStringLiteral("xml_import.sync_title"), QStringLiteral("Alinear línea de tiempo XML")},
      {QStringLiteral("xml_import.sync_instructions"),
       QStringLiteral("Desplace la línea de tiempo del video hasta el momento que coincide con el ancla de inicio del XML y continúe. La posición del cabezal de reproducción se usa como punto de sincronización.")},
      {QStringLiteral("xml_import.sync_fallback_warning"),
       QStringLiteral("No se encontró una marca Inicio en el XML. Se usa el primer cuarto o el evento más temprano como ancla.")},
      {QStringLiteral("xml_import.sync_xml_anchor"), QStringLiteral("Ancla XML (%2): %1")},
      {QStringLiteral("xml_import.sync_video_anchor"), QStringLiteral("Ancla de video: %1")},
      {QStringLiteral("xml_import.sync_use_playhead"), QStringLiteral("Usar cabezal actual")},
      {QStringLiteral("xml_import.sync_offset"), QStringLiteral("Desplazamiento: %1 s")},
      {QStringLiteral("xml_import.sync_preview_ok"), QStringLiteral("Se importarán %1 instancias.")},
      {QStringLiteral("xml_import.sync_preview_clamp"),
       QStringLiteral("Se importarán %1 instancias. %2 comenzarán antes del inicio del video; %3 se extenderán más allá del final (las marcas de tiempo se ajustarán).")},
      {QStringLiteral("xml_import.mapping_title"), QStringLiteral("Mapear códigos de evento XML")},
      {QStringLiteral("xml_import.mapping_instructions"),
       QStringLiteral("Revise cómo cada código XML se mapea a eventos de AVA. Ajuste cualquier fila antes de importar.")},
      {QStringLiteral("xml_import.mapping_abbrev_header"), QStringLiteral("Asigne abreviaturas XML a equipos de la sesión:")},
      {QStringLiteral("xml_import.mapping_home_abbrev"), QStringLiteral("Abreviatura local en XML:")},
      {QStringLiteral("xml_import.mapping_away_abbrev"), QStringLiteral("Abreviatura visitante en XML:")},
      {QStringLiteral("xml_import.mapping_col_code"), QStringLiteral("Código XML")},
      {QStringLiteral("xml_import.mapping_col_count"), QStringLiteral("Cantidad")},
      {QStringLiteral("xml_import.mapping_col_event"), QStringLiteral("Mapear a evento")},
      {QStringLiteral("xml_import.mapping_col_team"), QStringLiteral("Equipo")},
      {QStringLiteral("xml_import.mapping_col_action"), QStringLiteral("Acción")},
      {QStringLiteral("xml_import.mapping_team_none"), QStringLiteral("(ninguno)")},
      {QStringLiteral("xml_import.mapping_action_import"), QStringLiteral("Importar")},
      {QStringLiteral("xml_import.mapping_action_skip"), QStringLiteral("Omitir")},
      {QStringLiteral("xml_import.mapping_none_selected"), QStringLiteral("No hay instancias configuradas para importar.")},
      {QStringLiteral("xml_import.mapping_missing_event"), QStringLiteral("Elija un evento para el código: %1")},
      {QStringLiteral("xml_import.mapping_missing_team"), QStringLiteral("Elija un equipo para el código: %1")},
      {QStringLiteral("xml_import.complete_summary"),
       QStringLiteral("Se importaron %1 eventos (%2 omitidos, %3 ajustados a los límites del video).")},
  };
  return es.value(QLatin1String(key), QLatin1String(key));
}

} // namespace AppLocale

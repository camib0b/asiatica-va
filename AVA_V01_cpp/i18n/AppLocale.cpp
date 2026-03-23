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
      {QStringLiteral("16-yd play"), QStringLiteral("Salida 16")},
      {QStringLiteral("50-yd play"), QStringLiteral("Juego 50")},
      {QStringLiteral("75-yd play"), QStringLiteral("Juego 75")},
      {QStringLiteral("Circle Entry"), QStringLiteral("Ingreso área")},
      {QStringLiteral("Shot"), QStringLiteral("Tiro")},
      {QStringLiteral("Goal"), QStringLiteral("Gol")},
      {QStringLiteral("PC"), QStringLiteral("Corto")},
      {QStringLiteral("PS"), QStringLiteral("Penal")},
      {QStringLiteral("Pass"), QStringLiteral("Pase")},
      {QStringLiteral("Special"), QStringLiteral("Especial")},
      {QStringLiteral("Turnover"), QStringLiteral("Pérdida")},
      {QStringLiteral("Card"), QStringLiteral("Tarjeta")},
      {QStringLiteral("PC Foul"), QStringLiteral("Falta de PC")},

      // First-level follow-ups
      {QStringLiteral("On target"), QStringLiteral("Al arco")},
      {QStringLiteral("Off target"), QStringLiteral("Afuera")},
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
      {QStringLiteral("Off"), QStringLiteral("Ofensiva")},
      {QStringLiteral("Def"), QStringLiteral("Defensiva")},

      // Second / third level
      {QStringLiteral("Saved"), QStringLiteral("Atajado")},
      {QStringLiteral("Post"), QStringLiteral("Palo")},
      {QStringLiteral("Closeby"), QStringLiteral("Cerca")},
      {QStringLiteral("Not close"), QStringLiteral("Lejos")},
      {QStringLiteral("Swept"), QStringLiteral("Barrida")},
      {QStringLiteral("Dragflick"), QStringLiteral("Arrastre")},
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
      {QStringLiteral("Other"), QStringLiteral("Otro")},
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
        {QStringLiteral("welcome.title"), QStringLiteral("this is ava")},
        {QStringLiteral("welcome.subtitle"), QStringLiteral("Import a video file to get started")},
        {QStringLiteral("welcome.import"), QStringLiteral("&Select video file")},
        {QStringLiteral("setup.title"), QStringLiteral("Set up teams")},
        {QStringLiteral("setup.subtitle"), QStringLiteral("Enter team names and colors for this session.")},
        {QStringLiteral("setup.home_team"), QStringLiteral("Home team:")},
        {QStringLiteral("setup.away_team"), QStringLiteral("Away team:")},
        {QStringLiteral("setup.home_color"), QStringLiteral("Home color:")},
        {QStringLiteral("setup.away_color"), QStringLiteral("Away color:")},
        {QStringLiteral("setup.placeholder_team"), QStringLiteral("e.g. Lakers")},
        {QStringLiteral("setup.placeholder_hex"), QStringLiteral("#RRGGBB")},
        {QStringLiteral("setup.pick"), QStringLiteral("Pick")},
        {QStringLiteral("setup.back"), QStringLiteral("&Back")},
        {QStringLiteral("setup.continue"), QStringLiteral("&Continue")},
        {QStringLiteral("setup.lang_label"), QStringLiteral("Language:")},
        {QStringLiteral("setup.lang_en"), QStringLiteral("English")},
        {QStringLiteral("setup.lang_es"), QStringLiteral("Español")},
        {QStringLiteral("dialog.pick_home_color"), QStringLiteral("Home team color")},
        {QStringLiteral("dialog.pick_away_color"), QStringLiteral("Away team color")},
        {QStringLiteral("file.select_video"), QStringLiteral("Select a video file")},
        {QStringLiteral("file.video_filter"), QStringLiteral("Video files (*.mp4 *.mov *.m4v *.mkv *.avi);;All files (*.*)")},
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
        {QStringLiteral("vc.tt.slower"), QStringLiteral("{  Slower")},
        {QStringLiteral("vc.tt.faster"), QStringLiteral("}  Faster")},
        {QStringLiteral("vc.tt.reset"), QStringLiteral("\\  Reset speed")},
        {QStringLiteral("menu.export_clips"), QStringLiteral("Export clips…")},
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
        {QStringLiteral("export.before_tag"), QStringLiteral("Before tag:")},
        {QStringLiteral("export.after_tag"), QStringLiteral("After tag:")},
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
    };
    return en.value(QLatin1String(key), QLatin1String(key));
  }

  static const QHash<QString, QString> es = {
      {QStringLiteral("app.title"), QStringLiteral("AVA | Camila Escudero")},
      {QStringLiteral("welcome.title"), QStringLiteral("esto es ava")},
      {QStringLiteral("welcome.subtitle"), QStringLiteral("Sube un video para empezar")},
      {QStringLiteral("welcome.import"), QStringLiteral("&Elegir video")},
      {QStringLiteral("setup.title"), QStringLiteral("Configurar equipos")},
      {QStringLiteral("setup.subtitle"),
       QStringLiteral("Introduce nombres y colores de equipo para esta sesión.")},
      {QStringLiteral("setup.home_team"), QStringLiteral("Equipo local:")},
      {QStringLiteral("setup.away_team"), QStringLiteral("Equipo visitante:")},
      {QStringLiteral("setup.home_color"), QStringLiteral("Color local:")},
      {QStringLiteral("setup.away_color"), QStringLiteral("Color visitante:")},
      {QStringLiteral("setup.placeholder_team"), QStringLiteral("ej. Lakers")},
      {QStringLiteral("setup.placeholder_hex"), QStringLiteral("#RRGGBB")},
      {QStringLiteral("setup.pick"), QStringLiteral("Elegir")},
      {QStringLiteral("setup.back"), QStringLiteral("&Atrás")},
      {QStringLiteral("setup.continue"), QStringLiteral("&Continuar")},
      {QStringLiteral("setup.lang_label"), QStringLiteral("Idioma:")},
      {QStringLiteral("setup.lang_en"), QStringLiteral("English")},
      {QStringLiteral("setup.lang_es"), QStringLiteral("Español")},
      {QStringLiteral("dialog.pick_home_color"), QStringLiteral("Color del equipo local")},
      {QStringLiteral("dialog.pick_away_color"), QStringLiteral("Color del equipo visitante")},
      {QStringLiteral("file.select_video"), QStringLiteral("Seleccionar archivo de video")},
      {QStringLiteral("file.video_filter"),
       QStringLiteral("Vídeo (*.mp4 *.mov *.m4v *.mkv *.avi);;Todos los archivos (*.*)")},
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
      {QStringLiteral("vc.tt.slower"), QStringLiteral("{  Más lento")},
      {QStringLiteral("vc.tt.faster"), QStringLiteral("}  Más rápido")},
      {QStringLiteral("vc.tt.reset"), QStringLiteral("\\  Restablecer velocidad")},
      {QStringLiteral("menu.export_clips"), QStringLiteral("Exportar clips…")},
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
      {QStringLiteral("export.before_tag"), QStringLiteral("Antes de la marca:")},
      {QStringLiteral("export.after_tag"), QStringLiteral("Después de la marca:")},
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
  };
  return es.value(QLatin1String(key), QLatin1String(key));
}

} // namespace AppLocale

#include "AppLocale.h"
#include "LocaleNotifier.h"

#include <QHash>
#include <QSettings>
#include <QStringList>

#include <array>
#include <mutex>

namespace {

constexpr char kSettingsGroup[] = "ui";
constexpr char kLanguageKey[] = "language";

constexpr QLatin1StringView kDefaultHomeFollowUpLabel("home");
constexpr QLatin1StringView kDefaultAwayFollowUpLabel("away");
constexpr QLatin1StringView kHomeTeamSideKey("Home");
constexpr QLatin1StringView kAwayTeamSideKey("Away");

class LanguageStore {
public:
  static LanguageStore& instance() {
    static LanguageStore store;
    return store;
  }

  AppLocale::Language current() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return language_;
  }

  bool setLanguageAndPersist(AppLocale::Language language) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (language_ == language) return false;
    language_ = language;
    writeSettingsLocked();
    return true;
  }

  bool loadFromSettings() {
    std::lock_guard<std::mutex> lock(mutex_);
    const AppLocale::Language loaded = readSettingsLocked();
    if (language_ == loaded) return false;
    language_ = loaded;
    return true;
  }

  void saveToSettings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    writeSettingsLocked();
  }

private:
  LanguageStore() = default;

  AppLocale::Language readSettingsLocked() const {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const QString value =
        settings.value(QLatin1String(kLanguageKey), QStringLiteral("en")).toString();
    settings.endGroup();
    return value == QLatin1String("es") ? AppLocale::Language::Spanish
                                        : AppLocale::Language::English;
  }

  void writeSettingsLocked() const {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.setValue(QLatin1String(kLanguageKey),
                      language_ == AppLocale::Language::Spanish ? QStringLiteral("es")
                                                               : QStringLiteral("en"));
    settings.endGroup();
  }

  mutable std::mutex mutex_;
  AppLocale::Language language_ = AppLocale::Language::English;
};

void notifyLanguageChangedIf(bool changed) {
  if (!changed) return;
  LocaleNotifier::instance().notifyLanguageChanged();
}

struct SpanishEventEntry {
  const char* canonical;
  const char* spanish;
};

constexpr std::array<SpanishEventEntry, 63> kSpanishEventEntries{{
    // Main grid
    {"Circle Entry", "Ingreso área"},
    {"Shot", "Tiro"},
    {"Goal", "Gol"},
    {"PC", "Corto"},
    {"PS", "Penal"},
    {"S.O.", "S.O."},
    {"Pass", "Pase"},
    {"Turnover", "Pérdida"},
    {"Card", "Tarjeta"},
    {"PC Foul", "Falta PC"},

    // First-level follow-ups
    {"On target", "Al arco"},
    {"Off target", "Afuera"},
    {"Blocked", "Bloqueado"},
    {"For", "A favor"},
    {"Against", "En contra"},
    {"Direct shot", "Directo"},
    {"Variant", "Variante"},
    {"Ruined", "Arruinado"},
    {"Forward", "Hacia adelante"},
    {"Sideways", "Hacia el lado"},
    {"Back", "Hacia atrás"},
    {"Green", "Verde"},
    {"Yellow", "Amarilla"},
    {"Red", "Roja"},
    {"Flick", "Flick"},
    {"Push", "Push"},
    {"Sweep", "Barrida"},
    {"Hit", "Pegada"},
    {"Good", "Positivo"},
    {"Bad", "Negativo"},
    {"Neutral", "Neutro"},
    {"Referee", "Arbitraje"},
    {"Off", "Ofensiva"},
    {"Def", "Defensiva"},

    // Second / third level
    {"Saved", "Atajado"},
    {"Post", "Palo"},
    {"Closeby", "Cerca"},
    {"Not close", "Lejos"},
    {"Swept", "Barrida"},
    {"Dragflick", "Arrastre"},
    {"New PC", "Nuevo corto"},
    {"Dribbling", "Conducción"},
    {"Deflection", "Desvío"},
    {"Completed", "Completado"},
    {"Failed", "Fallido"},
    {"Interception", "Intercepción"},
    {"Tackle", "Quite"},
    {"Pressure", "Presión"},
    {"Unforced error", "Error"},
    {"Foot", "Pie"},
    {"Stick", "Palo"},
    {"Danger", "Peligro"},
    {"Other", "Otro"},
    {"Converted", "Convertido"},
    {"Missed", "No convertido"},
    {"Replay", "Repite"},
    {"Left", "Izquierda"},
    {"Middle", "Centro"},
    {"Right", "Derecha"},
    {"3 man", "de 3"},
    {"4 man", "de 4"},
    // Default follow-up team labels (when names empty; English canonical tokens)
    {"home", "Local"},
    {"away", "Visita"},
}};

const QHash<QString, QString>& spanishEventMap() {
  static const QHash<QString, QString> map = [] {
    QHash<QString, QString> built;
    built.reserve(static_cast<int>(kSpanishEventEntries.size()));
    for (const SpanishEventEntry& entry : kSpanishEventEntries) {
      const QString key = QString::fromUtf8(entry.canonical);
      Q_ASSERT(!built.contains(key));
      built.insert(key, QString::fromUtf8(entry.spanish));
    }
    Q_ASSERT(built.size() == static_cast<int>(kSpanishEventEntries.size()));
    return built;
  }();
  return map;
}

QString translateEventForLanguage(const QString& canonicalToken, AppLocale::Language language) {
  const QString key = canonicalToken.trimmed();
  if (key.isEmpty()) return key;
  if (QString::compare(key, QStringLiteral("Special"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("☆");
  }
  if (language != AppLocale::Language::Spanish) return key;
  const auto& map = spanishEventMap();
  const auto iterator = map.constFind(key);
  if (iterator == map.cend()) return key;
  return iterator.value();
}

struct UiTranslation {
  QString english;
  QString spanish;
};

const QHash<QString, UiTranslation>& uiTranslationTable() {
  static const QHash<QString, UiTranslation> table = {
      {QStringLiteral("app.title"),
       {QStringLiteral("AVA | Camila Escudero"),
        QStringLiteral("AVA | Camila Escudero")}},
      {QStringLiteral("welcome.import"),
       {QStringLiteral("&Select video file(s)"),
        QStringLiteral("&Elegir video(s)")}},
      {QStringLiteral("setup.title"), {QStringLiteral("Set up teams"), QStringLiteral("Configurar equipos")}},
      {QStringLiteral("setup.home_team"), {QStringLiteral("Home team"), QStringLiteral("Equipo local")}},
      {QStringLiteral("setup.away_team"), {QStringLiteral("Away team"), QStringLiteral("Equipo visitante")}},
      {QStringLiteral("setup.home_color"), {QStringLiteral("Home color:"), QStringLiteral("Color local:")}},
      {QStringLiteral("setup.away_color"), {QStringLiteral("Away color:"), QStringLiteral("Color visitante:")}},
      {QStringLiteral("setup.placeholder_team"), {QStringLiteral("e.g. Lakers"), QStringLiteral("ej. Lakers")}},
      {QStringLiteral("setup.placeholder_home_team"), {QStringLiteral("Home team"), QStringLiteral("Equipo local")}},
      {QStringLiteral("setup.placeholder_away_team"),
       {QStringLiteral("Away team"),
        QStringLiteral("Equipo visitante")}},
      {QStringLiteral("setup.placeholder_hex"), {QStringLiteral("#RRGGBB"), QStringLiteral("#RRGGBB")}},
      {QStringLiteral("setup.placeholder_abbrev"), {QStringLiteral("3 letters"), QStringLiteral("3 letras")}},
      {QStringLiteral("setup.placeholder_competition"),
       {QStringLiteral("e.g. World Cup"),
        QStringLiteral("ej. Mundial")}},
      {QStringLiteral("setup.pick"), {QStringLiteral("Pick"), QStringLiteral("Elegir")}},
      {QStringLiteral("setup.back"), {QStringLiteral("&Back"), QStringLiteral("&Atrás")}},
      {QStringLiteral("setup.continue"), {QStringLiteral("&Continue"), QStringLiteral("&Continuar")}},
      {QStringLiteral("setup.continue_disabled_hint"),
       {QStringLiteral("Select a video file to continue."),
        QStringLiteral("Selecciona un archivo de video para continuar.")}},
      {QStringLiteral("setup.lang_label"), {QStringLiteral("Language:"), QStringLiteral("Idioma:")}},
      {QStringLiteral("setup.lang_en"), {QStringLiteral("English"), QStringLiteral("English")}},
      {QStringLiteral("setup.lang_es"), {QStringLiteral("Español"), QStringLiteral("Español")}},
      {QStringLiteral("setup.competition"), {QStringLiteral("Competition"), QStringLiteral("Competición")}},
      {QStringLiteral("setup.home_abbrev"), {QStringLiteral("Home abbrev:"), QStringLiteral("Sigla local:")}},
      {QStringLiteral("setup.away_abbrev"), {QStringLiteral("Away abbrev:"), QStringLiteral("Sigla visita:")}},
      {QStringLiteral("setup.ai_status_teams"),
       {QStringLiteral("Identifying teams from the file name…"),
        QStringLiteral("Identificando equipos a partir del nombre de archivo…")}},
      {QStringLiteral("setup.ai_status_colors"),
       {QStringLiteral("Detecting kit colors…"),
        QStringLiteral("Detectando colores de camiseta…")}},
      {QStringLiteral("setup.color_more"), {QStringLiteral("More colors…"), QStringLiteral("Más colores…")}},
      {QStringLiteral("setup.color_choose"), {QStringLiteral("Choose color"), QStringLiteral("Elegir color")}},
      {QStringLiteral("setup.color_code"), {QStringLiteral("Color code"), QStringLiteral("Código de color")}},
      {QStringLiteral("setup.color_red"), {QStringLiteral("Red"), QStringLiteral("Rojo")}},
      {QStringLiteral("setup.color_light_blue"), {QStringLiteral("Light blue"), QStringLiteral("Azul claro")}},
      {QStringLiteral("setup.color_dark_blue"), {QStringLiteral("Dark blue"), QStringLiteral("Azul oscuro")}},
      {QStringLiteral("setup.color_yellow"), {QStringLiteral("Yellow"), QStringLiteral("Amarillo")}},
      {QStringLiteral("setup.color_gray"), {QStringLiteral("Gray"), QStringLiteral("Gris")}},
      {QStringLiteral("setup.color_brown"), {QStringLiteral("Brown"), QStringLiteral("Marrón")}},
      {QStringLiteral("setup.color_white"), {QStringLiteral("White"), QStringLiteral("Blanco")}},
      {QStringLiteral("setup.color_black"), {QStringLiteral("Black"), QStringLiteral("Negro")}},
      {QStringLiteral("setup.color_green"), {QStringLiteral("Green"), QStringLiteral("Verde")}},
      {QStringLiteral("setup.color_pink"), {QStringLiteral("Pink"), QStringLiteral("Rosa")}},
      {QStringLiteral("gamecontrols.start_game"),
       {QStringLiteral("Start game (G)"),
        QStringLiteral("Iniciar partido (G)")}},
      {QStringLiteral("gamecontrols.start_q1"), {QStringLiteral("Start Q1"), QStringLiteral("Iniciar Q1")}},
      {QStringLiteral("gamecontrols.start_q2"), {QStringLiteral("Start Q2 (H)"), QStringLiteral("Iniciar Q2 (H)")}},
      {QStringLiteral("gamecontrols.start_q3"), {QStringLiteral("Start Q3 (H)"), QStringLiteral("Iniciar Q3 (H)")}},
      {QStringLiteral("gamecontrols.start_q4"), {QStringLiteral("Start Q4 (H)"), QStringLiteral("Iniciar Q4 (H)")}},
      {QStringLiteral("gamecontrols.end_game"),
       {QStringLiteral("End game (H)"),
        QStringLiteral("Fin del partido (H)")}},
      {QStringLiteral("gamecontrols.quarter_not_started"), {QStringLiteral("--"), QStringLiteral("--")}},
      {QStringLiteral("gamecontrols.quarter_ended"), {QStringLiteral("end"), QStringLiteral("fin")}},
      {QStringLiteral("dialog.pick_home_color"),
       {QStringLiteral("Home team color"),
        QStringLiteral("Color del equipo local")}},
      {QStringLiteral("dialog.pick_away_color"),
       {QStringLiteral("Away team color"),
        QStringLiteral("Color del equipo visitante")}},
      {QStringLiteral("file.select_video"),
       {QStringLiteral("Select a video file"),
        QStringLiteral("Seleccionar archivo de video")}},
      {QStringLiteral("file.video_filter"),
       {QStringLiteral("Video files (*.mp4 *.mov *.m4v *.mkv *.avi *.mts *.MTS);;All files (*.*)"),
        QStringLiteral("Vídeo (*.mp4 *.mov *.m4v *.mkv *.avi *.mts *.MTS);;Todos los archivos (*.*)")}},
      {QStringLiteral("file.xml_filter"),
       {QStringLiteral("XML files (*.xml);;All files (*.*)"),
        QStringLiteral("Archivos XML (*.xml);;Todos los archivos (*.*)")}},
      {QStringLiteral("mode.tagging"), {QStringLiteral("Tagging"), QStringLiteral("Etiquetado")}},
      {QStringLiteral("mode.analyzing"), {QStringLiteral("Analyzing"), QStringLiteral("Análisis")}},
      {QStringLiteral("mode.presenting"), {QStringLiteral("Presentation"), QStringLiteral("Presentación")}},
      {QStringLiteral("tooltip.mode_tagging"),
       {QStringLiteral("Eyes on video, hands on keyboard (M)"),
        QStringLiteral("Ojos en el vídeo, manos en el teclado (M)")}},
      {QStringLiteral("tooltip.mode_analyzing"),
       {QStringLiteral("Stats and notes (M)"),
        QStringLiteral("Estadísticas y notas (M)")}},
      {QStringLiteral("tooltip.mode_presenting"),
       {QStringLiteral("Show selected clips on a big player (M)"),
        QStringLiteral("Mostrar los clips elegidos en un reproductor grande (M)")}},
      {QStringLiteral("presentation.panel_title"),
       {QStringLiteral("Selected clips"),
        QStringLiteral("Clips seleccionados")}},
      {QStringLiteral("presentation.filter_event"), {QStringLiteral("Event:"), QStringLiteral("Evento:")}},
      {QStringLiteral("presentation.filter_team"), {QStringLiteral("Team:"), QStringLiteral("Equipo:")}},
      {QStringLiteral("presentation.team_all"), {QStringLiteral("Both"), QStringLiteral("Ambos")}},
      {QStringLiteral("presentation.all_events"), {QStringLiteral("All events"), QStringLiteral("Todos los eventos")}},
      {QStringLiteral("presentation.select_all"), {QStringLiteral("Select all"), QStringLiteral("Seleccionar todo")}},
      {QStringLiteral("presentation.select_none"), {QStringLiteral("Clear"), QStringLiteral("Quitar")}},
      {QStringLiteral("presentation.selection_summary"),
       {QStringLiteral("%1 of %2 listed selected (%3 total)"),
        QStringLiteral("%1 de %2 en lista seleccionados (%3 total)")}},
      {QStringLiteral("presentation.current_clip"), {QStringLiteral("Current clip"), QStringLiteral("Clip actual")}},
      {QStringLiteral("presentation.apply_to_all"),
       {QStringLiteral("Apply to all selected clips"),
        QStringLiteral("Aplicar a todos los clips seleccionados")}},
      {QStringLiteral("presentation.apply_to_all_tooltip"),
       {QStringLiteral("Gives every selected clip the same lead and lag times around its own event mark."),
        QStringLiteral("Da a cada clip seleccionado la misma anticipación y retardo alrededor de su propia marca.")}},
      {QStringLiteral("presentation.show_notes"),
       {QStringLiteral("Show notes on screen"),
        QStringLiteral("Mostrar notas en pantalla")}},
      {QStringLiteral("presentation.export"),
       {QStringLiteral("Export selected clips"),
        QStringLiteral("Exportar clips seleccionados")}},
      {QStringLiteral("presentation.keyboard_hint"),
       {QStringLiteral("Tab: next clip · Shift+Tab: previous clip · Space: play/pause · Arrows: seek"),
        QStringLiteral("Tab: clip siguiente · Shift+Tab: clip anterior · Espacio: reproducir/pausar · Flechas: avanzar")}},
      {QStringLiteral("presentation.empty_title"),
       {QStringLiteral("No clips selected"),
        QStringLiteral("Selecciona clips en el panel izquierdo")}},
      {QStringLiteral("presentation.empty_hint"),
       {QStringLiteral("Use Tab and Shift Tab to navigate between the clips you select."),
        QStringLiteral("Ocupa Tab y Shift Tab para navegar entre los clips que selecciones.")}},
      {QStringLiteral("tooltip.video_menu"), {QStringLiteral("Video Manager"), QStringLiteral("Video")}},
      {QStringLiteral("menu.replace_video"),
       {QStringLiteral("Replace video with another one"),
        QStringLiteral("Sustituir video por otro")}},
      {QStringLiteral("menu.close_video"),
       {QStringLiteral("Close current video"),
        QStringLiteral("Cerrar video actual")}},
      {QStringLiteral("tags.header"), {QStringLiteral("Tags"), QStringLiteral("Marcas")}},
      {QStringLiteral("tags.filter"), {QStringLiteral("Filter"), QStringLiteral("Filtrar")}},
      {QStringLiteral("tags.remove_filters"), {QStringLiteral("Remove filters"), QStringLiteral("Quitar filtros")}},
      {QStringLiteral("tags.undo"), {QStringLiteral("Undo"), QStringLiteral("Deshacer")}},
      {QStringLiteral("tags.undo_tooltip"),
       {QStringLiteral("Ctrl+Z  Remove most recent tag"),
        QStringLiteral("Ctrl+Z  Quitar la última marca")}},
      {QStringLiteral("tags.note_placeholder"),
       {QStringLiteral("Note for selected tag…"),
        QStringLiteral("Nota sobre el clip seleccionado…")}},
      {QStringLiteral("notes.match_title"), {QStringLiteral("Match notes"), QStringLiteral("Notas del partido")}},
      {QStringLiteral("notes.match_placeholder"),
       {QStringLiteral("Match notes… Type @ to mention a clip"),
        QStringLiteral("Notas del partido… Escribe @ para mencionar un clip")}},
      {QStringLiteral("notes.clip_title"), {QStringLiteral("Clip note"), QStringLiteral("Nota del clip")}},
      {QStringLiteral("notes.clip_placeholder_none"),
       {QStringLiteral("Select a tag to add a clip note…"),
        QStringLiteral("Selecciona una marca para añadir una nota de clip…")}},
      {QStringLiteral("notes.mention_empty"),
       {QStringLiteral("No clips to mention"),
        QStringLiteral("No hay clips para mencionar")}},
      {QStringLiteral("tags.col_time"), {QStringLiteral("Time"), QStringLiteral("Tiempo")}},
      {QStringLiteral("tags.col_team"), {QStringLiteral("Team"), QStringLiteral("Equipo")}},
      {QStringLiteral("tags.col_event"), {QStringLiteral("Event"), QStringLiteral("Evento")}},
      {QStringLiteral("filter.select_all"), {QStringLiteral("Select all"), QStringLiteral("Seleccionar todo")}},
      {QStringLiteral("filter.select_none"), {QStringLiteral("Select none"), QStringLiteral("Seleccionar ninguno")}},
      {QStringLiteral("filter.indicator"), {QStringLiteral("Filtered by: "), QStringLiteral("Filtrado por: ")}},
      {QStringLiteral("stats.header"), {QStringLiteral("Stats"), QStringLiteral("Estadísticas")}},
      {QStringLiteral("stats.col_event"), {QStringLiteral("Event"), QStringLiteral("Evento")}},
      {QStringLiteral("stats.col_count"), {QStringLiteral("Count"), QStringLiteral("Cantidad")}},
      {QStringLiteral("stats.filter_home_fallback"), {QStringLiteral("Home"), QStringLiteral("Local")}},
      {QStringLiteral("stats.filter_away_fallback"), {QStringLiteral("Away"), QStringLiteral("Visitante")}},
      {QStringLiteral("stats.filter_both"), {QStringLiteral("Both"), QStringLiteral("Ambos")}},
      {QStringLiteral("stats.overlay_title"),
       {QStringLiteral("Stats — Tag taxonomy"),
        QStringLiteral("Estadísticas")}},
      {QStringLiteral("stats_overlay.tooltip"),
       {QStringLiteral("Stats overlay (,)"),
        QStringLiteral("Superposición de estadísticas (,)")}},
      {QStringLiteral("vc.play"), {QStringLiteral("Play"), QStringLiteral("Reproducir")}},
      {QStringLiteral("vc.pause"), {QStringLiteral("Pause"), QStringLiteral("Pausa")}},
      {QStringLiteral("vc.back"), {QStringLiteral("⟵ 2s"), QStringLiteral("⟵ 2s")}},
      {QStringLiteral("vc.forward"), {QStringLiteral("2s ⟶"), QStringLiteral("2s ⟶")}},
      {QStringLiteral("vc.slower"), {QStringLiteral("Slower"), QStringLiteral("Más lento")}},
      {QStringLiteral("vc.reset_speed"), {QStringLiteral("Reset 1.0x"), QStringLiteral("Restablecer 1.0x")}},
      {QStringLiteral("vc.faster"), {QStringLiteral("Faster"), QStringLiteral("Más rápido")}},
      {QStringLiteral("vc.mute"), {QStringLiteral("Mute"), QStringLiteral("Silenciar")}},
      {QStringLiteral("vc.unmute"), {QStringLiteral("Unmute"), QStringLiteral("Activar audio")}},
      {QStringLiteral("vc.speed_label"), {QStringLiteral("Speed: %1"), QStringLiteral("Velocidad: %1")}},
      {QStringLiteral("vc.tt.play"), {QStringLiteral("space  Play"), QStringLiteral("espacio  Reproducir")}},
      {QStringLiteral("vc.tt.pause"), {QStringLiteral("space  Pause"), QStringLiteral("espacio  Pausa")}},
      {QStringLiteral("vc.tt.back"), {QStringLiteral("⟵  Back"), QStringLiteral("⟵  Atrás")}},
      {QStringLiteral("vc.tt.forward"), {QStringLiteral("⟶  Forward"), QStringLiteral("⟶  Adelante")}},
      {QStringLiteral("vc.tt.slower"), {QStringLiteral("-  Slower"), QStringLiteral("-  Más lento")}},
      {QStringLiteral("vc.tt.faster"), {QStringLiteral("+  Faster"), QStringLiteral("+  Más rápido")}},
      {QStringLiteral("vc.tt.reset"), {QStringLiteral("}  Reset speed"), QStringLiteral("}  Restablecer velocidad")}},
      {QStringLiteral("vc.tt.mute"), {QStringLiteral("shift+m  Mute"), QStringLiteral("shift+m  Silenciar")}},
      {QStringLiteral("vc.tt.unmute"), {QStringLiteral("shift+m  Unmute"), QStringLiteral("shift+m  Activar audio")}},
      {QStringLiteral("menu.export_clips"), {QStringLiteral("Export clips…"), QStringLiteral("Exportar clips…")}},
      {QStringLiteral("menu.import_xml"), {QStringLiteral("Import XML…"), QStringLiteral("Importar XML…")}},
      {QStringLiteral("menu.clip_durations"),
       {QStringLiteral("Clip durations…"),
        QStringLiteral("Duraciones de clip…")}},
      {QStringLiteral("clip_durations.title"),
       {QStringLiteral("Clip Durations"),
        QStringLiteral("Duraciones de clip")}},
      {QStringLiteral("clip_durations.subtitle"),
       {QStringLiteral("Default lead and lag times around each tag when creating clips. Changes apply immediately to untrimmed tags."),
        QStringLiteral("Tiempos de anticipación y retardo predeterminados alrededor de cada marca al crear clips. Los cambios se aplican de inmediato a marcas sin recorte.")}},
      {QStringLiteral("clip_durations.col_event"), {QStringLiteral("Event"), QStringLiteral("Evento")}},
      {QStringLiteral("clip_durations.col_lead"), {QStringLiteral("Lead (s)"), QStringLiteral("Anticipación (s)")}},
      {QStringLiteral("clip_durations.col_lag"), {QStringLiteral("Lag (s)"), QStringLiteral("Retardo (s)")}},
      {QStringLiteral("clip_durations.col_total"), {QStringLiteral("Total"), QStringLiteral("Total")}},
      {QStringLiteral("clip_durations.reset"),
       {QStringLiteral("Reset all to defaults"),
        QStringLiteral("Restablecer valores predeterminados")}},
      {QStringLiteral("clip_durations.close"), {QStringLiteral("Close"), QStringLiteral("Cerrar")}},
      {QStringLiteral("export.title"), {QStringLiteral("Export Clips"), QStringLiteral("Exportar clips")}},
      {QStringLiteral("export.subtitle"),
       {QStringLiteral("Export the clips selected in presentation mode, using each clip's current lead and lag times."),
        QStringLiteral("Exportar los clips seleccionados en modo presentación, usando la anticipación y el retardo actuales de cada clip.")}},
      {QStringLiteral("export.event_type"), {QStringLiteral("Event type:"), QStringLiteral("Tipo de evento:")}},
      {QStringLiteral("export.clips_label"), {QStringLiteral("clips to export"), QStringLiteral("clips a exportar")}},
      {QStringLiteral("export.team_label"), {QStringLiteral("Team:"), QStringLiteral("Equipo:")}},
      {QStringLiteral("export.team_all"), {QStringLiteral("All teams"), QStringLiteral("Todos los equipos")}},
      {QStringLiteral("export.team_home_default"), {QStringLiteral("Home"), QStringLiteral("Local")}},
      {QStringLiteral("export.team_away_default"), {QStringLiteral("Away"), QStringLiteral("Visitante")}},
      {QStringLiteral("export.sort_order"), {QStringLiteral("Sort order:"), QStringLiteral("Orden:")}},
      {QStringLiteral("export.sort_chronological"), {QStringLiteral("Chronological"), QStringLiteral("Cronológico")}},
      {QStringLiteral("export.sort_by_team"),
       {QStringLiteral("By team, then chronological"),
        QStringLiteral("Por equipo, luego cronológico")}},
      {QStringLiteral("export.overlay_language"),
       {QStringLiteral("Overlay language:"),
        QStringLiteral("Idioma del overlay:")}},
      {QStringLiteral("export.include_bottom_overlay"),
       {QStringLiteral("Include bottom tag overlay"),
        QStringLiteral("Incluir overlay de etiqueta inferior")}},
      {QStringLiteral("export.include_scoreboard_overlay"),
       {QStringLiteral("Include scoreboard overlay"),
        QStringLiteral("Incluir overlay de marcador")}},
      {QStringLiteral("export.include_audio_track"),
       {QStringLiteral("Include clip audio track"),
        QStringLiteral("Incluir audio del clip")}},
      {QStringLiteral("export.include_ava_overlay"),
       {QStringLiteral("Show AVA overlay in top-right corner"),
        QStringLiteral("Mostrar overlay de AVA en la esquina superior derecha")}},
      {QStringLiteral("export.before_tag"), {QStringLiteral("Before tag:"), QStringLiteral("Antes de la marca:")}},
      {QStringLiteral("export.after_tag"), {QStringLiteral("After tag:"), QStringLiteral("Después de la marca:")}},
      {QStringLiteral("export.event_durations_header"),
       {QStringLiteral("Per-event clip durations"),
        QStringLiteral("Duraciones por tipo de evento")}},
      {QStringLiteral("export.event_duration_pre_prefix"), {QStringLiteral("pre "), QStringLiteral("antes ")}},
      {QStringLiteral("export.event_duration_post_prefix"), {QStringLiteral("post "), QStringLiteral("después ")}},
      {QStringLiteral("export.output_format"),
       {QStringLiteral("Output format:"),
        QStringLiteral("Formato de salida:")}},
      {QStringLiteral("export.format_mp4"), {QStringLiteral("MP4 video"), QStringLiteral("Video MP4")}},
      {QStringLiteral("export.format_xml"), {QStringLiteral("XML report"), QStringLiteral("Reporte XML")}},
      {QStringLiteral("export.format_both"),
       {QStringLiteral("MP4 video + XML report"),
        QStringLiteral("Video MP4 + reporte XML")}},
      {QStringLiteral("export.export_xml_now"),
       {QStringLiteral("Export XML \u2192"),
        QStringLiteral("Exportar XML \u2192")}},
      {QStringLiteral("export.xml_instances_label"),
       {QStringLiteral("tags exported in XML report"),
        QStringLiteral("marcas exportadas en el reporte XML")}},
      {QStringLiteral("export.xml_filters_ignored_tooltip"),
       {QStringLiteral("XML reports export every tag in the session; MP4 compilations use the clips selected in presentation mode."),
        QStringLiteral("Los reportes XML exportan todas las marcas de la sesión; las compilaciones MP4 usan los clips seleccionados en modo presentación.")}},
      {QStringLiteral("export.xml_report_filename_segment"),
       {QStringLiteral("XML report"),
        QStringLiteral("reporte XML")}},
      {QStringLiteral("export.xml_success"),
       {QStringLiteral("XML report exported successfully."),
        QStringLiteral("Reporte XML exportado correctamente.")}},
      {QStringLiteral("export.xml_failed"),
       {QStringLiteral("Failed to write the XML report."),
        QStringLiteral("No se pudo escribir el reporte XML.")}},
      {QStringLiteral("export.output_placeholder_xml"),
       {QStringLiteral("Choose output .xml file…"),
        QStringLiteral("Elegir archivo .xml de salida…")}},
      {QStringLiteral("export.output_placeholder_both"),
       {QStringLiteral("Choose output .mp4 file (.xml saved alongside)…"),
        QStringLiteral("Elegir archivo .mp4 (el .xml se guarda al lado)…")}},
      {QStringLiteral("export.save_to"), {QStringLiteral("Save to:"), QStringLiteral("Guardar en:")}},
      {QStringLiteral("export.output_placeholder"),
       {QStringLiteral("Choose output file…"),
        QStringLiteral("Elegir archivo de salida…")}},
      {QStringLiteral("export.browse"), {QStringLiteral("Browse…"), QStringLiteral("Buscar…")}},
      {QStringLiteral("export.save_dialog_title"),
       {QStringLiteral("Save exported video"),
        QStringLiteral("Guardar video exportado")}},
      {QStringLiteral("export.export"), {QStringLiteral("Export"), QStringLiteral("Exportar")}},
      {QStringLiteral("export.cancel"), {QStringLiteral("Cancel"), QStringLiteral("Cancelar")}},
      {QStringLiteral("export.close"), {QStringLiteral("Close"), QStringLiteral("Cerrar")}},
      {QStringLiteral("export.review_clips"),
       {QStringLiteral("Review clips \u2192"),
        QStringLiteral("Revisar clips \u2192")}},
      {QStringLiteral("export.back"), {QStringLiteral("\u2190 Back"), QStringLiteral("\u2190 Volver")}},
      {QStringLiteral("export.clip_label"), {QStringLiteral("Clip"), QStringLiteral("Clip")}},
      {QStringLiteral("export.discard_clip"),
       {QStringLiteral("Discard this clip"),
        QStringLiteral("Descartar este clip")}},
      {QStringLiteral("export.all_clips_discarded"),
       {QStringLiteral("All clips have been discarded."),
        QStringLiteral("Todos los clips han sido descartados.")}},
      {QStringLiteral("export.include_note"),
       {QStringLiteral("Include note in overlay"),
        QStringLiteral("Incluir nota en overlay")}},
      {QStringLiteral("export.note_placeholder"),
       {QStringLiteral("Note text\u2026"),
        QStringLiteral("Texto sobre el clip seleccionado…")}},
      {QStringLiteral("export.starting"),
       {QStringLiteral("Starting export…"),
        QStringLiteral("Iniciando exportación…")}},
      {QStringLiteral("export.progress_prefix"),
       {QStringLiteral("Exporting clip"),
        QStringLiteral("Exportando clip")}},
      {QStringLiteral("export.done"), {QStringLiteral("Export complete."), QStringLiteral("Exportación completa.")}},
      {QStringLiteral("export.success"),
       {QStringLiteral("Clips exported successfully!"),
        QStringLiteral("¡Clips exportados exitosamente!")}},
      {QStringLiteral("export.no_output_path"),
       {QStringLiteral("Please choose an output file path."),
        QStringLiteral("Por favor elija una ruta de archivo de salida.")}},
      {QStringLiteral("export.ffmpeg_not_found"),
       {QStringLiteral("FFmpeg was not found on this system.\nPlease install FFmpeg to use clip export.\n\nhttps://ffmpeg.org"),
        QStringLiteral("FFmpeg no fue encontrado en este sistema.\nPor favor instale FFmpeg para exportar clips.\n\nhttps://ffmpeg.org")}},
      {QStringLiteral("export.mixed_events_filename"), {QStringLiteral("clips"), QStringLiteral("clips")}},
      {QStringLiteral("export.job_dismiss"), {QStringLiteral("Dismiss"), QStringLiteral("Cerrar")}},
      {QStringLiteral("export.job_path_in_use"),
       {QStringLiteral("Another export is already writing to that file."),
        QStringLiteral("Otra exportación ya está escribiendo en ese archivo.")}},
      {QStringLiteral("export.job_cancelled"),
       {QStringLiteral("Export cancelled."),
        QStringLiteral("Exportación cancelada.")}},
      {QStringLiteral("export.no_clips_selected"),
       {QStringLiteral("Select at least one clip in presentation mode to export an MP4."),
        QStringLiteral("Seleccione al menos un clip en modo presentación para exportar un MP4.")}},
      {QStringLiteral("export.no_tags_for_xml"),
       {QStringLiteral("Tag at least one instance to export an XML report."),
        QStringLiteral("Etiquete al menos una instancia para exportar un informe XML.")}},
      {QStringLiteral("concat.dialog_title"),
       {QStringLiteral("Arrange Video Files"),
        QStringLiteral("Ordenar archivos de video")}},
      {QStringLiteral("concat.move_left"), {QStringLiteral("\u2190 Move Left"), QStringLiteral("\u2190 Mover izq.")}},
      {QStringLiteral("concat.move_right"),
       {QStringLiteral("Move Right \u2192"),
        QStringLiteral("Mover der. \u2192")}},
      {QStringLiteral("concat.continue_btn"), {QStringLiteral("&Continue"), QStringLiteral("&Continuar")}},
      {QStringLiteral("concat.cancel"), {QStringLiteral("Cancel"), QStringLiteral("Cancelar")}},
      {QStringLiteral("concat.preparing"),
       {QStringLiteral("Combining video files\u2026"),
        QStringLiteral("Combinando archivos de video\u2026")}},
      {QStringLiteral("concat.error_ffmpeg"),
       {QStringLiteral("FFmpeg is required to combine multiple video files.\nPlease install FFmpeg to continue.\n\nhttps://ffmpeg.org"),
        QStringLiteral("Se necesita FFmpeg para combinar m\u00faltiples archivos de video.\nPor favor instale FFmpeg para continuar.\n\nhttps://ffmpeg.org")}},
      {QStringLiteral("concat.error_failed"),
       {QStringLiteral("Failed to combine video files."),
        QStringLiteral("Error al combinar archivos de video.")}},
      {QStringLiteral("concat.error_cancelled"),
       {QStringLiteral("Video combination cancelled."),
        QStringLiteral("Combinaci\u00f3n de video cancelada.")}},
      {QStringLiteral("concat.error_list_file"),
       {QStringLiteral("Could not write the concatenation list:\n%1\n%2"),
        QStringLiteral("No se pudo escribir la lista de concatenaci\u00f3n:\n%1\n%2")}},
      {QStringLiteral("concat.error_ffmpeg_start"),
       {QStringLiteral("Failed to start FFmpeg at \"%1\".\n%2"),
        QStringLiteral("No se pudo iniciar FFmpeg en \"%1\".\n%2")}},
      {QStringLiteral("concat.error_missing_file"),
       {QStringLiteral("Cannot combine a missing or invalid video file:\n%1"),
        QStringLiteral("No se puede combinar un archivo de video ausente o no válido:\n%1")}},
      {QStringLiteral("concat.error_unsafe_path"),
       {QStringLiteral("Cannot combine this file because its path contains invalid characters:\n%1"),
        QStringLiteral("No se puede combinar este archivo porque su ruta contiene caracteres no válidos:\n%1")}},
      {QStringLiteral("playback_prep.preparing"),
       {QStringLiteral("Preparing video for playback\u2026\n(This format is converted once for the player; export still uses your original file.)"),
        QStringLiteral("Preparando video para reproducci\u00f3n\u2026\n(Este formato se convierte una vez para el reproductor; la exportaci\u00f3n sigue usando su archivo original.)")}},
      {QStringLiteral("playback_prep.cancel"), {QStringLiteral("Cancel"), QStringLiteral("Cancelar")}},
      {QStringLiteral("playback_prep.error_ffmpeg"),
       {QStringLiteral("FFmpeg is required to play this video format in AVA.\nPlease install FFmpeg to continue.\n\nhttps://ffmpeg.org"),
        QStringLiteral("Se necesita FFmpeg para reproducir este formato de video en AVA.\nPor favor instale FFmpeg para continuar.\n\nhttps://ffmpeg.org")}},
      {QStringLiteral("playback_prep.error_failed"),
       {QStringLiteral("Could not prepare this video for playback. Try re-downloading or converting it to MP4 (H.264/AAC)."),
        QStringLiteral("No se pudo preparar este video para reproducci\u00f3n. Intente volver a descargarlo o convertirlo a MP4 (H.264/AAC).")}},
      {QStringLiteral("playback_prep.error_cancelled"),
       {QStringLiteral("Video preparation cancelled."),
        QStringLiteral("Preparaci\u00f3n de video cancelada.")}},
      {QStringLiteral("playback_prep.error_ffmpeg_start"),
       {QStringLiteral("Failed to start FFmpeg at \"%1\".\n%2"),
        QStringLiteral("No se pudo iniciar FFmpeg en \"%1\".\n%2")}},
      {QStringLiteral("xml_import.title"), {QStringLiteral("Import XML"), QStringLiteral("Importar XML")}},
      {QStringLiteral("xml_import.select_file"),
       {QStringLiteral("Select XML file to import"),
        QStringLiteral("Seleccionar archivo XML para importar")}},
      {QStringLiteral("xml_import.cancel"), {QStringLiteral("Cancel"), QStringLiteral("Cancelar")}},
      {QStringLiteral("xml_import.continue"), {QStringLiteral("Continue"), QStringLiteral("Continuar")}},
      {QStringLiteral("xml_import.import"), {QStringLiteral("Import"), QStringLiteral("Importar")}},
      {QStringLiteral("xml_import.conflict_title"),
       {QStringLiteral("Existing tags found"),
        QStringLiteral("Marcas existentes")}},
      {QStringLiteral("xml_import.conflict_message"),
       {QStringLiteral("This session already has tagged events. Replace them with the imported XML, merge both sets, or cancel."),
        QStringLiteral("Esta sesión ya tiene eventos etiquetados. ¿Reemplazarlos con el XML importado, combinar ambos conjuntos o cancelar?")}},
      {QStringLiteral("xml_import.conflict_replace"),
       {QStringLiteral("Replace all tags"),
        QStringLiteral("Reemplazar todas las marcas")}},
      {QStringLiteral("xml_import.conflict_merge"),
       {QStringLiteral("Merge with existing tags"),
        QStringLiteral("Combinar con marcas existentes")}},
      {QStringLiteral("xml_import.sync_title"),
       {QStringLiteral("Align XML timeline"),
        QStringLiteral("Alinear línea de tiempo XML")}},
      {QStringLiteral("xml_import.sync_instructions"),
       {QStringLiteral("Scrub the video timeline to the moment that matches the XML start anchor, then continue. The playhead position is used as the sync point."),
        QStringLiteral("Desplace la línea de tiempo del video hasta el momento que coincide con el ancla de inicio del XML y continúe. La posición del cabezal de reproducción se usa como punto de sincronización.")}},
      {QStringLiteral("xml_import.sync_fallback_warning"),
       {QStringLiteral("No Inicio tag was found in the XML. Using the first quarter or earliest event as the sync anchor."),
        QStringLiteral("No se encontró una marca Inicio en el XML. Se usa el primer cuarto o el evento más temprano como ancla.")}},
      {QStringLiteral("xml_import.sync_xml_anchor"),
       {QStringLiteral("XML anchor (%2): %1"),
        QStringLiteral("Ancla XML (%2): %1")}},
      {QStringLiteral("xml_import.sync_video_anchor"),
       {QStringLiteral("Video anchor: %1"),
        QStringLiteral("Ancla de video: %1")}},
      {QStringLiteral("xml_import.sync_video_unavailable"),
       {QStringLiteral("unavailable"),
        QStringLiteral("no disponible")}},
      {QStringLiteral("xml_import.sync_use_playhead"),
       {QStringLiteral("Use current playhead"),
        QStringLiteral("Usar cabezal actual")}},
      {QStringLiteral("xml_import.sync_offset"),
       {QStringLiteral("Offset: %1 s"),
        QStringLiteral("Desplazamiento: %1 s")}},
      {QStringLiteral("xml_import.sync_preview_ok"),
       {QStringLiteral("%1 instances will be imported."),
        QStringLiteral("Se importarán %1 instancias.")}},
      {QStringLiteral("xml_import.sync_preview_clamp"),
       {QStringLiteral("%1 instances will be imported. %2 will start before video zero; %3 will extend past the video end (timestamps will be clamped)."),
        QStringLiteral("Se importarán %1 instancias. %2 comenzarán antes del inicio del video; %3 se extenderán más allá del final (las marcas de tiempo se ajustarán).")}},
      {QStringLiteral("xml_import.mapping_title"),
       {QStringLiteral("Map XML event codes"),
        QStringLiteral("Mapear códigos de evento XML")}},
      {QStringLiteral("xml_import.mapping_instructions"),
       {QStringLiteral("Review how each XML code maps to AVA events. Adjust any row before importing."),
        QStringLiteral("Revise cómo cada código XML se mapea a eventos de AVA. Ajuste cualquier fila antes de importar.")}},
      {QStringLiteral("xml_import.mapping_abbrev_header"),
       {QStringLiteral("Map XML team abbreviations to session teams:"),
        QStringLiteral("Asigne abreviaturas XML a equipos de la sesión:")}},
      {QStringLiteral("xml_import.mapping_home_abbrev"),
       {QStringLiteral("Home abbrev in XML:"),
        QStringLiteral("Abreviatura local en XML:")}},
      {QStringLiteral("xml_import.mapping_away_abbrev"),
       {QStringLiteral("Away abbrev in XML:"),
        QStringLiteral("Abreviatura visitante en XML:")}},
      {QStringLiteral("xml_import.mapping_abbrev_for_team"),
       {QStringLiteral("%1 abbrev in XML:"),
        QStringLiteral("Abreviatura %1 en XML:")}},
      {QStringLiteral("xml_import.mapping_col_code"), {QStringLiteral("XML code"), QStringLiteral("Código XML")}},
      {QStringLiteral("xml_import.mapping_col_count"), {QStringLiteral("Count"), QStringLiteral("Cantidad")}},
      {QStringLiteral("xml_import.mapping_col_event"),
       {QStringLiteral("Map to event"),
        QStringLiteral("Mapear a evento")}},
      {QStringLiteral("xml_import.mapping_col_team"), {QStringLiteral("Team"), QStringLiteral("Equipo")}},
      {QStringLiteral("xml_import.mapping_col_import"), {QStringLiteral("Import"), QStringLiteral("Importar")}},
      {QStringLiteral("xml_import.mapping_col_action"), {QStringLiteral("Action"), QStringLiteral("Acción")}},
      {QStringLiteral("xml_import.mapping_team_none"), {QStringLiteral("(none)"), QStringLiteral("(ninguno)")}},
      {QStringLiteral("xml_import.mapping_action_import"), {QStringLiteral("Import"), QStringLiteral("Importar")}},
      {QStringLiteral("xml_import.mapping_action_skip"), {QStringLiteral("Skip"), QStringLiteral("Omitir")}},
      {QStringLiteral("xml_import.mapping_none_selected"),
       {QStringLiteral("No instances are set to import."),
        QStringLiteral("No hay instancias configuradas para importar.")}},
      {QStringLiteral("xml_import.mapping_missing_event"),
       {QStringLiteral("Choose an event for code: %1"),
        QStringLiteral("Elija un evento para el código: %1")}},
      {QStringLiteral("xml_import.mapping_missing_team"),
       {QStringLiteral("Choose a team for code: %1"),
        QStringLiteral("Elija un equipo para el código: %1")}},
      {QStringLiteral("xml_import.complete_summary"),
       {QStringLiteral("Imported %1 events (%2 skipped, %3 clamped to video bounds)."),
        QStringLiteral("Se importaron %1 eventos (%2 omitidos, %3 ajustados a los límites del video).")}},
      {QStringLiteral("xml_import.no_sync_anchor"),
       {QStringLiteral("No usable instance was found to align the XML with the video."),
        QStringLiteral("No se encontró una instancia usable para alinear el XML con el video.")}},
      {QStringLiteral("license.lock.title_trial"),
       {QStringLiteral("Your trial has ended"),
        QStringLiteral("Se acabó tu prueba")}},
      {QStringLiteral("license.lock.body_trial"),
       {QStringLiteral("Tagging, export, and Presentation are locked. Paste a license key from Camila to keep using AVA."),
        QStringLiteral("El etiquetado, la exportación y Presentación están bloqueados. Pega la clave de licencia que te envió Camila para seguir usando AVA.")}},
      {QStringLiteral("license.lock.title_grace"),
       {QStringLiteral("Connect once to refresh"),
        QStringLiteral("Conéctate una vez para actualizar")}},
      {QStringLiteral("license.lock.body_grace"),
       {QStringLiteral("AVA could not refresh your license. Connect to the internet once, then you can work offline again."),
        QStringLiteral("AVA no pudo actualizar tu licencia. Conéctate a internet una vez y después puedes volver a trabajar sin conexión.")}},
      {QStringLiteral("license.lock.title_expired"),
       {QStringLiteral("This license has expired"),
        QStringLiteral("Esta licencia venció")}},
      {QStringLiteral("license.lock.body_expired"),
       {QStringLiteral("Tagging, export, and Presentation are locked. Ask Camila for a new license key."),
        QStringLiteral("El etiquetado, la exportación y Presentación están bloqueados. Pídele a Camila una clave nueva.")}},
      {QStringLiteral("license.lock.title_other_device"),
       {QStringLiteral("This license is on another Mac"),
        QStringLiteral("Esta licencia está en otro Mac")}},
      {QStringLiteral("license.lock.body_other_device"),
       {QStringLiteral("That key is already in use on a different computer. Write to Camila if you changed Macs."),
        QStringLiteral("Esa clave ya se usa en otro computador. Escríbele a Camila si cambiaste de Mac.")}},
      {QStringLiteral("license.lock.title_enter"),
       {QStringLiteral("Enter your license"),
        QStringLiteral("Ingresa tu licencia")}},
      {QStringLiteral("license.lock.body_enter"),
       {QStringLiteral("Paste the license key Camila sent you. You can keep working offline after it activates."),
        QStringLiteral("Pega la clave de licencia que te envió Camila. Después de activarla puedes seguir trabajando sin conexión.")}},
      {QStringLiteral("license.lock.email"), {QStringLiteral("Email:"), QStringLiteral("Email:")}},
      {QStringLiteral("license.lock.email_placeholder"),
       {QStringLiteral("you@example.com"),
        QStringLiteral("tucorreo@ejemplo.com")}},
      {QStringLiteral("license.lock.key"), {QStringLiteral("License key:"), QStringLiteral("Clave de licencia:")}},
      {QStringLiteral("license.lock.key_placeholder"), {QStringLiteral("AVA1.…"), QStringLiteral("AVA1.…")}},
      {QStringLiteral("license.lock.activate"), {QStringLiteral("Activate"), QStringLiteral("Activar")}},
      {QStringLiteral("license.lock.load_file"),
       {QStringLiteral("Load license file…"),
        QStringLiteral("Cargar archivo de licencia…")}},
      {QStringLiteral("license.lock.close"), {QStringLiteral("Close"), QStringLiteral("Cerrar")}},
      {QStringLiteral("license.lock.contact"),
       {QStringLiteral("Write to Camila at camilaescudero.cl"),
        QStringLiteral("Escríbele a Camila en camilaescudero.cl")}},
      {QStringLiteral("license.lock.busy"),
       {QStringLiteral("Checking license…"),
        QStringLiteral("Comprobando licencia…")}},
      {QStringLiteral("license.error.email_and_key"),
       {QStringLiteral("Enter your email and license key."),
        QStringLiteral("Ingresa tu email y la clave de licencia.")}},
      {QStringLiteral("license.error.email_mismatch"),
       {QStringLiteral("This license belongs to another email."),
        QStringLiteral("Esta licencia pertenece a otro email.")}},
      {QStringLiteral("license.error.invalid_key"),
       {QStringLiteral("That key is not valid. Check it and try again."),
        QStringLiteral("Esa clave no es válida. Revisa y vuelve a intentar.")}},
      {QStringLiteral("license.error.other_device"),
       {QStringLiteral("This license is already used on another Mac."),
        QStringLiteral("Esta licencia ya se usa en otro Mac.")}},
      {QStringLiteral("license.error.expired_key"),
       {QStringLiteral("This license has expired."),
        QStringLiteral("Esta licencia venció.")}},
      {QStringLiteral("license.error.file"),
       {QStringLiteral("Could not read that license file."),
        QStringLiteral("No se pudo leer ese archivo de licencia.")}},
      {QStringLiteral("license.ok.activated"),
       {QStringLiteral("License activated."),
        QStringLiteral("Licencia activada.")}},
      {QStringLiteral("license.status.trial"),
       {QStringLiteral("Trial · %1 days left"),
        QStringLiteral("Prueba · %1 días restantes")}},
      {QStringLiteral("license.status.paid"),
       {QStringLiteral("Licensed until %1"),
        QStringLiteral("Licencia hasta %1")}},
      {QStringLiteral("license.enter_key"),
       {QStringLiteral("Enter license key"),
        QStringLiteral("Ingresar clave de licencia")}},
      {QStringLiteral("license.file_filter"),
       {QStringLiteral("AVA license (*.ava-license);;All files (*.*)"),
        QStringLiteral("Licencia AVA (*.ava-license);;Todos los archivos (*.*)")}},
  };
  return table;
}

const QHash<QString, QString>& uiStringsForLanguage(AppLocale::Language language) {
  static const QHash<QString, QString> englishStrings = [] {
    const QHash<QString, UiTranslation>& table = uiTranslationTable();
    QHash<QString, QString> strings;
    strings.reserve(table.size());
    for (auto iterator = table.constBegin(); iterator != table.constEnd(); ++iterator) {
      strings.insert(iterator.key(), iterator->english);
    }
    return strings;
  }();
  static const QHash<QString, QString> spanishStrings = [] {
    const QHash<QString, UiTranslation>& table = uiTranslationTable();
    QHash<QString, QString> strings;
    strings.reserve(table.size());
    for (auto iterator = table.constBegin(); iterator != table.constEnd(); ++iterator) {
      strings.insert(iterator.key(), iterator->spanish);
    }
    return strings;
  }();
  return language == AppLocale::Language::Spanish ? spanishStrings : englishStrings;
}

QStringList splitCompoundPath(const QString& path) {
  return path.split(AppLocale::kCompoundPathSeparator, Qt::KeepEmptyParts);
}

QString joinCompoundPath(const QStringList& segments) {
  return segments.join(QString(AppLocale::kCompoundPathSeparator));
}

bool equalsIgnoreCase(const QString& lhs, const QString& rhs) {
  return lhs.compare(rhs, Qt::CaseInsensitive) == 0;
}

/// Matches team labels embedded in follow-up paths (GameControls defaults, side keys, or names).
class TeamFollowUpSegmentMatcher {
public:
  TeamFollowUpSegmentMatcher(const QString& homeTeamName, const QString& awayTeamName)
      : homeDisplay_(homeTeamName.trimmed().isEmpty() ? QString(kDefaultHomeFollowUpLabel)
                                                       : homeTeamName.trimmed()),
        awayDisplay_(awayTeamName.trimmed().isEmpty() ? QString(kDefaultAwayFollowUpLabel)
                                                       : awayTeamName.trimmed()) {}

  bool isTeamSegment(const QString& segment) const {
    const QString trimmed = segment.trimmed();
    if (trimmed.isEmpty()) return false;
    if (equalsIgnoreCase(trimmed, homeDisplay_)) return true;
    if (equalsIgnoreCase(trimmed, awayDisplay_)) return true;
    if (equalsIgnoreCase(trimmed, QString(kHomeTeamSideKey))) return true;
    if (equalsIgnoreCase(trimmed, QString(kAwayTeamSideKey))) return true;
    if (equalsIgnoreCase(trimmed, QString(kDefaultHomeFollowUpLabel))) return true;
    if (equalsIgnoreCase(trimmed, QString(kDefaultAwayFollowUpLabel))) return true;
    return false;
  }

private:
  QString homeDisplay_;
  QString awayDisplay_;
};

} // namespace

namespace AppLocale {

Language currentLanguage() {
  return LanguageStore::instance().current();
}

void setLanguage(Language language) {
  notifyLanguageChangedIf(LanguageStore::instance().setLanguageAndPersist(language));
}

void loadFromSettings() {
  // Same notification path as setLanguage() so listeners refresh after startup load.
  notifyLanguageChangedIf(LanguageStore::instance().loadFromSettings());
}

void saveToSettings() {
  LanguageStore::instance().saveToSettings();
}

QString trEvent(const QString& canonicalToken) {
  return translateEventForLanguage(canonicalToken, currentLanguage());
}

QString trEventForLanguage(const QString& canonicalToken, Language language) {
  return translateEventForLanguage(canonicalToken, language);
}

QString translateCompoundPath(const QString& canonicalPath) {
  if (canonicalPath.isEmpty()) return canonicalPath;
  const QStringList parts = splitCompoundPath(canonicalPath);
  QStringList translated;
  translated.reserve(parts.size());
  for (const QString& part : parts) {
    translated.append(trEvent(part.trimmed()));
  }
  return joinCompoundPath(translated);
}

QString followUpPathWithoutTeamSegments(const QString& followUpEvent, const QString& homeTeamName,
                                        const QString& awayTeamName) {
  if (followUpEvent.isEmpty()) return followUpEvent;
  const QStringList parts = splitCompoundPath(followUpEvent);
  const TeamFollowUpSegmentMatcher matcher(homeTeamName, awayTeamName);
  QStringList filtered;
  filtered.reserve(parts.size());
  for (const QString& segment : parts) {
    if (matcher.isTeamSegment(segment)) continue;
    filtered.append(segment.trimmed());
  }
  return joinCompoundPath(filtered);
}

QString trDisplayTagLine(const QString& mainEvent, const QString& followUpEvent) {
  QString line = trEvent(mainEvent);
  if (!followUpEvent.isEmpty()) {
    line += QString(kCompoundPathSeparator) + translateCompoundPath(followUpEvent);
  }
  return line;
}

QString trUi(const char* key) {
  if (!key) return QString();
  const QLatin1String latinKey(key);
  const QHash<QString, QString>& strings = uiStringsForLanguage(currentLanguage());
  const auto iterator = strings.constFind(latinKey);
  if (iterator == strings.cend()) return QString::fromLatin1(key);
  return iterator.value();
}

} // namespace AppLocale

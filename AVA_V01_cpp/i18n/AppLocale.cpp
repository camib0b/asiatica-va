#include "AppLocale.h"
#include "EventCatalog.h"
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

// Tokens that are not catalog events. home/away are empty-name team labels.
// For/Against/Off/Def are retired follow-up words still stored on old tags.
constexpr std::array<SpanishEventEntry, 6> kResidualSpanishEntries{{
    {"For", "A favor"},
    {"Against", "En contra"},
    {"Off", "Ofensiva"},
    {"Def", "Defensiva"},
    {"home", "Local"},
    {"away", "Visita"},
}};

const QHash<QString, QString>& residualSpanishMap() {
  static const QHash<QString, QString> map = [] {
    QHash<QString, QString> built;
    built.reserve(static_cast<int>(kResidualSpanishEntries.size()));
    for (const SpanishEventEntry& entry : kResidualSpanishEntries) {
      const QString key = QString::fromUtf8(entry.canonical);
      Q_ASSERT(!built.contains(key));
      Q_ASSERT(!EventCatalog::containsCanonicalName(key));
      built.insert(key, QString::fromUtf8(entry.spanish));
    }
    Q_ASSERT(built.size() == static_cast<int>(kResidualSpanishEntries.size()));
    return built;
  }();
  return map;
}

QString translateEventForLanguage(const QString& canonicalToken, AppLocale::Language language) {
  const QString key = canonicalToken.trimmed();
  if (key.isEmpty()) return key;

  const QString displayLabel = EventCatalog::displayLabelForToken(key);
  if (!displayLabel.isEmpty()) return displayLabel;

  if (EventCatalog::containsCanonicalName(key)) {
    if (language != AppLocale::Language::Spanish) return key;
    const QString spanish = EventCatalog::spanishForCanonicalName(key);
    return spanish.isEmpty() ? key : spanish;
  }

  if (language != AppLocale::Language::Spanish) return key;
  const auto iterator = residualSpanishMap().constFind(key);
  if (iterator == residualSpanishMap().cend()) return key;
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
      {QStringLiteral("session.unreadable"),
       {QStringLiteral("Couldn't read the session file. Changes to this match won't be saved."),
        QStringLiteral("No se pudo leer el archivo de la sesión. Los cambios de este partido no se guardarán.")}},
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
      {QStringLiteral("setup.color_hex_hint"),
       {QStringLiteral("Use 6 hex digits, with or without #."),
        QStringLiteral("Usa 6 dígitos hex, con o sin #.")}},
      {QStringLiteral("setup.placeholder_abbrev"), {QStringLiteral("3 letters"), QStringLiteral("3 letras")}},
      {QStringLiteral("setup.same_abbrev"),
       {QStringLiteral("Home and away can't use the same abbreviation."),
        QStringLiteral("El local y el visitante no pueden usar la misma abreviatura.")}},
      {QStringLiteral("setup.pick"), {QStringLiteral("Pick"), QStringLiteral("Elegir")}},
      {QStringLiteral("setup.back"), {QStringLiteral("&Back"), QStringLiteral("&Atrás")}},
      {QStringLiteral("setup.continue"), {QStringLiteral("&Continue"), QStringLiteral("&Continuar")}},
      {QStringLiteral("setup.continue_disabled_hint"),
       {QStringLiteral("Select a video file to continue."),
        QStringLiteral("Selecciona un archivo de video para continuar.")}},
      {QStringLiteral("setup.lang_label"), {QStringLiteral("Language:"), QStringLiteral("Idioma:")}},
      {QStringLiteral("setup.lang_en"), {QStringLiteral("English"), QStringLiteral("English")}},
      {QStringLiteral("setup.lang_es"), {QStringLiteral("Español"), QStringLiteral("Español")}},
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
       {QStringLiteral("Start game"),
        QStringLiteral("Iniciar partido")}},
      {QStringLiteral("gamecontrols.start_q2"), {QStringLiteral("Start Q2"), QStringLiteral("Iniciar Q2")}},
      {QStringLiteral("gamecontrols.start_q3"), {QStringLiteral("Start Q3"), QStringLiteral("Iniciar Q3")}},
      {QStringLiteral("gamecontrols.start_q4"), {QStringLiteral("Start Q4"), QStringLiteral("Iniciar Q4")}},
      {QStringLiteral("gamecontrols.end_game"),
       {QStringLiteral("End game"),
        QStringLiteral("Fin del partido")}},
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
      {QStringLiteral("presentation.apply_to_one"),
       {QStringLiteral("Apply to the selected clip"),
        QStringLiteral("Aplicar al clip seleccionado")}},
      {QStringLiteral("presentation.apply_to_one_tooltip"),
       {QStringLiteral("Gives the selected clip these lead and lag times around its event mark."),
        QStringLiteral("Da al clip seleccionado esta anticipación y retardo alrededor de su marca.")}},
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
      {QStringLiteral("menu.replace_video"),
       {QStringLiteral("Replace video with another one"),
        QStringLiteral("Sustituir video por otro")}},
      {QStringLiteral("replace_video.title"),
       {QStringLiteral("Replace video"), QStringLiteral("Sustituir video")}},
      {QStringLiteral("replace_video.warning"),
       {QStringLiteral("Replacing the video clears every tag and note."),
        QStringLiteral("Sustituir el video borra todas las marcas y las notas.")}},
      {QStringLiteral("replace_video.export"),
       {QStringLiteral("Export XML and notes"), QStringLiteral("Exportar XML y notas")}},
      {QStringLiteral("replace_video.replace"),
       {QStringLiteral("Replace without exporting"), QStringLiteral("Sustituir sin exportar")}},
      {QStringLiteral("replace_video.cancel"),
       {QStringLiteral("Cancel"), QStringLiteral("Cancelar")}},
      {QStringLiteral("replace_video.export_dialog_title"),
       {QStringLiteral("Export XML and notes"), QStringLiteral("Exportar XML y notas")}},
      {QStringLiteral("replace_video.notes_failed"),
       {QStringLiteral("Could not write the notes file."),
        QStringLiteral("No se pudo escribir el archivo de notas.")}},
      {QStringLiteral("replace_video.notes_no_path"),
       {QStringLiteral("Choose a file for the notes."),
        QStringLiteral("Elige un archivo para las notas.")}},
      {QStringLiteral("replace_video.notes_open_failed"),
       {QStringLiteral("Could not open the notes file: %1"),
        QStringLiteral("No se pudo abrir el archivo de notas: %1")}},
      {QStringLiteral("replace_video.notes_write_failed"),
       {QStringLiteral("Could not write the notes file: %1"),
        QStringLiteral("No se pudo escribir el archivo de notas: %1")}},
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
      {QStringLiteral("notes.export_match_heading"),
       {QStringLiteral("Match notes"), QStringLiteral("Notas del partido")}},
      {QStringLiteral("notes.export_clip_heading"),
       {QStringLiteral("Clip notes"), QStringLiteral("Notas de clips")}},
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
      {QStringLiteral("vc.tt.speed"),
       {QStringLiteral("Drag to set speed  ·  Double-click the speed to reset 1.0x  ·  -  slower  ·  +  faster  ·  }  reset 1.0x"),
        QStringLiteral("Arrastra para cambiar la velocidad  ·  Doble clic en la velocidad para restablecer 1.0x  ·  -  más lento  ·  +  más rápido  ·  }  restablecer 1.0x")}},
      {QStringLiteral("vc.tt.mute"), {QStringLiteral("shift+m  Mute"), QStringLiteral("shift+m  Silenciar")}},
      {QStringLiteral("vc.tt.unmute"), {QStringLiteral("shift+m  Unmute"), QStringLiteral("shift+m  Activar audio")}},
      {QStringLiteral("menu.export_clips"), {QStringLiteral("Export clips…"), QStringLiteral("Exportar clips…")}},
      {QStringLiteral("menu.import_xml"), {QStringLiteral("Import XML…"), QStringLiteral("Importar XML…")}},
      {QStringLiteral("menu.export_xml"),
       {QStringLiteral("Export XML report…"), QStringLiteral("Exportar reporte XML…")}},
      {QStringLiteral("menu.clip_durations"),
       {QStringLiteral("Clip durations…"),
        QStringLiteral("Duraciones de clip…")}},
      {QStringLiteral("menu.reduce_motion"),
       {QStringLiteral("Reduce animations"),
        QStringLiteral("Reducir animaciones")}},
      {QStringLiteral("menu.reduce_motion_tooltip"),
       {QStringLiteral("Keeps buttons, the welcome screen, and export progress still."),
        QStringLiteral("Deja quietos los botones, la pantalla de inicio y el progreso de exportación.")}},
      {QStringLiteral("clip_durations.title"),
       {QStringLiteral("Clip Durations"),
        QStringLiteral("Duraciones de clip")}},
      {QStringLiteral("clip_durations.subtitle"),
       {QStringLiteral("Default lead and lag times around each tag when creating clips. Changes apply immediately to untrimmed tags."),
        QStringLiteral("Tiempos de anticipación y retardo predeterminados alrededor de cada marca al crear clips. Los cambios se aplican de inmediato a marcas sin recorte.")}},
      {QStringLiteral("clip_durations.col_event"), {QStringLiteral("Event"), QStringLiteral("Evento")}},
      {QStringLiteral("clip_durations.col_lead"), {QStringLiteral("Lead"), QStringLiteral("Anticipación")}},
      {QStringLiteral("clip_durations.col_lag"), {QStringLiteral("Lag"), QStringLiteral("Retardo")}},
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
      {QStringLiteral("export.xml_title"),
       {QStringLiteral("Export XML report"),
        QStringLiteral("Exportar reporte XML")}},
      {QStringLiteral("export.xml_save_dialog_title"),
       {QStringLiteral("Save XML report"),
        QStringLiteral("Guardar reporte XML")}},
      {QStringLiteral("export.xml_report_filename_segment"),
       {QStringLiteral("XML report"),
        QStringLiteral("reporte XML")}},
      {QStringLiteral("export.xml_success"),
       {QStringLiteral("XML report exported successfully."),
        QStringLiteral("Reporte XML exportado correctamente.")}},
      {QStringLiteral("export.xml_failed"),
       {QStringLiteral("Failed to write the XML report."),
        QStringLiteral("No se pudo escribir el reporte XML.")}},
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
      {QStringLiteral("export.include_drawings"),
       {QStringLiteral("Include drawings"),
        QStringLiteral("Incluir dibujos")}},
      {QStringLiteral("telestration.tool.arrow"),
       {QStringLiteral("Arrow"), QStringLiteral("Flecha")}},
      {QStringLiteral("telestration.tool.dotted_arrow"),
       {QStringLiteral("Dotted arrow"), QStringLiteral("Flecha punteada")}},
      {QStringLiteral("telestration.tool.circle"),
       {QStringLiteral("Circle"), QStringLiteral("Círculo")}},
      {QStringLiteral("telestration.tool.freehand"),
       {QStringLiteral("Pen"), QStringLiteral("Lápiz")}},
      {QStringLiteral("telestration.undo"),
       {QStringLiteral("Undo"), QStringLiteral("Deshacer")}},
      {QStringLiteral("telestration.color.white"),
       {QStringLiteral("White"), QStringLiteral("Blanco")}},
      {QStringLiteral("telestration.color.black"),
       {QStringLiteral("Black"), QStringLiteral("Negro")}},
      {QStringLiteral("telestration.color.blue"),
       {QStringLiteral("Blue"), QStringLiteral("Azul")}},
      {QStringLiteral("telestration.color.red"),
       {QStringLiteral("Red"), QStringLiteral("Rojo")}},
      {QStringLiteral("export.note_placeholder"),
       {QStringLiteral("Note text\u2026"),
        QStringLiteral("Texto sobre el clip seleccionado…")}},
      {QStringLiteral("export.starting"),
       {QStringLiteral("Starting export…"),
        QStringLiteral("Iniciando exportación…")}},
      {QStringLiteral("export.preparing"),
       {QStringLiteral("Preparing the export\u2026 checking the video before cutting the clips."),
        QStringLiteral("Preparando la exportación\u2026 revisando el video antes de cortar los clips.")}},
      {QStringLiteral("export.clip_status"),
       {QStringLiteral("Exporting clip %1 of %2\u2026 cutting each clip from the match takes a while."),
        QStringLiteral("Exportando el clip %1 de %2\u2026 cada clip se corta del partido, así que tarda.")}},
      {QStringLiteral("export.joining"),
       {QStringLiteral("Joining the clips into one video\u2026 this can take a moment."),
        QStringLiteral("Uniendo los clips en un solo video\u2026 puede tardar un momento.")}},
      {QStringLiteral("export.finishing"),
       {QStringLiteral("Finishing the video\u2026 saving the clip list can take a moment."),
        QStringLiteral("Terminando el video\u2026 guardar la lista de clips puede tardar un momento.")}},
      {QStringLiteral("export.done"), {QStringLiteral("Export complete."), QStringLiteral("Exportación completa.")}},
      {QStringLiteral("export.success"),
       {QStringLiteral("Clips exported successfully!"),
        QStringLiteral("¡Clips exportados exitosamente!")}},
      {QStringLiteral("export.no_output_path"),
       {QStringLiteral("Please choose an output file path."),
        QStringLiteral("Por favor elija una ruta de archivo de salida.")}},
      {QStringLiteral("export.ffmpeg_not_found"),
       {QStringLiteral("Clip export needs FFmpeg, which should be inside AVA. Re-download the app from camilaescudero.cl/ava."),
        QStringLiteral("La exportación de clips necesita FFmpeg, que debería estar dentro de AVA. Vuelva a descargar la aplicación desde camilaescudero.cl/ava.")}},
      {QStringLiteral("export.ffprobe_not_found"),
       {QStringLiteral("Clip export needs ffprobe, which should be inside AVA. Re-download the app from camilaescudero.cl/ava."),
        QStringLiteral("La exportación de clips necesita ffprobe, que debería estar dentro de AVA. Vuelva a descargar la aplicación desde camilaescudero.cl/ava.")}},
      {QStringLiteral("export.sidecar_probe_failed"),
       {QStringLiteral("Could not measure the duration of exported clip %1."),
        QStringLiteral("No se pudo medir la duración del clip exportado %1.")}},
      {QStringLiteral("export.sidecar_write_failed"),
       {QStringLiteral("The video was exported, but the clip timestamp file could not be written:\n%1"),
        QStringLiteral("El video se exportó, pero no se pudo escribir el archivo de tiempos de los clips:\n%1")}},
      {QStringLiteral("export.mixed_events_filename"), {QStringLiteral("clips"), QStringLiteral("clips")}},
      {QStringLiteral("export.job_dismiss"), {QStringLiteral("Dismiss"), QStringLiteral("Cerrar")}},
      {QStringLiteral("export.open_folder"),
       {QStringLiteral("Open folder"),
        QStringLiteral("Abrir carpeta")}},
      {QStringLiteral("export.job_path_in_use"),
       {QStringLiteral("Another export is already writing to that file."),
        QStringLiteral("Otra exportación ya está escribiendo en ese archivo.")}},
      {QStringLiteral("export.output_exists_title"),
       {QStringLiteral("File already exists"),
        QStringLiteral("Ese video ya existe")}},
      {QStringLiteral("export.output_exists"),
       {QStringLiteral("A video with this name is already saved. Exporting now would replace the earlier one."),
        QStringLiteral("Ya hay un video guardado con este nombre. Si exportas ahora, se reemplaza el anterior.")}},
      {QStringLiteral("export.output_rename"),
       {QStringLiteral("Rename"),
        QStringLiteral("Cambiar nombre")}},
      {QStringLiteral("export.output_overwrite"),
       {QStringLiteral("Replace it"),
        QStringLiteral("Reemplazar")}},
      {QStringLiteral("export.output_cancel"),
       {QStringLiteral("Cancel"),
        QStringLiteral("Cancelar")}},
      {QStringLiteral("export.output_same_as_source"),
       {QStringLiteral("Choose a different file. This one is the match video, and exporting over it would replace the original."),
        QStringLiteral("Elige otro archivo. Este es el video del partido, y exportar encima lo reemplazaría.")}},
      {QStringLiteral("export.output_rename_failed"),
       {QStringLiteral("Could not find a free name next to that file. Choose another folder."),
        QStringLiteral("No encontré un nombre libre al lado de ese archivo. Elige otra carpeta.")}},
      {QStringLiteral("export.busy_title"),
       {QStringLiteral("Export still running"),
        QStringLiteral("La exportación sigue en curso")}},
      {QStringLiteral("export.busy_message"),
       {QStringLiteral("An export is still reading this video. You can wait until it finishes, or cancel it. Cancelling removes any half-written file."),
        QStringLiteral("Todavía se está exportando y este video se está leyendo. Puedes esperar a que termine, o cancelar la exportación. Si cancelas, se borra el archivo a medias.")}},
      {QStringLiteral("export.busy_wait"),
       {QStringLiteral("Wait"),
        QStringLiteral("Esperar")}},
      {QStringLiteral("export.busy_cancel"),
       {QStringLiteral("Cancel export"),
        QStringLiteral("Cancelar exportación")}},
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
      {QStringLiteral("concat.headline_zero"),
       {QStringLiteral("No clips to join"),
        QStringLiteral("No hay clips para unir")}},
      {QStringLiteral("concat.headline_one"),
       {QStringLiteral("Join 1 clip into one video"),
        QStringLiteral("Unir 1 clip en un solo video")}},
      {QStringLiteral("concat.headline_many"),
       {QStringLiteral("Join %1 clips into one video"),
        QStringLiteral("Unir %1 clips en un solo video")}},
      {QStringLiteral("concat.subtitle"),
       {QStringLiteral("Drag each clip or use the arrows to order them. The top one plays first."),
        QStringLiteral("Arrastra cada clip o usa las flechas para ordenarlos. El de arriba se reproduce primero.")}},
      {QStringLiteral("concat.flow_accessible"),
       {QStringLiteral("Into the final video"),
        QStringLiteral("Hacia el video final")}},
      {QStringLiteral("concat.output_title"),
       {QStringLiteral("One video"),
        QStringLiteral("Un solo video")}},
      {QStringLiteral("concat.output_zero"),
       {QStringLiteral("Nothing to compose"),
        QStringLiteral("Nada para componer")}},
      {QStringLiteral("concat.output_one"),
       {QStringLiteral("Composed of 1 clip"),
        QStringLiteral("Compuesto por 1 clip")}},
      {QStringLiteral("concat.output_many"),
       {QStringLiteral("Composed of %1 clips"),
        QStringLiteral("Compuesto por %1 clips")}},
      {QStringLiteral("concat.output_one_duration"),
       {QStringLiteral("Composed of 1 clip · %1"),
        QStringLiteral("Compuesto por 1 clip · %1")}},
      {QStringLiteral("concat.output_many_duration"),
       {QStringLiteral("Composed of %1 clips · %2"),
        QStringLiteral("Compuesto por %1 clips · %2")}},
      {QStringLiteral("concat.duration_hms"),
       {QStringLiteral("%1 h %2 min %3 s"),
        QStringLiteral("%1 h %2 min %3 s")}},
      {QStringLiteral("concat.duration_hm"),
       {QStringLiteral("%1 h %2 min"),
        QStringLiteral("%1 h %2 min")}},
      {QStringLiteral("concat.duration_hs"),
       {QStringLiteral("%1 h %2 s"),
        QStringLiteral("%1 h %2 s")}},
      {QStringLiteral("concat.duration_h"),
       {QStringLiteral("%1 h"),
        QStringLiteral("%1 h")}},
      {QStringLiteral("concat.duration_ms"),
       {QStringLiteral("%1 min %2 s"),
        QStringLiteral("%1 min %2 s")}},
      {QStringLiteral("concat.duration_m"),
       {QStringLiteral("%1 min"),
        QStringLiteral("%1 min")}},
      {QStringLiteral("concat.duration_s"),
       {QStringLiteral("%1 s"),
        QStringLiteral("%1 s")}},
      {QStringLiteral("concat.reset_order"),
       {QStringLiteral("Restore chronological order"),
        QStringLiteral("Restablecer orden cronológico")}},
      {QStringLiteral("concat.reset_hint"),
       {QStringLiteral("Back to each clip's start time"),
        QStringLiteral("Volver al orden según el inicio de cada clip")}},
      {QStringLiteral("concat.reset_done"),
       {QStringLiteral("Clips are already in chronological order"),
        QStringLiteral("Los clips ya están en orden cronológico")}},
      {QStringLiteral("concat.move_up"),
       {QStringLiteral("Move up"),
        QStringLiteral("Subir")}},
      {QStringLiteral("concat.move_down"),
       {QStringLiteral("Move down"),
        QStringLiteral("Bajar")}},
      {QStringLiteral("concat.move_up_shortcut"),
       {QStringLiteral("Move up (%1)"),
        QStringLiteral("Subir (%1)")}},
      {QStringLiteral("concat.move_down_shortcut"),
       {QStringLiteral("Move down (%1)"),
        QStringLiteral("Bajar (%1)")}},
      {QStringLiteral("concat.row_accessible"),
       {QStringLiteral("Position %1. %2. %3"),
        QStringLiteral("Posición %1. %2. %3")}},
      {QStringLiteral("concat.row_accessible_no_time"),
       {QStringLiteral("Position %1. %2"),
        QStringLiteral("Posición %1. %2")}},
      {QStringLiteral("concat.continue_btn"), {QStringLiteral("&Continue"), QStringLiteral("&Continuar")}},
      {QStringLiteral("concat.cancel"), {QStringLiteral("Cancel"), QStringLiteral("Cancelar")}},
      {QStringLiteral("concat.preparing"),
       {QStringLiteral("Joining %1 videos into one\u2026 this can take a while depending on file size."),
        QStringLiteral("Uniendo %1 videos en uno solo\u2026 puede tardar según el tamaño de los archivos.")}},
      {QStringLiteral("concat.error_ffmpeg"),
       {QStringLiteral("Combining videos needs FFmpeg, which should be inside AVA. Re-download the app from camilaescudero.cl/ava."),
        QStringLiteral("Combinar videos necesita FFmpeg, que debería estar dentro de AVA. Vuelva a descargar la aplicación desde camilaescudero.cl/ava.")}},
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
       {QStringLiteral("Converting this format so it plays smoothly\u2026 this only happens once, and your original file is kept."),
        QStringLiteral("Convirtiendo este formato para que se reproduzca bien\u2026 solo pasa una vez, y se conserva tu archivo original.")}},
      {QStringLiteral("playback_prep.cancel"), {QStringLiteral("Cancel"), QStringLiteral("Cancelar")}},
      {QStringLiteral("playback_prep.error_ffmpeg"),
       {QStringLiteral("Playing this video format needs FFmpeg, which should be inside AVA. Re-download the app from camilaescudero.cl/ava."),
        QStringLiteral("Reproducir este formato de video necesita FFmpeg, que debería estar dentro de AVA. Vuelva a descargar la aplicación desde camilaescudero.cl/ava.")}},
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
      {QStringLiteral("xml_import.mapping_same_abbrev"),
       {QStringLiteral("Home and away cannot use the same abbreviation for the XML."),
        QStringLiteral(
            "El local y el visitante no pueden usar la misma abreviatura para el XML.")}},
      {QStringLiteral("xml_import.complete_summary"),
       {QStringLiteral("Imported %1 events (%2 skipped, %3 clamped to video bounds)."),
        QStringLiteral("Se importaron %1 eventos (%2 omitidos, %3 ajustados a los límites del video).")}},
      {QStringLiteral("xml_import.adjusted_before_start_one"),
       {QStringLiteral("1 event started before the video. It was kept, beginning at 00:00, with its original length."),
        QStringLiteral("1 evento empezaba antes del video. Lo dejamos desde el inicio, con su duración.")}},
      {QStringLiteral("xml_import.adjusted_before_start_many"),
       {QStringLiteral("%1 events started before the video. They were kept from 00:00, with their length or only the part that overlaps the video."),
        QStringLiteral("%1 eventos empezaban antes del video. Los dejamos desde el inicio, con su duración o solo la parte que entra en el video.")}},
      {QStringLiteral("xml_import.no_sync_anchor"),
       {QStringLiteral("No usable instance was found to align the XML with the video."),
        QStringLiteral("No se encontró una instancia usable para alinear el XML con el video.")}},
      {QStringLiteral("shootout.round"),
       {QStringLiteral("Round %1"), QStringLiteral("Ronda %1")}},
      {QStringLiteral("shootout.sudden_death"),
       {QStringLiteral("Sudden death %1"), QStringLiteral("Muerte súbita %1")}},
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

QString trUiForLanguage(const char* key, Language language) {
  if (!key) return QString();
  const QLatin1String latinKey(key);
  const QHash<QString, QString>& strings = uiStringsForLanguage(language);
  const auto iterator = strings.constFind(latinKey);
  if (iterator == strings.cend()) return QString::fromLatin1(key);
  return iterator.value();
}

QString trUi(const char* key) {
  return trUiForLanguage(key, currentLanguage());
}

} // namespace AppLocale

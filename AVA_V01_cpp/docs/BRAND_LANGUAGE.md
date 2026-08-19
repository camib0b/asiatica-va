# AVA brand language (EN / ES)

Agent-facing guide for UI wording. Use this when adding or editing labels, tooltips, placeholders, dialogs, and empty states.

**Source of truth for strings:** `[i18n/AppLocale.cpp](../i18n/AppLocale.cpp)`
This document explains *how* to choose wording. Prefer existing `trUi("…")` keys. If you need new copy, match the tone and glossary below, then add **both** English and Spanish entries in `AppLocale.cpp`.

Canonical field-hockey **event tokens stay English** in session data and code. Spanish display comes from `spanishEventMap()` / `trEvent()` / `trDisplayTagLine()`.

---

## Voice and tone

AVA speaks to coaches and video analysts. Copy is short, direct, and practical—not marketing.


| Principle          | English                                  | Spanish                                              |
| ------------------ | ---------------------------------------- | ---------------------------------------------------- |
| Imperative actions | Select, Export, Clear, Apply             | Elegir, Exportar, Quitar, Aplicar                    |
| Keyboard inline    | `(M)`, `Ctrl+Z`, `Tab: … · Shift+Tab: …` | Same pattern; key names stay Latin (`Tab`, `Ctrl+Z`) |
| Guidance person    | “you” (neutral)                          | Informal **tú** (`Selecciona…`), not *usted*         |
| Loanwords to keep  | clip, overlay, XML, YouTube, Flick, Push | Same—do not force pure-Spanish substitutes           |
| Length             | Prefer one short sentence                | Prefer one short sentence                            |


**Avoid:** promotional fluff, long explanations in buttons, inventing synonyms when a glossary term already exists.

---

## Core product glossary


| Concept                         | English                          | Spanish                            | Use / avoid                                              |
| ------------------------------- | -------------------------------- | ---------------------------------- | -------------------------------------------------------- |
| Product                         | AVA                              | AVA                                | Brand name never translated                              |
| Timed mark on the timeline      | tag                              | marca                              | ES list header is **Marcas**, not *Etiquetas*            |
| Video segment around a mark     | clip                             | clip                               | Same word both languages                                 |
| Taxonomy node                   | event                            | evento                             |                                                          |
| Pre / post window around a mark | lead / lag                       | anticipación / retardo             | Column labels; not “before/after” as the primary terms   |
| Sides (fallback labels)         | Home / Away                      | Local / Visitante                  | UI filters and setup; see consistency traps for *Visita* |
| Work modes                      | Tagging, Analyzing, Presentation | Etiquetado, Análisis, Presentación | Mode button uses **Presentation** / **Presentación**     |
| Free-text annotation            | note                             | nota                               |                                                          |
| Aggregate counts                | Stats                            | Estadísticas                       |                                                          |
| Burned-in graphics              | overlay                          | overlay                            | Keep the English loanword in ES UI                       |
| Session XML report              | XML report                       | reporte XML                        |                                                          |


### Good / avoid examples


|                       | Prefer                          | Avoid                                                          |
| --------------------- | ------------------------------- | -------------------------------------------------------------- |
| EN presentation panel | Selected clips                  | Clips to present                                               |
| EN empty state        | No clips selected               | Nothing chosen yet                                             |
| ES tag list           | Marcas                          | Etiquetas                                                      |
| ES clip windows       | Anticipación / Retardo          | Lead / Lag (untranslated), or Antes / Después as column titles |
| ES sides              | Equipo local / Equipo visitante | Equipo home / Equipo away                                      |
| EN mode               | Presentation                    | Presenting (as the mode label)                                 |


---

## Field-hockey event terminology

Event **canonical** tokens remain English in data. Translate only for display via `trEvent()`.

### Main grid


| English (canonical) | Spanish display |
| ------------------- | --------------- |
| Circle Entry        | Ingreso área    |
| Shot                | Tiro            |
| Goal                | Gol             |
| PC                  | Corto           |
| PS                  | Penal           |
| SO                  | SO              |
| Pass                | Pase            |
| Turnover            | Pérdida         |
| Card                | Tarjeta         |
| PC Foul             | Falta PC        |


### Keep English in Spanish UI

Flick, Push, S.O.

### Notable Spanish choices (follow-ups)


| English                                | Spanish                    |
| -------------------------------------- | -------------------------- |
| On target                              | Al arco                    |
| Off target                             | Afuera                     |
| Direct shot                            | Directo                    |
| Sweep / Swept                          | Barrida                    |
| Hit                                    | Pegada                     |
| Dragflick                              | Arrastre                   |
| Dribling                               | Conducción                 |
| Tackle                                 | Quite                      |
| Good / Bad                             | Positivo / Negativo        |
| Referee                                | Arbitraje                  |
| Off / Def                              | Ofensiva / Defensiva       |
| Saved                                  | Atajado                    |
| Post / Stick                           | Palo                       |
| Unforced error                         | Error                      |
| Converted / Missed                     | convertido / no convertido |
| Replay                                 | repite                     |
| home / away (default follow-up labels) | Local / Visita             |


Full map: `spanishEventMap()` in `AppLocale.cpp`. Prefer that map over inventing a new translation.

---

## UI microcopy patterns

Recurring patterns from existing strings—reuse them.


| Pattern                         | English                                                                        | Spanish                                                                                          |
| ------------------------------- | ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------ |
| Form label + colon              | `Event:`, `Team:`                                                              | `Evento:`, `Equipo:`                                                                             |
| Incomplete action (menu/dialog) | `Export clips…`, `Import XML…`                                                 | `Exportar clips…`, `Importar XML…`                                                               |
| Placeholder ellipsis            | `Note for selected tag…`                                                       | `Nota sobre el clip seleccionado…` (see traps)                                                   |
| Middle-dot separator            | `%1 selected · %2 listed`                                                      | `%1 seleccionados · %2 en lista`                                                                 |
| Keyboard hint list              | `Tab: next clip · Shift+Tab: previous clip · Space: play/pause · Arrows: seek` | `Tab: clip siguiente · Shift+Tab: clip anterior · Espacio: reproducir/pausar · Flechas: avanzar` |
| Shortcut on control             | `Start game (G)`, `Undo` + tooltip `Ctrl+Z …`                                  | `Iniciar partido (G)`, `Ctrl+Z …`                                                                |
| Dialog verbs                    | Back, Continue, Cancel, Close                                                  | Atrás, Continuar, Cancelar, Cerrar                                                               |
| Browse / choose file            | Browse…, Select a video file                                                   | Buscar…, Seleccionar archivo de video                                                            |
| Success (short)                 | Export complete.                                                               | Exportación completa.                                                                            |
| Error (plain)                   | Please choose an output file path.                                             | Por favor elija una ruta de archivo de salida.                                                   |


Use the Unicode ellipsis `…`, not three ASCII dots `...`, when matching existing copy.

---

## Mode-specific cheat sheet

### Tagging — Etiquetado

- Focus: eyes on video, hands on keyboard.
- Nouns: **tags** / **marcas**; undo removes the most recent **tag** / **marca**.
- EN examples: `Tags`, `Remove most recent tag`, `Note for selected tag…`
- ES examples: `Marcas`, `Quitar la última marca`

### Analyzing — Análisis

- Focus: stats and notes.
- EN: `Stats`, `Stats and notes (M)`
- ES: `Estadísticas`, `Estadísticas y notas (M)`

### Presentation — Presentación

- Focus: selected clips on a large player; lead/lag around each event mark; export from here.
- EN: `Selected clips`, `Current clip`, `Apply to all selected clips`, `Export selected clips`, `Show notes on screen`
- ES: `Clips seleccionados`, `Clip actual`, `Aplicar a todos los clips seleccionados`, `Exportar clips seleccionados`, `Mostrar notas en pantalla`
- Empty state EN: `No clips selected` / `Use Tab and Shift Tab to navigate between the clips you select.`
- Empty state ES: `Selecciona clips en el panel izquierdo` / guidance with Tab / Shift Tab

Lead/lag in this mode:

- EN: lead, lag (seconds)
- ES: anticipación, retardo (segundos)
- Tooltip idea: same lead and lag **around each clip’s own event mark** / **alrededor de su propia marca**

---

## Consistency traps (do not invent a third variant)

1. **tag vs clip** — A *tag* is the timed mark; a *clip* is the playable/exportable segment around it. In Spanish, prefer **marca** for the mark and **clip** for the segment. If you edit an existing string that mixes them (e.g. ES note placeholder saying “clip” while EN says “tag”), align the pair to this glossary rather than inventing a new synonym.
2. **Visitante vs Visita** — Setup and filters use **Visita**. Do not invent *Equipo away* or *Away team* → mixed English.
3. **Retained English** — Do not translate: AVA, clip, overlay, XML, YouTube, Flick, Push, S.O., or the **PC** in “Falta PC”.
4. **Mode label** — Button text is **Presentation** / **Presentación**, not “Presenting” / “Presentando”.
5. **Lead/lag columns** — Use **Lead (s)** / **Lag (s)** and **Antes (s)** / **Después (s)**. Phrases like “before the tag” / “Antes de la marca” are fine in longer sentences, not as the primary column names.
6. **Always ship both languages** — Every new `trUi` key needs an English entry and a Spanish entry in `AppLocale.cpp`.

---

## Quick checklist for new UI copy

1. Is there already a `trUi` key for this?
2. Does the noun match the glossary (tag/marca, clip/clip, lead/lag ↔ anticipación/retardo)?
3. Is Spanish informal *tú* for guidance, and loanwords left where the product already keeps them?
4. Are keyboard shortcuts and `…` / `·` patterns consistent with nearby strings?
5. Did you add both EN and ES in `AppLocale.cpp`?


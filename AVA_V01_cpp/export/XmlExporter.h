#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <optional>

class TagSession;

/// Serializes a TagSession into a LongoMatch-compatible <ALL_INSTANCES> + <ROWS> XML document.
///
/// Categories are keyed only by the GameControls main event (e.g. all PCs for a team share one
/// <code> such as "HOM PC+"); follow-up drill-down paths from tagging are not written to XML.
/// Scored goals are always exported as explicit Goal instances (<code> … GOAL+ / GOAL-), including
/// when the session only recorded the goal inside another event's follow-up (PC, Shot, PS, etc.).
///
/// <start>/<end> use EventDefaults lead/lag for each main event around the tag position; manually
/// trimmed tags (export review or fixed game-time spans) keep their stored start/end instead.
/// QUARTOS is the quarter span that contains the event mark (closed Q1–Q4, or the quarter
/// still in progress). It is not the period stored on the tag: that value is whichever
/// quarter was current when the analyst tagged, including after rolling the video back.
/// Instance IDs follow game time (mark), with an originating event before a derived Goal that
/// shares its mark; RESULTADO only counts Goal instances already emitted. IDs are assigned once
/// by buildExportedInstances(); compilation sidecars must use taggedTeamInstanceFor() so a clip
/// stores the same <ID> this writer emits. The tagged-team "+" instance is emitted before the
/// opposing "-" instance.
///
/// The output format mirrors the reference shipped with the spec:
///   <file>
///     <ALL_INSTANCES>
///       <instance>
///         <ID>integer</ID>
///         <start>seconds.fff</start>
///         <end>seconds.fff</end>
///         <code>HOME GOAL+</code>
///         <label><group>QUARTOS</group><text>Q1</text></label>
///         ...
///       </instance>
///       ...
///     </ALL_INSTANCES>
///     <ROWS>
///       <row>
///         <code>HOME GOAL+</code>
///         <R>0..65535</R><G>0..65535</G><B>0..65535</B>
///       </row>
///       ...
///     </ROWS>
///   </file>
namespace XmlExporter {

struct ExportedXmlLabel {
  QString group;
  QString text;
};

/// One <instance> in emission order, including the <ID> that writeAllInstances will write.
struct ExportedXmlInstance {
  int id = 0;
  quint64 sourceTagId = 0;
  QString mainEvent;
  QString team;
  qint64 startMs = 0;
  qint64 endMs = 0;
  QString code;
  QVector<ExportedXmlLabel> labels;
};

/// Ordered catalog of every instance the XML writer emits, with IDs already assigned.
/// Empty when \p session is null. Synthetic follow-up Goals are included.
QVector<ExportedXmlInstance> buildExportedInstances(const TagSession* session);

/// The tagged-team instance for a compilation clip: the first catalog entry with this
/// tag id and main event. That is the "+" code when the event emits a +/- pair.
std::optional<ExportedXmlInstance> taggedTeamInstanceFor(const QVector<ExportedXmlInstance>& instances,
                                                          quint64 sourceTagId,
                                                          const QString& mainEvent);

/// Writes the entire session to \p filePath. Returns true on success; on failure populates
/// \p errorMessage (when non-null) with a human-readable explanation.
bool writeAllInstances(const TagSession* session,
                       const QString& filePath,
                       QString* errorMessage = nullptr);

} // namespace XmlExporter

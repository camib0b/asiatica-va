#pragma once

#include <QString>

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

/// Writes the entire session to \p filePath. Returns true on success; on failure populates
/// \p errorMessage (when non-null) with a human-readable explanation.
bool writeAllInstances(const TagSession* session,
                       const QString& filePath,
                       QString* errorMessage = nullptr);

} // namespace XmlExporter

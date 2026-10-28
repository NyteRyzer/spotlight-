#pragma once

#include "core/projectdata.h"

#include <QSet>

// Pure editing operations on ProjectData's timeline.
//
// Invariants maintained by every operation:
//  * clips on a track are sorted by start and never overlap
//  * clips never start before 0
//  * clips are never shorter than kMinClipDuration
// Placing or moving clips uses "overwrite" semantics like most NLEs: whatever
// was underneath on the destination track is trimmed, split or removed.
namespace spotlight::timeline {

// Linked clips (e.g. a video clip and its audio) are edited as a group.
QVector<QUuid> groupOf(const ProjectData &d, const QUuid &clipId);

// Index of the clip covering time t on the track, or -1.
int clipIndexAt(const Track &track, TimeUs t);

// Removes all material in [from, to) on the track.
void clearRange(Track &track, TimeUs from, TimeUs to);

// Inserts a clip keeping the track sorted (caller ensures the space is free).
void insertSorted(Track &track, const Clip &clip);

// Places media on the timeline. Video files with sound get a linked audio clip
// on the matching audio track (V1 -> A1, V2 -> A2, created if missing).
// out < 0 means "to the end of the media". Returns the primary clip id or a
// null id if nothing could be placed.
QUuid placeMedia(ProjectData &d, const QUuid &mediaId, int trackIndex, TimeUs start, TimeUs in = 0,
                 TimeUs out = -1);

// Moves a clip (and its linked partners by the same offset). The primary clip
// may change to another track of the same type. Returns false if nothing moved.
bool moveClip(ProjectData &d, const QUuid &clipId, TimeUs newStart, int newTrackIndex);

// Trims the clip's head/tail to the requested timeline time, clamped to media
// bounds and neighbouring clips. Linked partners are trimmed identically.
bool trimClipStart(ProjectData &d, const QUuid &clipId, TimeUs newStart);
bool trimClipEnd(ProjectData &d, const QUuid &clipId, TimeUs newEnd);

// Splits the given clips (plus linked partners) at time `at`.
// Returns the ids of the newly created right-hand pieces.
QVector<QUuid> splitClips(ProjectData &d, const QVector<QUuid> &clipIds, TimeUs at);

// Splits every clip under `at` on unlocked tracks (the "razor at playhead").
QVector<QUuid> splitAll(ProjectData &d, TimeUs at);

// Deletes clips (plus linked partners). With ripple, later clips on the same
// tracks shift left to close the gap.
int removeClips(ProjectData &d, const QVector<QUuid> &clipIds, bool ripple);

// Removes a media item and every clip that uses it.
void removeMedia(ProjectData &d, const QUuid &mediaId);

// Top-most visible video clip at time t (higher video tracks win).
const Clip *topVideoClipAt(const ProjectData &d, TimeUs t, int *trackIndex = nullptr);

// The timeline flattened into consecutive video segments from 0 to the
// project duration. clipId is null for gaps (rendered black).
struct VideoSegment {
    TimeUs start = 0;
    TimeUs end = 0;
    QUuid clipId;
    int trackIndex = -1;
    TimeUs duration() const { return end - start; }
};
QVector<VideoSegment> videoSegments(const ProjectData &d);

// Whether an audio track is heard, honouring mute and solo.
bool isTrackAudible(const ProjectData &d, int trackIndex);

// Snaps t to the nearest clip edge (or extra point such as the playhead)
// within threshold. Clips in `exclude` are ignored.
TimeUs snapTime(const ProjectData &d, TimeUs t, TimeUs threshold, const QSet<QUuid> &exclude = {},
                const QVector<TimeUs> &extraPoints = {});

// Envelope multiplier (0..1) for a clip's fades at timeline time t.
double fadeEnvelope(const Clip &c, TimeUs t);

// ---- Copy / paste -------------------------------------------------------
// Clips captured by copyClips(), positioned relative to the earliest one.
struct ClipboardEntry {
    QUuid trackId;
    int trackIndex = -1; // fallback when the track no longer exists
    Clip clip;           // clip.start is relative to the copied range start
};
// Copies the clips (plus linked partners) for later pasting.
QVector<ClipboardEntry> copyClips(const ProjectData &d, const QVector<QUuid> &clipIds);
// Pastes clips with their relative layout starting at `at` (overwrite edit).
// Links between pasted clips are preserved with fresh ids. Returns new ids.
QVector<QUuid> pasteClips(ProjectData &d, const QVector<ClipboardEntry> &entries, TimeUs at);
// End of the latest clip among clipIds (and partners); 0 if none.
TimeUs selectionEnd(const ProjectData &d, const QVector<QUuid> &clipIds);

// ---- Markers ------------------------------------------------------------
// Adds a marker at t, or removes the one already within `tolerance` of t.
// Returns true if a marker was added.
bool toggleMarker(ProjectData &d, TimeUs t, TimeUs tolerance, const QString &name = QString());
// Nearest marker strictly after / before t, or -1.
TimeUs nextMarker(const ProjectData &d, TimeUs t);
TimeUs previousMarker(const ProjectData &d, TimeUs t);

// ---- Relinking ----------------------------------------------------------
// Searches `folder` (recursively) for files named like missing media and
// points those media items at them. Returns the number relinked.
int relinkMissingMedia(ProjectData &d, const QString &folder);

// ---- Pro editing ------------------------------------------------------
// Opens a gap of `duration` at `at` on every unlocked track: clips under `at`
// are split and everything from `at` on moves right (insert edit).
void insertGap(ProjectData &d, TimeUs at, TimeUs duration);

// Insert edit: like placeMedia, but pushes later material right instead of
// overwriting it. Returns the primary clip id.
QUuid insertMedia(ProjectData &d, const QUuid &mediaId, int trackIndex, TimeUs at, TimeUs in = 0, TimeUs out = -1);

// Removes [from, to) from every unlocked track and closes the gap.
void rippleDeleteRange(ProjectData &d, TimeUs from, TimeUs to);
// Ripple-deletes several ranges (e.g. detected silences); ranges may be in
// any order and may overlap. Returns the total time removed.
TimeUs rippleDeleteRanges(ProjectData &d, QVector<QPair<TimeUs, TimeUs>> ranges);

// Changes the playback speed of a clip (and linked partners), keeping its
// start. If the clip gets longer, later clips on the same tracks move right
// to make room. Stills ignore speed.
bool setClipSpeed(ProjectData &d, const QUuid &clipId, double speed);

// Moves every clip on the track left so there are no gaps (CapCut's
// "magnetic" track). Linked partners move with them. Returns clips moved.
int closeGaps(ProjectData &d, int trackIndex);

// Adds a marker on every beat (or every bar) between from and to.
// Returns the number of markers added.
int addBeatMarkers(ProjectData &d, double bpm, TimeUs offset, TimeUs from, TimeUs to, int beatsPerBar,
                   bool barsOnly);

// Freeze frame: splits the clip at `at` and inserts `stillMediaId` (a frame
// grabbed at `at`) for `duration`, pushing later material right. The still
// inherits the clip's look and transform. Returns the still clip's id.
QUuid insertFreezeFrame(ProjectData &d, const QUuid &clipId, TimeUs at, const QUuid &stillMediaId,
                        TimeUs duration);

// The clip on the same track ending exactly where `c` starts, or nullptr.
const Clip *previousAdjacent(const Track &t, const Clip &c);
// The transition into `c` with its duration clamped to what fits, or none
// when there is no adjacent clip to transition from.
Transition effectiveTransition(const Track &t, const Clip &c);
// Opacity (0..1) of a video clip at time t from its fades. Fades are skipped
// where a transition takes over (as in the export).
double videoAlpha(const Track &t, const Clip &c, TimeUs at);

// ---- More editing ------------------------------------------------------
// The project with disabled clips removed (what preview and export show).
ProjectData withoutDisabledClips(const ProjectData &d);

// Ripple trim (Premiere Q / W): trims the clip's head (or tail) to `at` and
// closes the gap, pulling later clips on the same tracks left.
bool rippleTrim(ProjectData &d, const QUuid &clipId, TimeUs at, bool head);

// Nearest clip edge on any track strictly after / before t (-1 if none).
TimeUs nextEditPoint(const ProjectData &d, TimeUs t);
TimeUs previousEditPoint(const ProjectData &d, TimeUs t);

// Lift: removes [from, to) on unlocked tracks and leaves a gap.
// (Extract is rippleDeleteRange.)
void liftRange(ProjectData &d, TimeUs from, TimeUs to);

// Moves the clips (and partners) by delta, e.g. one frame (overwrite edit).
int nudgeClips(ProjectData &d, const QVector<QUuid> &clipIds, TimeUs delta);

// A copy of the project trimmed to [from, to), starting at 0 (for exporting
// the In/Out range). Locks are ignored.
ProjectData rangeProject(const ProjectData &d, TimeUs from, TimeUs to);

// Puts other media into the clip (and partners) keeping position, length
// limits and effects. Partners whose track can't hold the new media are
// removed. Returns false if nothing changed.
bool replaceClipMedia(ProjectData &d, const QUuid &clipId, const QUuid &mediaId);

// Paste Attributes: copies the chosen groups of settings from `src`.
struct AttributeSet {
    bool color = true;      // look, colour
    bool transform = true;  // position/scale/crop/opacity, motion
    bool effects = true;    // effects, chroma key, stabilise
    bool audio = true;      // gain, fades
    bool speed = false;
};
int pasteAttributes(ProjectData &d, const QVector<QUuid> &clipIds, const Clip &src, const AttributeSet &what);

// Gives the clips fade in/out of `length` (at most half the clip each).
int applyQuickFades(ProjectData &d, const QVector<QUuid> &clipIds, TimeUs length);

// YouTube chapter list ("0:00 Intro") from the markers. YouTube needs the
// first chapter at 0:00, three or more chapters and 10 s+ each: `warnings`
// explains anything that won't qualify.
QString youtubeChapters(const ProjectData &d, QStringList *warnings = nullptr);

// Collect Files: where each used media file should be copied inside
// `folder` (unique file names). Titles and missing files are skipped.
struct CopyPlan {
    QUuid mediaId;
    QString from;
    QString to;
};
QVector<CopyPlan> collectPlan(const ProjectData &d, const QString &folder);
// Media items no clip uses.
QVector<QUuid> unusedMedia(const ProjectData &d);

// Splits a clip (and partners) at every time in `cuts` that falls inside it.
int splitAtTimes(ProjectData &d, const QUuid &clipId, const QVector<TimeUs> &cuts);

} // namespace spotlight::timeline

#pragma once

#include "core/timeline.h"
#include "core/types.h"

#include <QObject>
#include <QPointer>

#include <functional>
#include <QUuid>

class QWidget;

namespace spotlight {

class Project;
class TimelinePlayer;
class TimelineWidget;
class MediaBin;

// Editing tools that need dialogs, FFmpeg or file I/O on top of the pure
// timeline operations: freeze frame, reverse, remove silences, scene-cut
// detection, beat markers, titles, captions, voice-over recording, insert
// edits, close gaps and vertical auto-reframe.
class EditTools : public QObject {
    Q_OBJECT
public:
    EditTools(Project *project, TimelinePlayer *player, MediaBin *bin, QWidget *window);

    // Which timeline the tools act on (Video or Audio Studio).
    void setTimelineProvider(std::function<TimelineWidget *()> provider) { m_timeline = std::move(provider); }

public slots:
    void freezeFrame();
    void reverseClip();
    void removeSilences();
    void detectScenes();
    void addBeatMarkers();
    void addTitle(int preset = 0);
    void editTitle();
    void importCaptions();
    void exportCaptions();
    void recordVoiceover();
    void insertAtPlayhead();
    void closeGaps();
    void autoReframe();

    // Premiere-style trims and edits.
    void rippleTrimHead(); // Q
    void rippleTrimTail(); // W
    void nudge(int frames);
    void toggleEnabled();
    void setLabel(int label);
    void replaceWithBinMedia();
    void copyAttributes();
    void pasteAttributes();
    void quickFades();
    void liftInOut();
    void extractInOut();

    // Analysis and delivery.
    void detectTempo();     // BPM detection -> beat markers
    void loudnessReport();  // EBU R128 of the whole mix
    void youtubeChapters();
    void exportStems();     // each audio track as its own WAV
    void collectFiles();    // copy project + media into one folder
    void removeUnusedMedia();

signals:
    void status(const QString &message);

private:
    // The selected clip, else the clip under the playhead on the current
    // track, else the top video clip under the playhead.
    QUuid targetClip(bool videoOnly = false) const;
    // Selected clips, else the target clip.
    QVector<QUuid> targetClips() const;
    // Clip under the playhead for ripple trims (selected one first).
    QUuid clipUnderPlayhead() const;
    void rippleTrimAt(bool head);
    // Runs tempo detection on a clip; fills bpm/first beat (timeline time).
    bool detectTempoOf(const QUuid &clipId, double *bpm, TimeUs *firstBeat, double *confidence);
    TimelineWidget *timeline() const { return m_timeline ? m_timeline() : nullptr; }
    // Index of a video track with room for [start, end), adding one on top
    // if every video track is busy there.
    static int freeOverlayTrack(struct ProjectData &d, TimeUs start, TimeUs end);

    Project *m_project;
    TimelinePlayer *m_player;
    MediaBin *m_bin;
    QWidget *m_window;
    std::function<TimelineWidget *()> m_timeline;
    bool m_hasAttributes = false;
    Clip m_attributes; // source of Paste Attributes
};

} // namespace spotlight

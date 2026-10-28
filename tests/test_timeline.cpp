#include "core/analysis.h"
#include "core/looks.h"
#include "core/timelineops.h"
#include "core/titles.h"
#include "testutil.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace spotlight;
using namespace testutil;

class TestTimeline : public QObject {
    Q_OBJECT

    static void checkInvariants(const ProjectData &d)
    {
        for (const auto &t : d.tracks) {
            for (int i = 0; i < t.clips.size(); ++i) {
                QVERIFY(t.clips[i].start >= 0);
                QVERIFY(t.clips[i].duration() >= kMinClipDuration);
                if (i > 0)
                    QVERIFY2(t.clips[i - 1].end() <= t.clips[i].start, "clips overlap");
            }
        }
    }

private slots:
    void defaultProjectHasTracks()
    {
        const ProjectData d = ProjectData::createDefault();
        QCOMPARE(d.videoTrackIndices().size(), 2);
        QCOMPARE(d.audioTrackIndices().size(), 2);
        QVERIFY(d.tracks[0].isVideo());
        QVERIFY(d.tracks[3].isAudio());
    }

    void placeVideoCreatesLinkedAudio()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S);
        const QUuid id = timeline::placeMedia(d, m, 0, 2 * S);
        QVERIFY(!id.isNull());
        QCOMPARE(d.tracks[0].clips.size(), 1);
        QCOMPARE(d.tracks[2].clips.size(), 1); // A1
        QCOMPARE(d.tracks[0].clips[0].linkId, d.tracks[2].clips[0].linkId);
        QCOMPARE(d.tracks[0].clips[0].start, 2 * S);
        QCOMPARE(d.duration(), 12 * S);
        QCOMPARE(timeline::groupOf(d, id).size(), 2);
    }

    void placeOnV2UsesA2()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 4 * S);
        timeline::placeMedia(d, m, 1, 0);
        QCOMPARE(d.tracks[1].clips.size(), 1);
        QCOMPARE(d.tracks[3].clips.size(), 1);
    }

    void placeAudioGoesToAudioTrack()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addAudio(d, 30 * S);
        timeline::placeMedia(d, m, 0, 0); // asked for V1, lands on A1
        QCOMPARE(d.tracks[0].clips.size(), 0);
        QCOMPARE(d.tracks[2].clips.size(), 1);
        QVERIFY(d.tracks[2].clips[0].linkId.isNull());
    }

    void placeImageUsesDefaultDuration()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addImage(d);
        timeline::placeMedia(d, m, 0, 0);
        QCOMPARE(d.tracks[0].clips[0].duration(), kDefaultStillDuration);
        QCOMPARE(d.tracks[2].clips.size(), 0);
    }

    void overwriteSplitsUnderlyingClip()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid a = addVideo(d, 10 * S, false);
        const QUuid b = addVideo(d, 2 * S, false);
        timeline::placeMedia(d, a, 0, 0);
        timeline::placeMedia(d, b, 0, 4 * S);
        checkInvariants(d);
        const auto &clips = d.tracks[0].clips;
        QCOMPARE(clips.size(), 3);
        QCOMPARE(clips[0].end(), 4 * S);
        QCOMPARE(clips[1].mediaId, b);
        QCOMPARE(clips[2].start, 6 * S);
        QCOMPARE(clips[2].in, 6 * S); // right remainder keeps source alignment
        QVERIFY(clips[0].id != clips[2].id);
    }

    void moveClipMovesLinkedPartner()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 5 * S);
        const QUuid id = timeline::placeMedia(d, m, 0, 0);
        QVERIFY(timeline::moveClip(d, id, 3 * S, 1)); // to V2
        QCOMPARE(d.tracks[0].clips.size(), 0);
        QCOMPARE(d.tracks[1].clips[0].start, 3 * S);
        QCOMPARE(d.tracks[2].clips[0].start, 3 * S); // audio stays on A1, shifted
        checkInvariants(d);
    }

    void moveClampsAtZeroAndRejectsWrongTrackType()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 5 * S, false);
        const QUuid id = timeline::placeMedia(d, m, 0, 2 * S);
        QVERIFY(timeline::moveClip(d, id, -10 * S, 2)); // A1 isn't a video track
        QCOMPARE(d.tracks[0].clips[0].start, 0);
        QCOMPARE(d.tracks[2].clips.size(), 0);
    }

    void lockedTrackBlocksEdits()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 5 * S, false);
        const QUuid id = timeline::placeMedia(d, m, 0, 0);
        d.tracks[0].locked = true;
        QVERIFY(!timeline::moveClip(d, id, 1 * S, 0));
        QVERIFY(!timeline::trimClipEnd(d, id, 2 * S));
        QCOMPARE(timeline::removeClips(d, {id}, false), 0);
    }

    void trimRespectsMediaBoundsAndNeighbours()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S, false);
        const QUuid first = timeline::placeMedia(d, m, 0, 0, 0, 4 * S);
        timeline::placeMedia(d, m, 0, 6 * S, 0, 2 * S);

        // Extending the tail stops at the next clip.
        QVERIFY(timeline::trimClipEnd(d, first, 9 * S));
        QCOMPARE(d.clipById(first)->end(), 6 * S);

        // Shortening the head moves the in-point.
        QVERIFY(timeline::trimClipStart(d, first, 1 * S));
        QCOMPARE(d.clipById(first)->start, 1 * S);
        QCOMPARE(d.clipById(first)->in, 1 * S);

        // Extending the head can't go before source in = 0.
        QVERIFY(timeline::trimClipStart(d, first, -5 * S));
        QCOMPARE(d.clipById(first)->in, 0);
        QCOMPARE(d.clipById(first)->start, 0);

        // Minimum duration.
        QVERIFY(timeline::trimClipEnd(d, first, 0));
        QCOMPARE(d.clipById(first)->duration(), kMinClipDuration);
        checkInvariants(d);
    }

    void trimEndClampedToMediaDuration()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S);
        const QUuid id = timeline::placeMedia(d, m, 0, 0, 0, 5 * S);
        QVERIFY(timeline::trimClipEnd(d, id, 60 * S));
        QCOMPARE(d.clipById(id)->out, 10 * S);
        // Linked audio trimmed identically.
        QCOMPARE(d.tracks[2].clips[0].out, 10 * S);
    }

    void stillsCanBeExtendedFreely()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addImage(d);
        const QUuid id = timeline::placeMedia(d, m, 0, 5 * S);
        QVERIFY(timeline::trimClipEnd(d, id, 60 * S));
        QCOMPARE(d.clipById(id)->end(), 60 * S);
        QVERIFY(timeline::trimClipStart(d, id, 1 * S));
        QCOMPARE(d.clipById(id)->start, 1 * S);
        QCOMPARE(d.clipById(id)->in, 0);
        QCOMPARE(d.clipById(id)->end(), 60 * S);
    }

    void splitKeepsLinksPaired()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S);
        const QUuid id = timeline::placeMedia(d, m, 0, 0);
        const QVector<QUuid> created = timeline::splitAll(d, 4 * S);
        QCOMPARE(created.size(), 2);
        QCOMPARE(d.tracks[0].clips.size(), 2);
        QCOMPARE(d.tracks[2].clips.size(), 2);
        // Left halves still linked together, right halves linked together.
        QCOMPARE(d.tracks[0].clips[0].linkId, d.tracks[2].clips[0].linkId);
        QCOMPARE(d.tracks[0].clips[1].linkId, d.tracks[2].clips[1].linkId);
        QVERIFY(d.tracks[0].clips[0].linkId != d.tracks[0].clips[1].linkId);
        QCOMPARE(d.clipById(id)->out, 4 * S);
        QCOMPARE(d.tracks[0].clips[1].in, 4 * S);
        checkInvariants(d);
    }

    void splitAtEdgeDoesNothing()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S, false);
        timeline::placeMedia(d, m, 0, 0);
        QVERIFY(timeline::splitAll(d, 0).isEmpty());
        QVERIFY(timeline::splitAll(d, 10 * S).isEmpty());
    }

    void rippleDeleteClosesGap()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 3 * S, false);
        const QUuid a = timeline::placeMedia(d, m, 0, 0);
        timeline::placeMedia(d, m, 0, 3 * S);
        timeline::placeMedia(d, m, 0, 6 * S);
        QCOMPARE(timeline::removeClips(d, {a}, true), 1);
        QCOMPARE(d.tracks[0].clips.size(), 2);
        QCOMPARE(d.tracks[0].clips[0].start, 0);
        QCOMPARE(d.tracks[0].clips[1].start, 3 * S);
        QCOMPARE(d.duration(), 6 * S);
    }

    void deleteRemovesLinkedPartner()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 3 * S);
        const QUuid a = timeline::placeMedia(d, m, 0, 0);
        QCOMPARE(timeline::removeClips(d, {a}, false), 2);
        QCOMPARE(d.duration(), 0);
    }

    void segmentsFlattenTracks()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S, false);
        const QUuid logo = addImage(d);
        const QUuid base = timeline::placeMedia(d, m, 0, 0);   // V1: 0-10
        const QUuid over = timeline::placeMedia(d, logo, 1, 2 * S); // V2: 2-7
        const auto segs = timeline::videoSegments(d);
        QCOMPARE(segs.size(), 3);
        QCOMPARE(segs[0].clipId, base);
        QCOMPARE(segs[0].end, 2 * S);
        QCOMPARE(segs[1].clipId, over);
        QCOMPARE(segs[1].end, 7 * S);
        QCOMPARE(segs[2].clipId, base);
        QCOMPARE(segs[2].end, 10 * S);

        // Hiding V2 reveals V1 entirely.
        d.tracks[1].muted = true;
        QCOMPARE(timeline::videoSegments(d).size(), 1);
    }

    void segmentsIncludeGaps()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 2 * S, false);
        timeline::placeMedia(d, m, 0, 3 * S);
        const auto segs = timeline::videoSegments(d);
        QCOMPARE(segs.size(), 2);
        QVERIFY(segs[0].clipId.isNull());
        QCOMPARE(segs[0].duration(), 3 * S);
    }

    void soloAndMute()
    {
        ProjectData d = ProjectData::createDefault();
        QVERIFY(timeline::isTrackAudible(d, 2));
        QVERIFY(!timeline::isTrackAudible(d, 0)); // video track
        d.tracks[3].solo = true;
        QVERIFY(!timeline::isTrackAudible(d, 2));
        QVERIFY(timeline::isTrackAudible(d, 3));
        d.tracks[3].muted = true;
        QVERIFY(!timeline::isTrackAudible(d, 3));
    }

    void snapping()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 5 * S, false);
        const QUuid id = timeline::placeMedia(d, m, 0, 0);
        QCOMPARE(timeline::snapTime(d, 5 * S + 50000, 100000), 5 * S);
        QCOMPARE(timeline::snapTime(d, 5 * S + 500000, 100000), 5 * S + 500000);
        QCOMPARE(timeline::snapTime(d, 5 * S + 50000, 100000, {id}), 5 * S + 50000);
        QCOMPARE(timeline::snapTime(d, 7 * S + 1000, 100000, {}, {7 * S}), 7 * S);
    }

    void fadeEnvelope()
    {
        Clip c;
        c.start = 0;
        c.in = 0;
        c.out = 10 * S;
        c.fadeIn = 2 * S;
        c.fadeOut = 2 * S;
        QCOMPARE(timeline::fadeEnvelope(c, 0), 0.0);
        QCOMPARE(timeline::fadeEnvelope(c, 1 * S), 0.5);
        QCOMPARE(timeline::fadeEnvelope(c, 5 * S), 1.0);
        QCOMPARE(timeline::fadeEnvelope(c, 9 * S), 0.5);
    }

    void copyPastePreservesLayoutAndLinks()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 4 * S);
        const QUuid id = timeline::placeMedia(d, m, 0, 1 * S);
        const auto clip = timeline::copyClips(d, {id});
        QCOMPARE(clip.size(), 2); // video + linked audio
        QCOMPARE(timeline::selectionEnd(d, {id}), 5 * S);

        const QVector<QUuid> pasted = timeline::pasteClips(d, clip, 10 * S);
        QCOMPARE(pasted.size(), 2);
        QCOMPARE(d.tracks[0].clips.size(), 2);
        QCOMPARE(d.tracks[2].clips.size(), 2);
        const Clip *v = d.clipById(pasted[0]);
        QCOMPARE(v->start, 10 * S);
        // The pasted pair is linked to each other, but not to the original.
        QCOMPARE(timeline::groupOf(d, pasted[0]).size(), 2);
        QVERIFY(v->linkId != d.clipById(id)->linkId);
        checkInvariants(d);
    }

    void pasteOverwritesAndSkipsLockedTracks()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S, false);
        const QUuid id = timeline::placeMedia(d, m, 0, 0, 0, 2 * S);
        timeline::placeMedia(d, m, 0, 3 * S, 0, 6 * S); // 3..9
        const auto clip = timeline::copyClips(d, {id});
        timeline::pasteClips(d, clip, 4 * S); // lands inside the second clip
        checkInvariants(d);
        QCOMPARE(d.tracks[0].clips.size(), 4);

        d.tracks[0].locked = true;
        QVERIFY(timeline::pasteClips(d, clip, 20 * S).isEmpty());
    }

    void markers()
    {
        ProjectData d = ProjectData::createDefault();
        QVERIFY(timeline::toggleMarker(d, 5 * S, 1000));
        QVERIFY(timeline::toggleMarker(d, 2 * S, 1000));
        QCOMPARE(d.markers.size(), 2);
        QCOMPARE(d.markers[0].time, 2 * S); // kept sorted
        QCOMPARE(timeline::nextMarker(d, 0), 2 * S);
        QCOMPARE(timeline::nextMarker(d, 2 * S), 5 * S);
        QCOMPARE(timeline::nextMarker(d, 5 * S), TimeUs(-1));
        QCOMPARE(timeline::previousMarker(d, 5 * S), 2 * S);
        QCOMPARE(timeline::previousMarker(d, 2 * S), TimeUs(-1));
        QVERIFY(!timeline::toggleMarker(d, 5 * S + 500, 1000)); // removes it
        QCOMPARE(d.markers.size(), 1);
    }

    void relinkMissingMedia()
    {
        QTemporaryDir tmp;
        QDir(tmp.path()).mkpath("deep/inside");
        QFile f(tmp.path() + "/deep/inside/Clip.MP4");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        ProjectData d = ProjectData::createDefault();
        addVideo(d, S, false, "/old/drive/clip.mp4");
        addVideo(d, S, false, "/old/drive/gone.mp4");
        QCOMPARE(timeline::relinkMissingMedia(d, tmp.path()), 1);
        QCOMPARE(d.media[0].path, QFileInfo(tmp.path() + "/deep/inside/Clip.MP4").absoluteFilePath());
        QCOMPARE(d.media[1].path, QStringLiteral("/old/drive/gone.mp4"));
    }


    // ---- Pro editing ----------------------------------------------------
    void speedChangesDurationAndSourceMapping()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 20 * S);
        const QUuid a = timeline::placeMedia(d, m, 0, 0, 0, 4 * S);
        const QUuid b = timeline::placeMedia(d, m, 0, 4 * S, 0, 2 * S);
        QVERIFY(timeline::setClipSpeed(d, a, 0.5)); // 4 s of source now lasts 8 s
        const Clip *ca = d.clipById(a);
        QCOMPARE(ca->duration(), 8 * S);
        QCOMPARE(ca->sourceAt(2 * S), S);
        // The following clip (and its linked audio) moved right to make room.
        QCOMPARE(d.clipById(b)->start, 8 * S);
        QCOMPARE(d.tracks[2].clips[1].start, 8 * S);
        checkInvariants(d);

        // Splitting a slowed clip maps the cut into source time.
        const QVector<QUuid> right = timeline::splitClips(d, {a}, 3 * S);
        QCOMPARE(right.size(), 2);
        QCOMPARE(d.clipById(right[0])->in, 1500 * kUsPerMs);
        QCOMPARE(d.clipById(right[0])->speed, 0.5);
        checkInvariants(d);

        // Trimming the tail by 2 s of timeline removes 1 s of source.
        const TimeUs out = d.clipById(right[0])->out;
        QVERIFY(timeline::trimClipEnd(d, right[0], d.clipById(right[0])->end() - 2 * S));
        QCOMPARE(d.clipById(right[0])->out, out - S);
        checkInvariants(d);
    }

    void insertEditPushesLaterClips()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S);
        const QUuid a = timeline::placeMedia(d, m, 0, 0, 0, 6 * S);
        const QUuid ins = timeline::insertMedia(d, m, 0, 2 * S, 0, 3 * S);
        QVERIFY(!ins.isNull());
        // a was split at 2 s; its right half now starts after the insert.
        QCOMPARE(d.clipById(a)->end(), 2 * S);
        QCOMPARE(d.clipById(ins)->start, 2 * S);
        QCOMPARE(d.tracks[0].clips.size(), 3);
        QCOMPARE(d.tracks[0].clips[2].start, 5 * S);
        QCOMPARE(d.tracks[0].clips[2].in, 2 * S);
        QCOMPARE(d.duration(), 9 * S);
        checkInvariants(d);
    }

    void rippleDeleteRangesRemovesSilences()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S);
        timeline::placeMedia(d, m, 0, 0, 0, 10 * S);
        const TimeUs removed = timeline::rippleDeleteRanges(d, {{6 * S, 7 * S}, {S, 2 * S}, {1500 * kUsPerMs, 3 * S}});
        QCOMPARE(removed, 3 * S); // [1,3) merged + [6,7)
        QCOMPARE(d.duration(), 7 * S);
        QCOMPARE(d.tracks[0].clips.size(), 3);
        QCOMPARE(d.tracks[2].clips.size(), 3); // linked audio cut identically
        QCOMPARE(d.tracks[0].clips[1].start, S);
        QCOMPARE(d.tracks[0].clips[1].in, 3 * S);
        checkInvariants(d);
    }

    void ripplesMoveMarkers()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 20 * S);
        timeline::placeMedia(d, m, 0, 0, 0, 20 * S);
        timeline::toggleMarker(d, 2 * S, 0, "before");
        timeline::toggleMarker(d, 6 * S, 0, "inside");
        timeline::toggleMarker(d, 12 * S, 0, "after");
        timeline::rippleDeleteRange(d, 5 * S, 8 * S);
        QCOMPARE(d.markers.size(), 3);
        QCOMPARE(d.markers[0].time, 2 * S);
        QCOMPARE(d.markers[1].time, 5 * S); // moved to the cut
        QCOMPARE(d.markers[2].time, 9 * S); // followed its material
        timeline::insertGap(d, 4 * S, S);
        QCOMPARE(d.markers[0].time, 2 * S);
        QCOMPARE(d.markers[1].time, 6 * S);
        QCOMPARE(d.markers[2].time, 10 * S);
    }

    void closeGapsMakesTrackMagnetic()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S, false);
        timeline::placeMedia(d, m, 0, 2 * S, 0, S);
        timeline::placeMedia(d, m, 0, 5 * S, 0, 2 * S);
        QCOMPARE(timeline::closeGaps(d, 0), 2);
        QCOMPARE(d.tracks[0].clips[0].start, 0);
        QCOMPARE(d.tracks[0].clips[1].start, S);
        QCOMPARE(d.duration(), 3 * S);
    }

    void beatMarkers()
    {
        ProjectData d = ProjectData::createDefault();
        // 120 BPM = a beat every 0.5 s.
        QCOMPARE(timeline::addBeatMarkers(d, 120, 0, 0, 4 * S, 4, false), 8);
        QCOMPARE(d.markers[1].time, 500 * kUsPerMs);
        QCOMPARE(d.markers[0].name, QStringLiteral("Bar 1"));
        QCOMPARE(d.markers[4].name, QStringLiteral("Bar 2"));
        // Bars only, offset grid; existing markers aren't duplicated.
        ProjectData e = ProjectData::createDefault();
        QCOMPARE(timeline::addBeatMarkers(e, 120, 250 * kUsPerMs, 0, 5 * S, 4, true), 3);
        QCOMPARE(e.markers[0].time, 250 * kUsPerMs);
        QCOMPARE(timeline::addBeatMarkers(e, 120, 250 * kUsPerMs, 0, 5 * S, 4, true), 0);
    }

    void freezeFrameInsertsStill()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S);
        const QUuid still = addImage(d);
        const QUuid a = timeline::placeMedia(d, m, 0, 0, 0, 4 * S);
        d.clipById(a)->color.saturation = 0.2;
        const QUuid f = timeline::insertFreezeFrame(d, a, 2 * S, still, S);
        QVERIFY(!f.isNull());
        QCOMPARE(d.clipById(f)->start, 2 * S);
        QCOMPARE(d.clipById(f)->duration(), S);
        QCOMPARE(d.clipById(f)->color.saturation, 0.2);
        QCOMPARE(d.duration(), 5 * S);
        QCOMPARE(d.tracks[0].clips.size(), 3);
        QCOMPARE(d.tracks[2].clips[1].start, 3 * S); // audio continues after the freeze
        checkInvariants(d);
    }

    void splitAtSceneTimes()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 10 * S);
        const QUuid a = timeline::placeMedia(d, m, 0, S, 0, 8 * S);
        QCOMPARE(timeline::splitAtTimes(d, a, {3 * S, 6 * S, 20 * S}), 2);
        QCOMPARE(d.tracks[0].clips.size(), 3);
        QCOMPARE(d.tracks[2].clips.size(), 3);
        checkInvariants(d);
    }

    void analysisParsing()
    {
        const QString log = "[silencedetect @ 0x1] silence_start: 1.5\n"
                            "[silencedetect @ 0x1] silence_end: 2.75 | silence_duration: 1.25\n"
                            "[silencedetect @ 0x1] silence_start: 8.2\n";
        const auto s = analysis::parseSilences(log, 10 * S);
        QCOMPARE(s.size(), 2);
        QCOMPARE(s[0].first, 1500 * kUsPerMs);
        QCOMPARE(s[0].second, 2750 * kUsPerMs);
        QCOMPARE(s[1].second, 10 * S); // open silence runs to the end
        const auto padded = analysis::padRanges(s, 100 * kUsPerMs, 500 * kUsPerMs);
        QCOMPARE(padded.size(), 2);
        QCOMPARE(padded[0].first, 1600 * kUsPerMs);

        const auto scenes = analysis::parseScenes("[scdet @ 0x2] lavfi.scd.score: 52.1, lavfi.scd.time: 2.04\n"
                                                  "[scdet @ 0x2] lavfi.scd.score: 40.0, lavfi.scd.time: 5.5\n");
        QCOMPARE(scenes, (QVector<TimeUs>{2040 * kUsPerMs, 5500 * kUsPerMs}));
    }

    void srtRoundTrip()
    {
        const QString srt = QString::fromUtf8("\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,500\r\nHello <i>world</i>\r\n\r\n"
                                              "2\n00:01:00,250 --> 00:01:02,000\nTwo\nlines\n");
        const auto caps = titles::parseSrt(srt);
        QCOMPARE(caps.size(), 2);
        QCOMPARE(caps[0].start, S);
        QCOMPARE(caps[0].end, 2500 * kUsPerMs);
        QCOMPARE(caps[0].text, QStringLiteral("Hello world"));
        QCOMPARE(caps[1].text, QStringLiteral("Two\nlines"));
        const auto back = titles::parseSrt(titles::toSrt(caps));
        QCOMPARE(back.size(), 2);
        QCOMPARE(back[1].start, 60 * S + 250 * kUsPerMs);
        QCOMPARE(back[1].text, caps[1].text);
    }

    void colorMatrixAndMotion()
    {
        ColorAdjust c;
        const ColorMatrix id = colorMatrix(c);
        QCOMPARE(id[0], 1.0);
        QCOMPARE(id[1], 0.0);
        c.mono = MonoMode::BlackWhite;
        const ColorMatrix bw = colorMatrix(c);
        QVERIFY(qFuzzyCompare(bw[0], bw[3]) && qFuzzyCompare(bw[1], bw[4])); // rows equal = grey
        c.mono = MonoMode::None;
        c.temperature = 1;
        QVERIFY(colorMatrix(c)[0] > 1.0 && colorMatrix(c)[8] < 1.0);

        QCOMPARE(motionAt(Motion::ZoomIn, 0).zoom, 1.0);
        QCOMPARE(motionAt(Motion::ZoomIn, 1).zoom, 1.0 + kMotionAmount);
        QVERIFY(motionAt(Motion::PanLeft, 0).dx > 0 && motionAt(Motion::PanLeft, 1).dx < 0);
        QCOMPARE(fitSize(QSize(1000, 1000), QSize(1920, 1080)), QSize(1080, 1080));
    }


    // ---- 50-feature pack: editing ------------------------------------------
    void rippleTrimClosesGap()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 20 * S);
        const QUuid a = timeline::placeMedia(d, m, 0, 0, 0, 6 * S);
        const QUuid b = timeline::placeMedia(d, m, 0, 6 * S, 0, 4 * S);
        QVERIFY(timeline::rippleTrim(d, a, 2 * S, true)); // Q: trim head to 2 s
        QCOMPARE(d.clipById(a)->start, 0);
        QCOMPARE(d.clipById(a)->in, 2 * S);
        QCOMPARE(d.clipById(b)->start, 4 * S);           // pulled left
        QCOMPARE(d.tracks[2].clips[1].start, 4 * S);      // linked audio too
        QVERIFY(timeline::rippleTrim(d, a, 3 * S, false)); // W: trim tail to 3 s
        QCOMPARE(d.clipById(a)->end(), 3 * S);
        QCOMPARE(d.clipById(b)->start, 3 * S);
        checkInvariants(d);
    }

    void editPointsLiftNudge()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 20 * S, false);
        timeline::placeMedia(d, m, 0, 0, 0, 3 * S);
        const QUuid b = timeline::placeMedia(d, m, 0, 5 * S, 0, 2 * S);
        QCOMPARE(timeline::nextEditPoint(d, 0), 3 * S);
        QCOMPARE(timeline::nextEditPoint(d, 3 * S), 5 * S);
        QCOMPARE(timeline::previousEditPoint(d, 6 * S), 5 * S);
        QCOMPARE(timeline::nextEditPoint(d, 7 * S), -1);
        timeline::liftRange(d, S, 2 * S);
        QCOMPARE(d.tracks[0].clips.size(), 3);            // gap left behind
        QCOMPARE(d.duration(), 7 * S);
        QCOMPARE(timeline::nudgeClips(d, {b}, kUsPerSecond / 30), 1);
        QCOMPARE(d.clipById(b)->start, 5 * S + kUsPerSecond / 30);
        checkInvariants(d);
    }

    void rangeProjectAndDisabledClips()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m = addVideo(d, 20 * S);
        const QUuid a = timeline::placeMedia(d, m, 0, 0, 0, 10 * S);
        timeline::toggleMarker(d, 6 * S, 0, "M");
        const ProjectData r = timeline::rangeProject(d, 4 * S, 8 * S);
        QCOMPARE(r.duration(), 4 * S);
        QCOMPARE(r.tracks[0].clips[0].in, 4 * S);
        QCOMPARE(r.markers[0].time, 2 * S);
        d.clipById(a)->enabled = false;
        const ProjectData e = timeline::withoutDisabledClips(d);
        QCOMPARE(e.tracks[0].clips.size(), 0);
        QCOMPARE(e.tracks[2].clips.size(), 1); // linked audio stays (only video was disabled)
    }

    void replaceAndPasteAttributes()
    {
        ProjectData d = ProjectData::createDefault();
        const QUuid m1 = addVideo(d, 20 * S);
        const QUuid m2 = addVideo(d, 3 * S, false);
        const QUuid a = timeline::placeMedia(d, m1, 0, 0, 2 * S, 8 * S);
        Clip src;
        src.color.saturation = 0.3;
        src.transform.scale = 0.5;
        src.fx.grain = 0.4;
        src.gain = 0.5;
        src.fadeIn = S;
        QCOMPARE(timeline::pasteAttributes(d, {a}, src, timeline::AttributeSet()), 1);
        QCOMPARE(d.clipById(a)->color.saturation, 0.3);
        QCOMPARE(d.clipById(a)->fx.grain, 0.4);
        QCOMPARE(d.clipById(a)->transform.scale, 0.5);
        // Replace with shorter, silent media: length clamps, the audio partner goes.
        QVERIFY(timeline::replaceClipMedia(d, a, m2));
        QCOMPARE(d.clipById(a)->mediaId, m2);
        QCOMPARE(d.clipById(a)->in, 0);
        QCOMPARE(d.clipById(a)->duration(), 3 * S);
        QCOMPARE(d.clipById(a)->color.saturation, 0.3); // effects kept
        QCOMPARE(d.tracks[2].clips.size(), 0);
        QCOMPARE(timeline::applyQuickFades(d, {a}, 2 * S), 1);
        QCOMPARE(d.clipById(a)->fadeIn, 1500 * kUsPerMs); // at most half the clip
    }

    void youtubeChaptersAndCollect()
    {
        ProjectData d = ProjectData::createDefault();
        timeline::toggleMarker(d, 65 * S, 0, "Setup");
        timeline::toggleMarker(d, 3725 * S, 0, "Finale");
        QStringList warnings;
        const QString text = timeline::youtubeChapters(d, &warnings);
        QCOMPARE(text, QStringLiteral("0:00 Intro\n1:05 Setup\n1:02:05 Finale"));
        QVERIFY(warnings.isEmpty());
        ProjectData e = ProjectData::createDefault();
        timeline::toggleMarker(e, 0, 0, "Start");
        timeline::toggleMarker(e, 5 * S, 0, "");
        warnings.clear();
        QCOMPARE(timeline::youtubeChapters(e, &warnings), QStringLiteral("0:00 Start\n0:05 Marker 2"));
        QCOMPARE(warnings.size(), 2); // too short + too few

        QTemporaryDir tmp;
        auto touch = [&](const QString &rel) {
            const QString p = tmp.path() + rel;
            QDir().mkpath(QFileInfo(p).absolutePath());
            QFile f(p);
            f.open(QIODevice::WriteOnly);
            f.write("x");
            return p;
        };
        ProjectData c = ProjectData::createDefault();
        const QUuid m1 = addVideo(c, S, false, touch("/a/clip.mp4"));
        addVideo(c, S, false, touch("/b/clip.mp4"));
        addVideo(c, S, false, tmp.path() + "/missing.mp4");
        timeline::placeMedia(c, m1, 0, 0);
        const auto plan = timeline::collectPlan(c, tmp.path() + "/collected");
        QCOMPARE(plan.size(), 2);
        QVERIFY(plan[0].to.endsWith("/clip.mp4"));
        QVERIFY(plan[1].to.endsWith("/clip (2).mp4"));
        QCOMPARE(timeline::unusedMedia(c).size(), 2);
    }

    void timecode()
    {
        QCOMPARE(formatTimecode(0, 30), QStringLiteral("00:00:00:00"));
        QCOMPARE(formatTimecode(61 * S + S / 2, 30), QStringLiteral("00:01:01:15"));
        QCOMPARE(formatDuration(3725 * S), QStringLiteral("1:02:05"));
        QCOMPARE(snapToFrame(S / 30 + 100, 30), S / 30);
    }
};

QTEST_GUILESS_MAIN(TestTimeline)
#include "test_timeline.moc"

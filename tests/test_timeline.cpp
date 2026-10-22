#include "core/timelineops.h"
#include "testutil.h"

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

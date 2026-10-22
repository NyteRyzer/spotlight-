#include "core/timelineops.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <limits>

namespace spotlight::timeline {

namespace {

bool isStillClip(const ProjectData &d, const Clip &c)
{
    const MediaItem *m = d.mediaById(c.mediaId);
    return m && m->isStill();
}

TimeUs mediaLimit(const ProjectData &d, const Clip &c)
{
    const MediaItem *m = d.mediaById(c.mediaId);
    if (!m || m->isStill() || m->duration <= 0)
        return std::numeric_limits<TimeUs>::max();
    return m->duration;
}

// Start of the previous clip's end / next clip's start relative to clip c.
TimeUs prevEnd(const Track &t, const Clip &c)
{
    TimeUs e = 0;
    for (const auto &o : t.clips) {
        if (o.id != c.id && o.end() <= c.start)
            e = std::max(e, o.end());
    }
    return e;
}

TimeUs nextStart(const Track &t, const Clip &c)
{
    TimeUs s = std::numeric_limits<TimeUs>::max();
    for (const auto &o : t.clips) {
        if (o.id != c.id && o.start >= c.end())
            s = std::min(s, o.start);
    }
    return s;
}

int matchingAudioTrack(ProjectData &d, int videoTrackIndex)
{
    const QVector<int> videos = d.videoTrackIndices();
    const int ordinal = std::max(0, int(videos.indexOf(videoTrackIndex)));
    QVector<int> audios = d.audioTrackIndices();
    while (audios.size() <= ordinal) {
        d.addTrack(TrackType::Audio);
        audios = d.audioTrackIndices();
    }
    return audios[ordinal];
}

} // namespace

QVector<QUuid> groupOf(const ProjectData &d, const QUuid &clipId)
{
    const Clip *c = d.clipById(clipId);
    if (!c)
        return {};
    if (c->linkId.isNull())
        return {clipId};
    QVector<QUuid> r{clipId};
    for (const auto &t : d.tracks) {
        for (const auto &o : t.clips) {
            if (o.linkId == c->linkId && o.id != clipId)
                r.append(o.id);
        }
    }
    return r;
}

int clipIndexAt(const Track &track, TimeUs t)
{
    for (int i = 0; i < track.clips.size(); ++i) {
        if (track.clips[i].contains(t))
            return i;
    }
    return -1;
}

void clearRange(Track &track, TimeUs from, TimeUs to)
{
    if (to <= from)
        return;
    QVector<Clip> result;
    result.reserve(track.clips.size() + 1);
    for (const Clip &c : track.clips) {
        if (c.end() <= from || c.start >= to) {
            result.append(c);
            continue;
        }
        // Left remainder.
        if (c.start < from) {
            Clip left = c;
            left.out = c.in + (from - c.start);
            left.fadeOut = 0;
            if (left.duration() >= kMinClipDuration)
                result.append(left);
        }
        // Right remainder.
        if (c.end() > to) {
            Clip right = c;
            const TimeUs cut = to - c.start;
            right.start = to;
            right.in = c.in + cut;
            right.fadeIn = 0;
            if (c.start < from) {
                // The clip was split in two; the right part is a new clip.
                right.id = QUuid::createUuid();
                right.linkId = QUuid();
            }
            if (right.duration() >= kMinClipDuration)
                result.append(right);
        }
    }
    track.clips = result;
    std::sort(track.clips.begin(), track.clips.end(), [](const Clip &a, const Clip &b) { return a.start < b.start; });
}

void insertSorted(Track &track, const Clip &clip)
{
    auto it = std::lower_bound(track.clips.begin(), track.clips.end(), clip,
                               [](const Clip &a, const Clip &b) { return a.start < b.start; });
    track.clips.insert(it, clip);
}

QUuid placeMedia(ProjectData &d, const QUuid &mediaId, int trackIndex, TimeUs start, TimeUs in, TimeUs out)
{
    const MediaItem *mp = d.mediaById(mediaId);
    if (!mp)
        return {};
    const MediaItem m = *mp;
    start = std::max<TimeUs>(0, start);

    if (m.isStill()) {
        in = 0;
        if (out <= 0)
            out = kDefaultStillDuration;
    } else {
        if (m.duration <= 0)
            return {};
        in = std::clamp<TimeUs>(in, 0, m.duration);
        out = out < 0 ? m.duration : std::clamp<TimeUs>(out, 0, m.duration);
    }
    if (out - in < kMinClipDuration)
        return {};

    const bool wantsVideo = m.hasVideo || m.isStill();
    int primaryTrack = -1;
    if (wantsVideo) {
        if (trackIndex >= 0 && trackIndex < d.tracks.size() && d.tracks[trackIndex].isVideo())
            primaryTrack = trackIndex;
        else {
            QVector<int> v = d.videoTrackIndices();
            primaryTrack = v.isEmpty() ? d.addTrack(TrackType::Video) : v.first();
        }
    } else {
        if (trackIndex >= 0 && trackIndex < d.tracks.size() && d.tracks[trackIndex].isAudio())
            primaryTrack = trackIndex;
        else {
            QVector<int> a = d.audioTrackIndices();
            primaryTrack = a.isEmpty() ? d.addTrack(TrackType::Audio) : a.first();
        }
    }

    int audioTrack = -1;
    if (wantsVideo && m.hasAudio) {
        // matchingAudioTrack may append tracks; audio tracks come after video
        // tracks so primaryTrack's index is unaffected.
        audioTrack = matchingAudioTrack(d, primaryTrack);
    }

    if (d.tracks[primaryTrack].locked || (audioTrack >= 0 && d.tracks[audioTrack].locked))
        return {};

    Clip c;
    c.id = QUuid::createUuid();
    c.mediaId = mediaId;
    c.start = start;
    c.in = in;
    c.out = out;

    if (audioTrack >= 0) {
        c.linkId = QUuid::createUuid();
        Clip a = c;
        a.id = QUuid::createUuid();
        clearRange(d.tracks[audioTrack], a.start, a.end());
        insertSorted(d.tracks[audioTrack], a);
    }
    clearRange(d.tracks[primaryTrack], c.start, c.end());
    insertSorted(d.tracks[primaryTrack], c);
    return c.id;
}

bool moveClip(ProjectData &d, const QUuid &clipId, TimeUs newStart, int newTrackIndex)
{
    int primaryTrack = -1;
    const Clip *primary = d.clipById(clipId, &primaryTrack);
    if (!primary)
        return false;

    const QVector<QUuid> group = groupOf(d, clipId);
    TimeUs delta = newStart - primary->start;

    // Never move any member before 0.
    TimeUs minStart = std::numeric_limits<TimeUs>::max();
    for (const auto &id : group) {
        int ti = -1;
        const Clip *c = d.clipById(id, &ti);
        if (d.tracks[ti].locked)
            return false;
        minStart = std::min(minStart, c->start);
    }
    delta = std::max(delta, -minStart);

    int destTrack = primaryTrack;
    if (newTrackIndex >= 0 && newTrackIndex < d.tracks.size()
        && d.tracks[newTrackIndex].type == d.tracks[primaryTrack].type && !d.tracks[newTrackIndex].locked)
        destTrack = newTrackIndex;

    if (delta == 0 && destTrack == primaryTrack)
        return false;

    // Lift every member off its track, then overwrite at the destination.
    struct Lifted {
        Clip clip;
        int track;
    };
    QVector<Lifted> lifted;
    for (const auto &id : group) {
        int ti = -1;
        Clip *c = d.clipById(id, &ti);
        Lifted l{*c, ti};
        l.clip.start += delta;
        if (id == clipId)
            l.track = destTrack;
        lifted.append(l);
        Track &t = d.tracks[ti];
        t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(), [&](const Clip &x) { return x.id == id; }),
                      t.clips.end());
    }
    for (const auto &l : lifted) {
        clearRange(d.tracks[l.track], l.clip.start, l.clip.end());
        insertSorted(d.tracks[l.track], l.clip);
    }
    return true;
}

bool trimClipStart(ProjectData &d, const QUuid &clipId, TimeUs newStart)
{
    const Clip *primary = d.clipById(clipId);
    if (!primary)
        return false;
    TimeUs delta = newStart - primary->start;
    TimeUs lo = std::numeric_limits<TimeUs>::min();
    TimeUs hi = std::numeric_limits<TimeUs>::max();

    const QVector<QUuid> group = groupOf(d, clipId);
    for (const auto &id : group) {
        int ti = -1;
        const Clip *c = d.clipById(id, &ti);
        if (d.tracks[ti].locked)
            return false;
        hi = std::min(hi, c->duration() - kMinClipDuration);
        lo = std::max(lo, prevEnd(d.tracks[ti], *c) - c->start);
        if (!isStillClip(d, *c))
            lo = std::max(lo, -c->in);
    }
    if (lo > hi)
        return false;
    delta = std::clamp(delta, lo, hi);
    if (delta == 0)
        return false;

    for (const auto &id : group) {
        Clip *c = d.clipById(id);
        c->start += delta;
        if (isStillClip(d, *c))
            c->out -= delta; // stills have no source bounds; keep in at 0
        else
            c->in += delta;
        c->fadeIn = std::min(c->fadeIn, c->duration());
    }
    return true;
}

bool trimClipEnd(ProjectData &d, const QUuid &clipId, TimeUs newEnd)
{
    const Clip *primary = d.clipById(clipId);
    if (!primary)
        return false;
    TimeUs delta = newEnd - primary->end();
    TimeUs lo = std::numeric_limits<TimeUs>::min();
    TimeUs hi = std::numeric_limits<TimeUs>::max();

    const QVector<QUuid> group = groupOf(d, clipId);
    for (const auto &id : group) {
        int ti = -1;
        const Clip *c = d.clipById(id, &ti);
        if (d.tracks[ti].locked)
            return false;
        lo = std::max(lo, kMinClipDuration - c->duration());
        const TimeUs ns = nextStart(d.tracks[ti], *c);
        if (ns != std::numeric_limits<TimeUs>::max())
            hi = std::min(hi, ns - c->end());
        const TimeUs limit = mediaLimit(d, *c);
        if (limit != std::numeric_limits<TimeUs>::max())
            hi = std::min(hi, limit - c->out);
    }
    if (lo > hi)
        return false;
    delta = std::clamp(delta, lo, hi);
    if (delta == 0)
        return false;

    for (const auto &id : group) {
        Clip *c = d.clipById(id);
        c->out += delta;
        c->fadeOut = std::min(c->fadeOut, c->duration());
    }
    return true;
}

QVector<QUuid> splitClips(ProjectData &d, const QVector<QUuid> &clipIds, TimeUs at)
{
    // Expand to linked groups.
    QVector<QUuid> all;
    for (const auto &id : clipIds) {
        for (const auto &g : groupOf(d, id)) {
            if (!all.contains(g))
                all.append(g);
        }
    }

    QHash<QUuid, QUuid> newLinks; // old link -> link for right-hand pieces
    QVector<QUuid> created;
    for (const auto &id : all) {
        int ti = -1;
        Clip *c = d.clipById(id, &ti);
        if (!c || d.tracks[ti].locked)
            continue;
        if (at - c->start < kMinClipDuration || c->end() - at < kMinClipDuration)
            continue;
        Clip right = *c;
        right.id = QUuid::createUuid();
        right.start = at;
        right.in = c->in + (at - c->start);
        right.fadeIn = 0;
        if (!c->linkId.isNull()) {
            if (!newLinks.contains(c->linkId))
                newLinks.insert(c->linkId, QUuid::createUuid());
            right.linkId = newLinks.value(c->linkId);
        }
        c->out = right.in;
        c->fadeOut = 0;
        insertSorted(d.tracks[ti], right);
        created.append(right.id);
    }
    return created;
}

QVector<QUuid> splitAll(ProjectData &d, TimeUs at)
{
    QVector<QUuid> ids;
    for (const auto &t : d.tracks) {
        if (t.locked)
            continue;
        const int i = clipIndexAt(t, at);
        if (i >= 0)
            ids.append(t.clips[i].id);
    }
    return splitClips(d, ids, at);
}

int removeClips(ProjectData &d, const QVector<QUuid> &clipIds, bool ripple)
{
    QVector<QUuid> all;
    for (const auto &id : clipIds) {
        for (const auto &g : groupOf(d, id)) {
            if (!all.contains(g))
                all.append(g);
        }
    }

    struct Removed {
        int track;
        TimeUs start;
        TimeUs duration;
    };
    QVector<Removed> removed;
    for (const auto &id : all) {
        int ti = -1;
        const Clip *c = d.clipById(id, &ti);
        if (!c || d.tracks[ti].locked)
            continue;
        removed.append({ti, c->start, c->duration()});
        Track &t = d.tracks[ti];
        t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(), [&](const Clip &x) { return x.id == id; }),
                      t.clips.end());
    }

    if (ripple) {
        std::sort(removed.begin(), removed.end(), [](const Removed &a, const Removed &b) { return a.start > b.start; });
        for (const auto &r : removed) {
            for (auto &c : d.tracks[r.track].clips) {
                if (c.start >= r.start)
                    c.start = std::max<TimeUs>(0, c.start - r.duration);
            }
        }
    }
    return removed.size();
}

void removeMedia(ProjectData &d, const QUuid &mediaId)
{
    for (auto &t : d.tracks) {
        t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(),
                                     [&](const Clip &c) { return c.mediaId == mediaId; }),
                      t.clips.end());
    }
    d.media.erase(std::remove_if(d.media.begin(), d.media.end(), [&](const MediaItem &m) { return m.id == mediaId; }),
                  d.media.end());
}

const Clip *topVideoClipAt(const ProjectData &d, TimeUs t, int *trackIndex)
{
    const QVector<int> videos = d.videoTrackIndices();
    for (int i = videos.size() - 1; i >= 0; --i) {
        const Track &track = d.tracks[videos[i]];
        if (track.muted)
            continue;
        const int ci = clipIndexAt(track, t);
        if (ci >= 0) {
            if (trackIndex)
                *trackIndex = videos[i];
            return &track.clips[ci];
        }
    }
    return nullptr;
}

QVector<VideoSegment> videoSegments(const ProjectData &d)
{
    const TimeUs total = d.duration();
    QVector<VideoSegment> segs;
    if (total <= 0)
        return segs;

    QVector<TimeUs> bounds{0, total};
    for (int ti : d.videoTrackIndices()) {
        if (d.tracks[ti].muted)
            continue;
        for (const auto &c : d.tracks[ti].clips) {
            bounds.append(c.start);
            bounds.append(c.end());
        }
    }
    std::sort(bounds.begin(), bounds.end());
    bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());

    for (int i = 0; i + 1 < bounds.size(); ++i) {
        const TimeUs a = bounds[i];
        const TimeUs b = bounds[i + 1];
        if (b <= a || a >= total)
            continue;
        int ti = -1;
        const Clip *c = topVideoClipAt(d, a, &ti);
        VideoSegment s{a, std::min(b, total), c ? c->id : QUuid(), c ? ti : -1};
        if (!segs.isEmpty() && segs.last().clipId == s.clipId && segs.last().end == s.start)
            segs.last().end = s.end;
        else
            segs.append(s);
    }
    return segs;
}

bool isTrackAudible(const ProjectData &d, int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= d.tracks.size())
        return false;
    const Track &t = d.tracks[trackIndex];
    if (!t.isAudio() || t.muted)
        return false;
    bool anySolo = false;
    for (const auto &o : d.tracks)
        anySolo = anySolo || (o.isAudio() && o.solo);
    return !anySolo || t.solo;
}

TimeUs snapTime(const ProjectData &d, TimeUs t, TimeUs threshold, const QSet<QUuid> &exclude,
                const QVector<TimeUs> &extraPoints)
{
    TimeUs best = t;
    TimeUs bestDist = threshold + 1;
    auto consider = [&](TimeUs p) {
        const TimeUs dist = std::abs(p - t);
        if (dist <= threshold && dist < bestDist) {
            best = p;
            bestDist = dist;
        }
    };
    consider(0);
    for (TimeUs p : extraPoints)
        consider(p);
    for (const auto &track : d.tracks) {
        for (const auto &c : track.clips) {
            if (exclude.contains(c.id))
                continue;
            consider(c.start);
            consider(c.end());
        }
    }
    return best;
}

double fadeEnvelope(const Clip &c, TimeUs t)
{
    double g = 1.0;
    const TimeUs local = t - c.start;
    if (c.fadeIn > 0 && local < c.fadeIn)
        g = std::min(g, std::max(0.0, double(local) / double(c.fadeIn)));
    const TimeUs remaining = c.end() - t;
    if (c.fadeOut > 0 && remaining < c.fadeOut)
        g = std::min(g, std::max(0.0, double(remaining) / double(c.fadeOut)));
    return g;
}

QVector<ClipboardEntry> copyClips(const ProjectData &d, const QVector<QUuid> &clipIds)
{
    QVector<QUuid> all;
    for (const auto &id : clipIds) {
        for (const auto &g : groupOf(d, id)) {
            if (!all.contains(g))
                all.append(g);
        }
    }
    QVector<ClipboardEntry> out;
    TimeUs origin = std::numeric_limits<TimeUs>::max();
    for (const auto &id : all) {
        int ti = -1;
        if (const Clip *c = d.clipById(id, &ti)) {
            out.append({d.tracks[ti].id, ti, *c});
            origin = std::min(origin, c->start);
        }
    }
    for (auto &e : out)
        e.clip.start -= origin;
    return out;
}

QVector<QUuid> pasteClips(ProjectData &d, const QVector<ClipboardEntry> &entries, TimeUs at)
{
    QHash<QUuid, QUuid> newLinks;
    QVector<QUuid> created;
    at = std::max<TimeUs>(0, at);
    for (const auto &e : entries) {
        if (!d.mediaById(e.clip.mediaId))
            continue; // media was removed from the project since copying
        int ti = d.trackIndex(e.trackId);
        if (ti < 0)
            ti = (e.trackIndex >= 0 && e.trackIndex < d.tracks.size()) ? e.trackIndex : -1;
        if (ti < 0 || d.tracks[ti].locked)
            continue;
        Clip c = e.clip;
        c.id = QUuid::createUuid();
        c.start = at + e.clip.start;
        if (!c.linkId.isNull()) {
            if (!newLinks.contains(c.linkId))
                newLinks.insert(c.linkId, QUuid::createUuid());
            c.linkId = newLinks.value(c.linkId);
        }
        clearRange(d.tracks[ti], c.start, c.end());
        insertSorted(d.tracks[ti], c);
        created.append(c.id);
    }
    return created;
}

TimeUs selectionEnd(const ProjectData &d, const QVector<QUuid> &clipIds)
{
    TimeUs end = 0;
    for (const auto &id : clipIds) {
        for (const auto &g : groupOf(d, id)) {
            if (const Clip *c = d.clipById(g))
                end = std::max(end, c->end());
        }
    }
    return end;
}

bool toggleMarker(ProjectData &d, TimeUs t, TimeUs tolerance, const QString &name)
{
    for (int i = 0; i < d.markers.size(); ++i) {
        if (std::abs(d.markers[i].time - t) <= tolerance) {
            d.markers.removeAt(i);
            return false;
        }
    }
    Marker m{QUuid::createUuid(), std::max<TimeUs>(0, t), name};
    if (m.name.isEmpty())
        m.name = QStringLiteral("Marker %1").arg(d.markers.size() + 1);
    auto it = std::lower_bound(d.markers.begin(), d.markers.end(), m,
                               [](const Marker &a, const Marker &b) { return a.time < b.time; });
    d.markers.insert(it, m);
    return true;
}

TimeUs nextMarker(const ProjectData &d, TimeUs t)
{
    for (const auto &m : d.markers) {
        if (m.time > t)
            return m.time;
    }
    return -1;
}

TimeUs previousMarker(const ProjectData &d, TimeUs t)
{
    TimeUs best = -1;
    for (const auto &m : d.markers) {
        if (m.time < t)
            best = m.time;
    }
    return best;
}

int relinkMissingMedia(ProjectData &d, const QString &folder)
{
    QHash<QString, QVector<int>> wanted; // lower-case file name -> media indices
    for (int i = 0; i < d.media.size(); ++i) {
        if (!QFileInfo::exists(d.media[i].path))
            wanted[QFileInfo(d.media[i].path).fileName().toLower()].append(i);
    }
    if (wanted.isEmpty())
        return 0;
    int relinked = 0;
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && !wanted.isEmpty()) {
        const QString path = it.next();
        const QString key = it.fileName().toLower();
        auto w = wanted.find(key);
        if (w == wanted.end())
            continue;
        for (int i : *w) {
            d.media[i].path = QFileInfo(path).absoluteFilePath();
            ++relinked;
        }
        wanted.erase(w);
    }
    return relinked;
}

} // namespace spotlight::timeline

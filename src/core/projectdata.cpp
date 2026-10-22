#include "core/projectdata.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace spotlight {

namespace {

QString uuidStr(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

QUuid uuidOrNew(const QJsonValue &v)
{
    const QUuid id = QUuid::fromString(v.toString());
    return id.isNull() ? QUuid::createUuid() : id;
}

QJsonObject clipToJson(const Clip &c)
{
    QJsonObject o;
    o["id"] = uuidStr(c.id);
    o["media"] = uuidStr(c.mediaId);
    if (!c.linkId.isNull())
        o["link"] = uuidStr(c.linkId);
    o["start"] = double(c.start);
    o["in"] = double(c.in);
    o["out"] = double(c.out);
    o["gain"] = c.gain;
    o["fadeIn"] = double(c.fadeIn);
    o["fadeOut"] = double(c.fadeOut);
    if (!c.color.isIdentity()) {
        QJsonObject col;
        col["brightness"] = c.color.brightness;
        col["contrast"] = c.color.contrast;
        col["saturation"] = c.color.saturation;
        o["color"] = col;
    }
    return o;
}

Clip clipFromJson(const QJsonObject &o)
{
    Clip c;
    c.id = uuidOrNew(o["id"]);
    c.mediaId = QUuid::fromString(o["media"].toString());
    c.linkId = QUuid::fromString(o["link"].toString());
    c.start = TimeUs(o["start"].toDouble());
    c.in = TimeUs(o["in"].toDouble());
    c.out = TimeUs(o["out"].toDouble());
    c.gain = o["gain"].toDouble(1.0);
    c.fadeIn = TimeUs(o["fadeIn"].toDouble());
    c.fadeOut = TimeUs(o["fadeOut"].toDouble());
    const QJsonObject col = o["color"].toObject();
    c.color.brightness = col["brightness"].toDouble(0.0);
    c.color.contrast = col["contrast"].toDouble(1.0);
    c.color.saturation = col["saturation"].toDouble(1.0);
    return c;
}

QJsonObject fxToJson(const AudioFx &fx)
{
    QJsonObject o;
    o["eq"] = fx.eqEnabled;
    o["eqLow"] = fx.lowGainDb;
    o["eqMid"] = fx.midGainDb;
    o["eqHigh"] = fx.highGainDb;
    o["comp"] = fx.compressorEnabled;
    o["compThreshold"] = fx.thresholdDb;
    o["compRatio"] = fx.ratio;
    o["dialogueCleanup"] = fx.dialogueCleanup;
    return o;
}

AudioFx fxFromJson(const QJsonObject &o)
{
    AudioFx fx;
    fx.eqEnabled = o["eq"].toBool();
    fx.lowGainDb = o["eqLow"].toDouble();
    fx.midGainDb = o["eqMid"].toDouble();
    fx.highGainDb = o["eqHigh"].toDouble();
    fx.compressorEnabled = o["comp"].toBool();
    fx.thresholdDb = o["compThreshold"].toDouble(-18.0);
    fx.ratio = o["compRatio"].toDouble(3.0);
    fx.dialogueCleanup = o["dialogueCleanup"].toBool();
    return fx;
}

} // namespace

const MediaItem *ProjectData::mediaById(const QUuid &id) const
{
    for (const auto &m : media) {
        if (m.id == id)
            return &m;
    }
    return nullptr;
}

int ProjectData::trackIndex(const QUuid &trackId) const
{
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks[i].id == trackId)
            return i;
    }
    return -1;
}

Clip *ProjectData::clipById(const QUuid &clipId, int *trackIdx)
{
    for (int t = 0; t < tracks.size(); ++t) {
        for (auto &c : tracks[t].clips) {
            if (c.id == clipId) {
                if (trackIdx)
                    *trackIdx = t;
                return &c;
            }
        }
    }
    return nullptr;
}

const Clip *ProjectData::clipById(const QUuid &clipId, int *trackIdx) const
{
    return const_cast<ProjectData *>(this)->clipById(clipId, trackIdx);
}

QVector<int> ProjectData::videoTrackIndices() const
{
    QVector<int> r;
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks[i].isVideo())
            r.append(i);
    }
    return r;
}

QVector<int> ProjectData::audioTrackIndices() const
{
    QVector<int> r;
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks[i].isAudio())
            r.append(i);
    }
    return r;
}

TimeUs ProjectData::duration() const
{
    TimeUs d = 0;
    for (const auto &t : tracks) {
        for (const auto &c : t.clips)
            d = std::max(d, c.end());
    }
    return d;
}

int ProjectData::addTrack(TrackType type, const QString &name)
{
    Track t;
    t.id = QUuid::createUuid();
    t.type = type;
    if (type == TrackType::Video) {
        const int n = videoTrackIndices().size();
        t.name = name.isEmpty() ? QStringLiteral("V%1").arg(n + 1) : name;
        tracks.insert(n, t);
        return n;
    }
    const int n = audioTrackIndices().size();
    t.name = name.isEmpty() ? QStringLiteral("A%1").arg(n + 1) : name;
    tracks.append(t);
    return tracks.size() - 1;
}

QJsonObject ProjectData::toJson(const QString &projectDir) const
{
    QJsonObject root;
    root["format"] = QStringLiteral("spotlight-project");
    root["version"] = kFormatVersion;
    root["id"] = uuidStr(id);
    root["name"] = name;

    QJsonObject seq;
    seq["width"] = sequence.width;
    seq["height"] = sequence.height;
    seq["frameRate"] = sequence.frameRate;
    seq["sampleRate"] = sequence.sampleRate;
    root["sequence"] = seq;

    const QDir dir(projectDir);
    QJsonArray mediaArr;
    for (const auto &m : media) {
        QJsonObject o;
        o["id"] = uuidStr(m.id);
        o["path"] = m.path;
        if (!projectDir.isEmpty())
            o["relativePath"] = dir.relativeFilePath(m.path);
        o["name"] = m.name;
        o["kind"] = mediaKindToString(m.kind);
        o["duration"] = double(m.duration);
        o["width"] = m.resolution.width();
        o["height"] = m.resolution.height();
        o["frameRate"] = m.frameRate;
        o["hasVideo"] = m.hasVideo;
        o["hasAudio"] = m.hasAudio;
        mediaArr.append(o);
    }
    root["media"] = mediaArr;

    QJsonArray trackArr;
    for (const auto &t : tracks) {
        QJsonObject o;
        o["id"] = uuidStr(t.id);
        o["type"] = t.isVideo() ? QStringLiteral("video") : QStringLiteral("audio");
        o["name"] = t.name;
        o["muted"] = t.muted;
        o["solo"] = t.solo;
        o["locked"] = t.locked;
        o["volume"] = t.volume;
        o["pan"] = t.pan;
        if (t.isAudio())
            o["fx"] = fxToJson(t.fx);
        QJsonArray clips;
        for (const auto &c : t.clips)
            clips.append(clipToJson(c));
        o["clips"] = clips;
        trackArr.append(o);
    }
    root["tracks"] = trackArr;
    root["design"] = design.toJson();
    QJsonArray markerArr;
    for (const auto &m : markers) {
        QJsonObject o;
        o["id"] = uuidStr(m.id);
        o["time"] = double(m.time);
        o["name"] = m.name;
        markerArr.append(o);
    }
    root["markers"] = markerArr;
    return root;
}

bool ProjectData::fromJson(const QJsonObject &root, ProjectData *out, QString *error, const QString &projectDir)
{
    if (root["format"].toString() != QLatin1String("spotlight-project")) {
        if (error)
            *error = QStringLiteral("Not a SPOTLIGHT! project file.");
        return false;
    }
    const int version = root["version"].toInt();
    if (version < 1 || version > kFormatVersion) {
        if (error)
            *error = QStringLiteral("Unsupported project version %1.").arg(version);
        return false;
    }

    ProjectData d;
    d.id = uuidOrNew(root["id"]);
    d.name = root["name"].toString(d.name);

    const QJsonObject seq = root["sequence"].toObject();
    d.sequence.width = seq["width"].toInt(1920);
    d.sequence.height = seq["height"].toInt(1080);
    d.sequence.frameRate = seq["frameRate"].toDouble(30.0);
    d.sequence.sampleRate = seq["sampleRate"].toInt(48000);

    const QDir dir(projectDir);
    for (const auto &v : root["media"].toArray()) {
        const QJsonObject o = v.toObject();
        MediaItem m;
        m.id = uuidOrNew(o["id"]);
        m.path = o["path"].toString();
        // If the absolute path is gone but the file sits at the same place
        // relative to the project, relink automatically.
        const QString rel = o["relativePath"].toString();
        if (!projectDir.isEmpty() && !rel.isEmpty() && !QFileInfo::exists(m.path)) {
            const QString candidate = QDir::cleanPath(dir.absoluteFilePath(rel));
            if (QFileInfo::exists(candidate))
                m.path = candidate;
        }
        m.name = o["name"].toString(QFileInfo(m.path).fileName());
        m.kind = mediaKindFromString(o["kind"].toString());
        m.duration = TimeUs(o["duration"].toDouble());
        m.resolution = QSize(o["width"].toInt(), o["height"].toInt());
        m.frameRate = o["frameRate"].toDouble();
        m.hasVideo = o["hasVideo"].toBool();
        m.hasAudio = o["hasAudio"].toBool();
        d.media.append(m);
    }

    for (const auto &v : root["tracks"].toArray()) {
        const QJsonObject o = v.toObject();
        Track t;
        t.id = uuidOrNew(o["id"]);
        t.type = o["type"].toString() == QLatin1String("audio") ? TrackType::Audio : TrackType::Video;
        t.name = o["name"].toString();
        t.muted = o["muted"].toBool();
        t.solo = o["solo"].toBool();
        t.locked = o["locked"].toBool();
        t.volume = o["volume"].toDouble(1.0);
        t.pan = o["pan"].toDouble(0.0);
        t.fx = fxFromJson(o["fx"].toObject());
        for (const auto &cv : o["clips"].toArray())
            t.clips.append(clipFromJson(cv.toObject()));
        std::sort(t.clips.begin(), t.clips.end(), [](const Clip &a, const Clip &b) { return a.start < b.start; });
        d.tracks.append(t);
    }
    // Keep the invariant: video tracks first, then audio tracks.
    std::stable_sort(d.tracks.begin(), d.tracks.end(),
                     [](const Track &a, const Track &b) { return a.isVideo() && b.isAudio(); });

    d.design = DesignDocument::fromJson(root["design"].toObject());
    for (const auto &v : root["markers"].toArray()) {
        const QJsonObject o = v.toObject();
        d.markers.append({uuidOrNew(o["id"]), TimeUs(o["time"].toDouble()), o["name"].toString()});
    }
    std::sort(d.markers.begin(), d.markers.end(), [](const Marker &a, const Marker &b) { return a.time < b.time; });
    *out = d;
    return true;
}

ProjectData ProjectData::createDefault()
{
    ProjectData d;
    d.addTrack(TrackType::Video);
    d.addTrack(TrackType::Video);
    d.addTrack(TrackType::Audio);
    d.addTrack(TrackType::Audio);
    return d;
}

bool saveProjectFile(const ProjectData &data, const QString &path, QString *error)
{
    const QString dir = QFileInfo(path).absolutePath();
    const QByteArray bytes = QJsonDocument(data.toJson(dir)).toJson(QJsonDocument::Indented);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(bytes);
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool loadProjectFile(const QString &path, ProjectData *out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("Corrupt project file: %1").arg(pe.errorString());
        return false;
    }
    return ProjectData::fromJson(doc.object(), out, error, QFileInfo(path).absolutePath());
}

} // namespace spotlight

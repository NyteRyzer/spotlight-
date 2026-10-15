#include "core/exportpreset.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace spotlight {

QJsonObject ExportPreset::toJson() const
{
    QJsonObject o;
    o["name"] = name;
    o["container"] = container;
    o["videoCodec"] = videoCodec;
    o["width"] = width;
    o["height"] = height;
    o["frameRate"] = frameRate;
    o["crf"] = crf;
    o["speedPreset"] = speedPreset;
    o["audioCodec"] = audioCodec;
    o["audioBitrate"] = audioBitrateKbps;
    o["sampleRate"] = sampleRate;
    return o;
}

ExportPreset ExportPreset::fromJson(const QJsonObject &o)
{
    ExportPreset p;
    p.name = o["name"].toString(QStringLiteral("Custom"));
    p.container = o["container"].toString(p.container);
    p.videoCodec = o["videoCodec"].toString(p.videoCodec);
    p.width = o["width"].toInt(p.width);
    p.height = o["height"].toInt(p.height);
    p.frameRate = o["frameRate"].toDouble(p.frameRate);
    p.crf = o["crf"].toInt(p.crf);
    p.speedPreset = o["speedPreset"].toString(p.speedPreset);
    p.audioCodec = o["audioCodec"].toString(p.audioCodec);
    p.audioBitrateKbps = o["audioBitrate"].toInt(p.audioBitrateKbps);
    p.sampleRate = o["sampleRate"].toInt(p.sampleRate);
    return p;
}

QVector<ExportPreset> builtInExportPresets()
{
    QVector<ExportPreset> r;
    auto video = [&](const QString &name, int w, int h, double fps, int crf) {
        ExportPreset p;
        p.name = name;
        p.width = w;
        p.height = h;
        p.frameRate = fps;
        p.crf = crf;
        p.builtIn = true;
        r.append(p);
        return &r.last();
    };
    video(QStringLiteral("YouTube 1080p (H.264)"), 1920, 1080, 30, 18);
    video(QStringLiteral("YouTube 1080p60 (H.264)"), 1920, 1080, 60, 18);
    {
        ExportPreset *p = video(QStringLiteral("YouTube 4K (H.264)"), 3840, 2160, 30, 18);
        p->audioBitrateKbps = 320;
    }
    video(QStringLiteral("YouTube Shorts / TikTok / Reels (1080×1920)"), 1080, 1920, 30, 20);
    video(QStringLiteral("720p (small file)"), 1280, 720, 30, 23);
    {
        ExportPreset *p = video(QStringLiteral("Square 1080 (Instagram)"), 1080, 1080, 30, 20);
        p->audioBitrateKbps = 160;
    }
    {
        ExportPreset *p = video(QStringLiteral("Archive 1080p (H.265, MKV)"), 1920, 1080, 30, 20);
        p->videoCodec = QStringLiteral("libx265");
        p->container = QStringLiteral("mkv");
        p->speedPreset = QStringLiteral("slow");
    }
    {
        ExportPreset *p = video(QStringLiteral("Web 1080p (VP9, WebM)"), 1920, 1080, 30, 31);
        p->videoCodec = QStringLiteral("libvpx-vp9");
        p->container = QStringLiteral("webm");
        p->audioCodec = QStringLiteral("libopus");
        p->audioBitrateKbps = 160;
        p->speedPreset.clear();
    }

    auto audio = [&](const QString &name, const QString &container, const QString &codec, int kbps) {
        ExportPreset p;
        p.name = name;
        p.container = container;
        p.videoCodec.clear();
        p.audioCodec = codec;
        p.audioBitrateKbps = kbps;
        p.speedPreset.clear();
        p.builtIn = true;
        r.append(p);
    };
    audio(QStringLiteral("Podcast / Music MP3 (320 kbps)"), QStringLiteral("mp3"), QStringLiteral("libmp3lame"), 320);
    audio(QStringLiteral("Audio AAC (M4A, 256 kbps)"), QStringLiteral("m4a"), QStringLiteral("aac"), 256);
    audio(QStringLiteral("Audio WAV (24-bit master)"), QStringLiteral("wav"), QStringLiteral("pcm_s24le"), 0);
    return r;
}

QVector<ExportPreset> loadUserExportPresets(const QString &file)
{
    QVector<ExportPreset> r;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return r;
    for (const auto &v : QJsonDocument::fromJson(f.readAll()).array())
        r.append(ExportPreset::fromJson(v.toObject()));
    return r;
}

bool saveUserExportPresets(const QString &file, const QVector<ExportPreset> &presets)
{
    QJsonArray arr;
    for (const auto &p : presets) {
        if (!p.builtIn)
            arr.append(p.toJson());
    }
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(arr).toJson());
    return f.commit();
}

} // namespace spotlight

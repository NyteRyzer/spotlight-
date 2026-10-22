#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace spotlight {

// A named set of output settings for rendering the timeline.
struct ExportPreset {
    QString name;
    QString container = QStringLiteral("mp4");    // mp4, mov, mkv, webm, mp3, wav, m4a
    QString videoCodec = QStringLiteral("libx264"); // empty = audio only
    int width = 1920;
    int height = 1080;
    double frameRate = 30.0;
    int crf = 20;                                  // quality for x264/x265/vp9
    QString speedPreset = QStringLiteral("medium");
    QString audioCodec = QStringLiteral("aac");
    int audioBitrateKbps = 192;
    int sampleRate = 48000;
    double loudnessLufs = 0.0;  // integrated loudness target; 0 = no normalization
    bool builtIn = false;

    bool isAudioOnly() const { return videoCodec.isEmpty(); }

    QJsonObject toJson() const;
    static ExportPreset fromJson(const QJsonObject &o);
};

QVector<ExportPreset> builtInExportPresets();

// User presets are stored as JSON in the app data directory.
QVector<ExportPreset> loadUserExportPresets(const QString &file);
bool saveUserExportPresets(const QString &file, const QVector<ExportPreset> &presets);

} // namespace spotlight

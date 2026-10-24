#pragma once

#include "core/design.h"
#include "core/media.h"

#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

// Titles, lower thirds and captions for the timeline.
//
// A title is a small Design Studio document with a transparent background,
// rendered to a PNG and placed on an upper video track, where the exporter
// and preview composite it over the footage. The document is stored on the
// media item so the title stays editable.
namespace spotlight::titles {

QStringList presetNames();
// Index of the preset used for captions.
constexpr int kCaptionPreset = 2;

// Builds a title document for an output frame of the given size.
DesignDocument makeTitle(int preset, const QSize &frame, const QString &text,
                         const QString &subtitle = QString());

// The main / secondary text of a title (layers named "Title" / "Subtitle").
QString mainText(const DesignDocument &doc);
QString subtitleText(const DesignDocument &doc);
// Replaces the texts, re-centring layers on their original anchor.
void setTexts(DesignDocument &doc, const QString &text, const QString &subtitle);

// Folder for generated title images (inside the app's data directory).
QString generatedDir();

// Renders the document to a new PNG and describes it as a media item.
bool renderTitleMedia(const DesignDocument &doc, const QString &name, MediaItem *out, QString *error);

// Re-renders title PNGs that are missing on disk (e.g. a project opened on
// another computer). Returns the number re-rendered.
int restoreMissingTitles(QVector<MediaItem> &media);

// ---- Captions (SRT) -----------------------------------------------------
struct Caption {
    TimeUs start = 0;
    TimeUs end = 0;
    QString text;
};
QVector<Caption> parseSrt(const QString &content);
QString toSrt(const QVector<Caption> &captions);

} // namespace spotlight::titles

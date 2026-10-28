#pragma once

#include "core/media.h"

#include <QHash>
#include <QSet>
#include <QImage>
#include <QUuid>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QListWidget;
class QLabel;

namespace spotlight {

class Project;
class MediaProbe;

// The project's media library: import, thumbnails, drag to timeline.
// Shared by all three studios (it lives in the Video Studio but the Audio
// and Design studios import through it too).
class MediaBin : public QWidget {
    Q_OBJECT
public:
    MediaBin(Project *project, QWidget *parent = nullptr);

    QImage thumbnail(const QString &path) const { return m_thumbs.value(path); }
    QUuid currentMedia() const;

public slots:
    void importFiles(const QStringList &paths);
    void importDialog();
    void removeSelected();
    // Marks a media item as being scanned by Auto-Clip (shown in the bin).
    void setAnalyzing(const QUuid &mediaId, bool on);

signals:
    void openInSource(const QUuid &mediaId);
    void thumbnailsChanged();
    void status(const QString &message);
    void autoClipRequested(const QUuid &mediaId);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    void rebuild();
    void onProbed(const MediaItem &item, const QImage &thumb);

    Project *m_project;
    QListWidget *m_list;
    QLineEdit *m_search;
    QComboBox *m_sort; // import order, name, duration, type, unused first
    QLabel *m_hint;
    MediaProbe *m_probe;      // imports
    MediaProbe *m_thumbProbe; // thumbnails for media loaded from a project file
    QHash<QString, QImage> m_thumbs;
    QSet<QString> m_thumbRequested;
    QString m_shownSignature; // what the list currently displays
    QSet<QUuid> m_analyzing;
};

} // namespace spotlight

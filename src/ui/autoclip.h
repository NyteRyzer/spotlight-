#pragma once

#include "core/highlights.h"

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QUuid>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTemporaryDir;

namespace spotlight {

class Project;
class MediaBin;
namespace analysis {
class Job;
}

// Auto-Clip options as remembered between sessions.
highlights::Options savedAutoClipOptions();
void saveAutoClipOptions(const highlights::Options &o);

// Scans imported videos for highlights in the background. Long videos
// (3+ minutes) are scanned automatically after import unless the user
// turned that off; results are stored on the media item.
class AutoClipper : public QObject {
    Q_OBJECT
public:
    AutoClipper(Project *project, MediaBin *bin, QObject *parent = nullptr);
    ~AutoClipper() override;

    static bool autoScanEnabled();
    static void setAutoScanEnabled(bool on);
    static constexpr TimeUs kAutoScanMinDuration = 3 * 60 * kUsPerSecond;

    void scan(const QUuid &mediaId);
    bool isScanning(const QUuid &mediaId) const { return m_current == mediaId || m_queue.contains(mediaId); }
    void cancel(const QUuid &mediaId);

signals:
    void status(const QString &message);
    void progress(const QUuid &mediaId, double fraction);
    void finished(const QUuid &mediaId, bool ok);

private:
    void checkForNewMedia();
    void startNext();

    Project *m_project;
    MediaBin *m_bin;
    QVector<QUuid> m_queue;
    QUuid m_current;
    QPointer<analysis::Job> m_job;
    QSet<QUuid> m_seen;
};

// Review the highlights found in one video: preview them, put them on the
// timeline as a highlight reel, or export each as its own file (optionally
// reframed for Shorts / TikTok / Reels).
class AutoClipDialog : public QDialog {
    Q_OBJECT
public:
    AutoClipDialog(Project *project, AutoClipper *clipper, const QUuid &mediaId, QWidget *parent = nullptr);
    ~AutoClipDialog() override;

signals:
    void previewRequested(const QUuid &mediaId, spotlight::TimeUs in, spotlight::TimeUs out);
    void status(const QString &message);

private:
    void findHighlights();
    void showResults();
    QVector<Highlight> checkedHighlights() const;
    void addToTimeline();
    void exportClips();
    void makeThumbnails();

    Project *m_project;
    AutoClipper *m_clipper;
    QUuid m_mediaId;
    QLabel *m_header;
    QComboBox *m_length;
    QSpinBox *m_count;
    QSlider *m_sensitivity;
    QCheckBox *m_scenes;
    QCheckBox *m_autoScan;
    QPushButton *m_find;
    QStackedWidget *m_pages;
    QProgressBar *m_progress;
    QLabel *m_progressLabel;
    QListWidget *m_list;
    QLabel *m_empty;
    QCheckBox *m_transitions;
    QPushButton *m_preview;
    QPushButton *m_addTimeline;
    QPushButton *m_export;
    QPointer<analysis::Job> m_job;
    QTemporaryDir *m_thumbDir = nullptr;
    QVector<Highlight> m_results;
};

} // namespace spotlight

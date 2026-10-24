#include "ui/autoclip.h"

#include "core/analysis.h"
#include "core/exporter.h"
#include "core/project.h"
#include "core/timelineops.h"
#include "ui/mediabin.h"


#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <cmath>

namespace spotlight {

namespace {

QString secs(TimeUs t)
{
    return QString::number(usToSeconds(t), 'f', 3);
}

QString clock(TimeUs t)
{
    const qint64 s = t / kUsPerSecond;
    return s >= 3600 ? QStringLiteral("%1:%2:%3")
                           .arg(s / 3600)
                           .arg(s / 60 % 60, 2, 10, QLatin1Char('0'))
                           .arg(s % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

// Stores analysis results on the media item (one undoable step).
void storeResults(Project *project, const QUuid &mediaId, const QVector<Highlight> &hs)
{
    project->edit(QObject::tr("Auto-Clip"), [&](ProjectData &d) {
        for (auto &m : d.media) {
            if (m.id == mediaId) {
                m.highlights = hs;
                m.highlightsAnalyzed = true;
            }
        }
    });
}

} // namespace

highlights::Options savedAutoClipOptions()
{
    QSettings s;
    highlights::Options o;
    o.clipSeconds = s.value(QStringLiteral("autoclip/length"), 30.0).toDouble();
    o.maxClips = s.value(QStringLiteral("autoclip/count"), 8).toInt();
    o.sensitivity = s.value(QStringLiteral("autoclip/sensitivity"), 0.5).toDouble();
    o.useScenes = s.value(QStringLiteral("autoclip/scenes"), true).toBool();
    return o;
}

void saveAutoClipOptions(const highlights::Options &o)
{
    QSettings s;
    s.setValue(QStringLiteral("autoclip/length"), o.clipSeconds);
    s.setValue(QStringLiteral("autoclip/count"), o.maxClips);
    s.setValue(QStringLiteral("autoclip/sensitivity"), o.sensitivity);
    s.setValue(QStringLiteral("autoclip/scenes"), o.useScenes);
}

// ---------------------------------------------------------------------------

AutoClipper::AutoClipper(Project *project, MediaBin *bin, QObject *parent)
    : QObject(parent)
    , m_project(project)
    , m_bin(bin)
{
    connect(project, &Project::changed, this, &AutoClipper::checkForNewMedia);
    for (const auto &m : project->data().media)
        m_seen.insert(m.id);
}

AutoClipper::~AutoClipper()
{
    if (m_job)
        m_job->cancel();
}

bool AutoClipper::autoScanEnabled()
{
    return QSettings().value(QStringLiteral("autoclip/onImport"), true).toBool();
}

void AutoClipper::setAutoScanEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("autoclip/onImport"), on);
}

void AutoClipper::checkForNewMedia()
{
    // Media in a project that is being opened (or recovered) was already
    // there, not newly imported, so it isn't scanned unasked.
    const bool justLoaded = m_project->isResetting();
    for (const auto &m : m_project->data().media) {
        if (justLoaded)
            m_seen.insert(m.id);
        if (m_seen.contains(m.id))
            continue;
        m_seen.insert(m.id);
        if (autoScanEnabled() && !m.highlightsAnalyzed && !m.isStill() && !m.isTitle()
            && m.duration >= kAutoScanMinDuration && m.hasAudio)
            scan(m.id);
    }
    // Forget scans of media that was removed.
    if (!m_current.isNull() && !m_project->data().mediaById(m_current))
        cancel(m_current);
}

void AutoClipper::scan(const QUuid &mediaId)
{
    if (isScanning(mediaId))
        return;
    m_queue.append(mediaId);
    m_bin->setAnalyzing(mediaId, true);
    if (m_current.isNull())
        startNext();
}

void AutoClipper::cancel(const QUuid &mediaId)
{
    m_queue.removeAll(mediaId);
    m_bin->setAnalyzing(mediaId, false);
    if (m_current == mediaId && m_job) {
        m_job->cancel();
        m_job->deleteLater();
        m_current = QUuid();
        startNext();
    }
}

void AutoClipper::startNext()
{
    while (!m_queue.isEmpty()) {
        const QUuid id = m_queue.takeFirst();
        const MediaItem *m = m_project->data().mediaById(id);
        if (!m) {
            m_bin->setAnalyzing(id, false);
            continue;
        }
        m_current = id;
        const highlights::Options opt = savedAutoClipOptions();
        const double dur = usToSeconds(m->duration);
        const QString name = m->name;
        m_job = new analysis::Job(this);
        connect(m_job, &analysis::Job::progress, this, [this, id, dur](qint64 us) {
            emit progress(id, std::clamp(usToSeconds(us) / std::max(1.0, dur), 0.0, 1.0));
        });
        connect(m_job, &analysis::Job::finished, this, [this, id, dur, opt, name](bool ok, const QString &log) {
            m_job->deleteLater();
            m_current = QUuid();
            m_bin->setAnalyzing(id, false);
            if (ok && m_project->data().mediaById(id)) {
                const auto hs = highlights::findHighlights(highlights::parseAnalysis(log, dur), opt);
                storeResults(m_project, id, hs);
                emit status(hs.isEmpty()
                                ? tr("Auto-Clip: no standout moments in “%1”.").arg(name)
                                : tr("Auto-Clip found %n highlight(s) in “%1” — right-click it in the Media Bin to "
                                     "review.",
                                     nullptr, hs.size())
                                      .arg(name));
            } else if (!ok) {
                emit status(tr("Auto-Clip couldn't scan “%1”.").arg(name));
            }
            emit finished(id, ok);
            startNext();
        });
        QString err;
        if (!m_job->start(highlights::analysisArgs(m->path, m->hasVideo, opt.useScenes), &err)) {
            m_job->deleteLater();
            m_current = QUuid();
            m_bin->setAnalyzing(id, false);
            emit status(err);
            emit finished(id, false);
            continue;
        }
        emit status(tr("Auto-Clip is scanning “%1” for highlights…").arg(name));
        return;
    }
}

// ---------------------------------------------------------------------------

AutoClipDialog::AutoClipDialog(Project *project, AutoClipper *clipper, const QUuid &mediaId, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_clipper(clipper)
    , m_mediaId(mediaId)
    , m_header(new QLabel(this))
    , m_length(new QComboBox(this))
    , m_count(new QSpinBox(this))
    , m_sensitivity(new QSlider(Qt::Horizontal, this))
    , m_scenes(new QCheckBox(tr("Also look for fast action (scene changes)"), this))
    , m_autoScan(new QCheckBox(tr("Scan long videos (3+ min) automatically when I import them"), this))
    , m_find(new QPushButton(tr("Find Highlights"), this))
    , m_pages(new QStackedWidget(this))
    , m_progress(new QProgressBar(this))
    , m_progressLabel(new QLabel(this))
    , m_list(new QListWidget(this))
    , m_empty(new QLabel(this))
    , m_transitions(new QCheckBox(tr("Cross-dissolve between clips"), this))
    , m_preview(new QPushButton(tr("Preview"), this))
    , m_addTimeline(new QPushButton(tr("Add to Timeline"), this))
    , m_export(new QPushButton(tr("Export Clips…"), this))
{
    setWindowTitle(tr("Auto-Clip Highlights"));
    resize(720, 620);
    auto *layout = new QVBoxLayout(this);
    QFont hf = m_header->font();
    hf.setBold(true);
    hf.setPointSizeF(hf.pointSizeF() * 1.15);
    m_header->setFont(hf);
    m_header->setWordWrap(true);
    layout->addWidget(m_header);
    auto *intro = new QLabel(tr("Finds the moments worth clipping — loud reactions, hype, laughter and fast action — "
                                "and turns them into ready-to-post clips."),
                             this);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: #9a9aa8;"));
    layout->addWidget(intro);

    const highlights::Options o = savedAutoClipOptions();
    auto *form = new QFormLayout;
    for (int s : {15, 20, 30, 45, 60, 90})
        m_length->addItem(tr("%1 seconds").arg(s), s);
    m_length->setCurrentIndex(std::max(0, m_length->findData(int(o.clipSeconds))));
    m_count->setRange(1, 50);
    m_count->setValue(o.maxClips);
    m_sensitivity->setRange(0, 100);
    m_sensitivity->setValue(int(o.sensitivity * 100));
    m_sensitivity->setToolTip(tr("Left: only the biggest moments. Right: more, smaller moments."));
    m_scenes->setChecked(o.useScenes);
    m_autoScan->setChecked(AutoClipper::autoScanEnabled());
    auto *row = new QHBoxLayout;
    row->addWidget(m_length);
    row->addWidget(new QLabel(tr("Up to"), this));
    row->addWidget(m_count);
    row->addWidget(new QLabel(tr("clips"), this));
    row->addStretch();
    form->addRow(tr("Clip length"), row);
    auto *sens = new QHBoxLayout;
    sens->addWidget(new QLabel(tr("Biggest only"), this));
    sens->addWidget(m_sensitivity, 1);
    sens->addWidget(new QLabel(tr("More clips"), this));
    form->addRow(tr("Sensitivity"), sens);
    form->addRow(m_scenes);
    form->addRow(m_autoScan);
    layout->addLayout(form);
    m_find->setStyleSheet(QStringLiteral("QPushButton { font-weight: bold; padding: 6px 14px; }"));
    layout->addWidget(m_find, 0, Qt::AlignLeft);

    // Page 0: progress. Page 1: results.
    auto *progressPage = new QWidget(this);
    auto *pl = new QVBoxLayout(progressPage);
    pl->addStretch();
    pl->addWidget(m_progressLabel);
    pl->addWidget(m_progress);
    pl->addStretch();
    auto *resultsPage = new QWidget(this);
    auto *rl = new QVBoxLayout(resultsPage);
    rl->setContentsMargins(0, 0, 0, 0);
    m_list->setIconSize(QSize(160, 90));
    m_list->setSpacing(2);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    rl->addWidget(m_list, 1);
    m_empty->setWordWrap(true);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setStyleSheet(QStringLiteral("color: #9a9aa8; padding: 30px;"));
    rl->addWidget(m_empty);
    m_pages->addWidget(progressPage);
    m_pages->addWidget(resultsPage);
    layout->addWidget(m_pages, 1);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_preview);
    actions->addWidget(m_transitions);
    actions->addStretch();
    actions->addWidget(m_addTimeline);
    actions->addWidget(m_export);
    auto *close = new QPushButton(tr("Close"), this);
    actions->addWidget(close);
    layout->addLayout(actions);
    m_addTimeline->setStyleSheet(QStringLiteral("QPushButton { font-weight: bold; }"));
    m_transitions->setChecked(true);

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_find, &QPushButton::clicked, this, &AutoClipDialog::findHighlights);
    connect(m_autoScan, &QCheckBox::toggled, this, [](bool on) { AutoClipper::setAutoScanEnabled(on); });
    connect(m_preview, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row >= 0 && row < m_results.size())
            emit previewRequested(m_mediaId, m_results[row].start, m_results[row].end);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, m_preview, &QPushButton::click);
    connect(m_addTimeline, &QPushButton::clicked, this, &AutoClipDialog::addToTimeline);
    connect(m_export, &QPushButton::clicked, this, &AutoClipDialog::exportClips);
    connect(m_list, &QListWidget::itemChanged, this, [this] {
        const bool any = !checkedHighlights().isEmpty();
        m_addTimeline->setEnabled(any);
        m_export->setEnabled(any);
    });

    const MediaItem *m = project->data().mediaById(mediaId);
    m_header->setText(m ? tr("%1  ·  %2").arg(m->name, formatDuration(m->duration)) : tr("Media not found"));
    m_find->setEnabled(m != nullptr);

    if (m && clipper->isScanning(mediaId)) {
        // A background scan is already running: follow it.
        m_pages->setCurrentIndex(0);
        m_progressLabel->setText(tr("Scanning for highlights…"));
        m_progress->setRange(0, 100);
        m_find->setEnabled(false);
        connect(clipper, &AutoClipper::progress, this, [this](const QUuid &id, double f) {
            if (id == m_mediaId)
                m_progress->setValue(int(f * 100));
        });
        connect(clipper, &AutoClipper::finished, this, [this](const QUuid &id) {
            if (id != m_mediaId)
                return;
            m_find->setEnabled(true);
            showResults();
        });
    } else if (m && m->highlightsAnalyzed) {
        m_find->setText(tr("Find Again"));
        showResults();
    } else if (m) {
        findHighlights();
    }
}

AutoClipDialog::~AutoClipDialog()
{
    if (m_job)
        m_job->cancel();
    delete m_thumbDir;
}

void AutoClipDialog::findHighlights()
{
    const MediaItem *m = m_project->data().mediaById(m_mediaId);
    if (!m)
        return;
    highlights::Options o;
    o.clipSeconds = m_length->currentData().toDouble();
    o.maxClips = m_count->value();
    o.sensitivity = m_sensitivity->value() / 100.0;
    o.useScenes = m_scenes->isChecked();
    saveAutoClipOptions(o);
    if (m_clipper->isScanning(m_mediaId))
        m_clipper->cancel(m_mediaId);

    m_pages->setCurrentIndex(0);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progressLabel->setText(tr("Listening for big moments in “%1”…").arg(m->name));
    m_find->setEnabled(false);
    for (auto *b : {m_preview, m_addTimeline, m_export})
        b->setEnabled(false);
    const double dur = usToSeconds(m->duration);
    m_job = new analysis::Job(this);
    connect(m_job, &analysis::Job::progress, this, [this, dur](qint64 us) {
        m_progress->setValue(int(100 * std::clamp(usToSeconds(us) / std::max(1.0, dur), 0.0, 1.0)));
    });
    connect(m_job, &analysis::Job::finished, this, [this, dur, o](bool ok, const QString &log) {
        m_job->deleteLater();
        m_find->setEnabled(true);
        m_find->setText(tr("Find Again"));
        if (!ok) {
            QMessageBox::warning(this, windowTitle(), tr("The video couldn't be scanned:\n\n%1").arg(log.right(1500)));
            showResults();
            return;
        }
        storeResults(m_project, m_mediaId, highlights::findHighlights(highlights::parseAnalysis(log, dur), o));
        showResults();
    });
    QString err;
    if (!m_job->start(highlights::analysisArgs(m->path, m->hasVideo, o.useScenes), &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        m_find->setEnabled(true);
    }
}

void AutoClipDialog::showResults()
{
    const MediaItem *m = m_project->data().mediaById(m_mediaId);
    m_results = m ? m->highlights : QVector<Highlight>();
    m_pages->setCurrentIndex(1);
    m_list->clear();
    m_list->setVisible(!m_results.isEmpty());
    m_empty->setVisible(m_results.isEmpty());
    m_empty->setText(tr("No standout moments found.\nTry moving Sensitivity towards “More clips” and press Find Again."));
    QSignalBlocker block(m_list);
    for (int i = 0; i < m_results.size(); ++i) {
        const Highlight &h = m_results[i];
        auto *item = new QListWidgetItem(m_list);
        const int stars = std::clamp(int(std::lround(h.score / 20.0)), 1, 5);
        item->setText(tr("#%1   %2 – %3   (%4)\n%5   %6")
                          .arg(i + 1)
                          .arg(clock(h.start), clock(h.end), formatDuration(h.duration()),
                               tr(qPrintable(h.reason)), QString(stars, QChar(0x2605)) + QString(5 - stars, QChar(0x2606))));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setSizeHint(QSize(0, 98));
    }
    if (!m_results.isEmpty())
        m_list->setCurrentRow(0);
    for (auto *b : {m_preview, m_addTimeline, m_export})
        b->setEnabled(!m_results.isEmpty());
    makeThumbnails();
}

void AutoClipDialog::makeThumbnails()
{
    const MediaItem *m = m_project->data().mediaById(m_mediaId);
    const QString ffmpeg = Exporter::findFfmpeg();
    if (!m || !m->hasVideo || ffmpeg.isEmpty() || m_results.isEmpty())
        return;
    delete m_thumbDir;
    m_thumbDir = new QTemporaryDir;
    // One frame from the middle of each clip, generated one after another.
    auto next = std::make_shared<std::function<void(int)>>();
    const QString path = m->path;
    const QString dir = m_thumbDir->path();
    QPointer<AutoClipDialog> self(this);
    *next = [self, next, ffmpeg, path, dir](int i) {
        if (!self || i >= self->m_results.size() || i >= self->m_list->count())
            return;
        const Highlight h = self->m_results[i];
        const QString out = QStringLiteral("%1/%2.jpg").arg(dir).arg(i);
        auto *p = new QProcess(self);
        QObject::connect(p, &QProcess::finished, self, [self, next, p, out, i] {
            p->deleteLater();
            if (!self)
                return;
            if (QListWidgetItem *item = self->m_list->item(i))
                item->setIcon(QIcon(QPixmap(out)));
            (*next)(i + 1);
        });
        p->start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                          QStringLiteral("error"), QStringLiteral("-ss"), secs((h.start + h.end) / 2),
                          QStringLiteral("-i"), path, QStringLiteral("-frames:v"), QStringLiteral("1"),
                          QStringLiteral("-vf"), QStringLiteral("scale=320:-2"), out});
    };
    (*next)(0);
}

QVector<Highlight> AutoClipDialog::checkedHighlights() const
{
    QVector<Highlight> out;
    for (int i = 0; i < m_list->count() && i < m_results.size(); ++i) {
        if (m_list->item(i)->checkState() == Qt::Checked)
            out.append(m_results[i]);
    }
    return out;
}

void AutoClipDialog::addToTimeline()
{
    QVector<Highlight> hs = checkedHighlights();
    if (hs.isEmpty())
        return;
    // A highlight reel plays in story order.
    std::sort(hs.begin(), hs.end(), [](const Highlight &a, const Highlight &b) { return a.start < b.start; });
    const bool dissolve = m_transitions->isChecked();
    int placed = 0;
    m_project->edit(tr("Add Highlight Reel"), [&](ProjectData &d) {
        const MediaItem *m = d.mediaById(m_mediaId);
        if (!m)
            return;
        const int track = m->hasVideo ? d.videoTrackIndices().value(0, -1) : d.audioTrackIndices().value(0, -1);
        TimeUs cursor = d.duration();
        for (const auto &h : hs) {
            const QUuid id = timeline::placeMedia(d, m_mediaId, track, cursor, h.start, h.end);
            Clip *c = d.clipById(id);
            if (!c)
                continue;
            if (dissolve && placed > 0 && m->hasVideo)
                c->transition = {QStringLiteral("fade"), 500 * kUsPerMs};
            cursor = c->end();
            ++placed;
        }
    });
    emit status(tr("Added %n clip(s) to the end of the timeline.", nullptr, placed));
    accept();
}

void AutoClipDialog::exportClips()
{
    const QVector<Highlight> hs = checkedHighlights();
    const MediaItem *mp = m_project->data().mediaById(m_mediaId);
    if (hs.isEmpty() || !mp)
        return;
    const MediaItem m = *mp;

    QDialog opts(this);
    opts.setWindowTitle(tr("Export Clips"));
    auto *form = new QFormLayout(&opts);
    auto *format = new QComboBox(&opts);
    format->addItem(tr("Original shape (%1×%2)").arg(m.resolution.width()).arg(m.resolution.height()), 0);
    if (m.hasVideo) {
        format->addItem(tr("Vertical 9:16 — Shorts, TikTok, Reels (1080×1920)"), 1);
        format->addItem(tr("Square 1:1 — Instagram (1080×1080)"), 2);
    }
    form->addRow(tr("Format"), format);
    QSettings settings;
    QString folder = settings.value(QStringLiteral("autoclip/exportDir"),
                                    QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                         .toString();
    auto *folderBtn = new QPushButton(folder, &opts);
    connect(folderBtn, &QPushButton::clicked, &opts, [&] {
        const QString f = QFileDialog::getExistingDirectory(&opts, tr("Export Clips To"), folder);
        if (!f.isEmpty()) {
            folder = f;
            folderBtn->setText(f);
        }
    });
    form->addRow(tr("Save to"), folderBtn);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &opts);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Export %n Clip(s)", nullptr, hs.size()));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &opts, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &opts, &QDialog::reject);
    if (opts.exec() != QDialog::Accepted)
        return;
    settings.setValue(QStringLiteral("autoclip/exportDir"), folder);
    QDir().mkpath(folder);

    const QString ffmpeg = Exporter::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("FFmpeg was not found."));
        return;
    }
    const int shape = format->currentData().toInt();
    QString vf;
    if (shape == 1) // centre crop to 9:16, then scale
        vf = QStringLiteral("crop='min(iw,ih*9/16)':'min(ih,iw*16/9)',scale=1080:1920,setsar=1");
    else if (shape == 2)
        vf = QStringLiteral("crop='min(iw,ih)':'min(iw,ih)',scale=1080:1080,setsar=1");

    QProgressDialog progress(tr("Exporting clips…"), tr("Cancel"), 0, hs.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    const QString base = QFileInfo(m.name).completeBaseName();
    QStringList written;
    for (int i = 0; i < hs.size(); ++i) {
        progress.setValue(i);
        progress.setLabelText(tr("Exporting clip %1 of %2…").arg(i + 1).arg(hs.size()));
        if (progress.wasCanceled())
            break;
        const Highlight &h = hs[i];
        const QString suffix = shape == 1 ? QStringLiteral(" vertical") : (shape == 2 ? QStringLiteral(" square") : QString());
        const QString out = QDir(folder).filePath(
            QStringLiteral("%1 - Clip %2 (%3)%4.%5")
                .arg(base)
                .arg(i + 1, 2, 10, QLatin1Char('0'))
                .arg(clock(h.start).replace(QLatin1Char(':'), QLatin1Char('.')), suffix,
                     m.hasVideo ? QStringLiteral("mp4") : QStringLiteral("m4a")));
        QStringList args{QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                         QStringLiteral("error"), QStringLiteral("-ss"), secs(h.start), QStringLiteral("-t"),
                         secs(h.duration()), QStringLiteral("-i"), m.path};
        if (m.hasVideo) {
            if (!vf.isEmpty())
                args << QStringLiteral("-vf") << vf;
            args << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-crf")
                 << QStringLiteral("20") << QStringLiteral("-preset") << QStringLiteral("veryfast")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
        } else {
            args << QStringLiteral("-vn");
        }
        if (m.hasAudio)
            args << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a") << QStringLiteral("192k");
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart") << out;
        QProcess p;
        p.start(ffmpeg, args);
        while (!p.waitForFinished(100)) {
            QCoreApplication::processEvents();
            if (progress.wasCanceled()) {
                p.kill();
                p.waitForFinished(2000);
                QFile::remove(out);
                break;
            }
        }
        if (progress.wasCanceled())
            break;
        if (p.exitCode() != 0) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("Clip %1 failed:\n\n%2").arg(i + 1).arg(QString::fromUtf8(p.readAllStandardError())));
            break;
        }
        written << out;
    }
    progress.setValue(hs.size());
    if (!written.isEmpty())
        emit status(tr("Exported %n clip(s) to %1", nullptr, written.size()).arg(folder));
}

} // namespace spotlight

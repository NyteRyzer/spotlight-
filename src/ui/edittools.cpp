#include "ui/edittools.h"

#include "core/analysis.h"
#include "core/designrender.h"
#include "core/exporter.h"
#include "core/looks.h"
#include "core/project.h"
#include "core/timelineops.h"
#include "core/titles.h"
#include "media/mediaprobe.h"
#include "media/timelineplayer.h"
#include "ui/mediabin.h"
#include "ui/timelinewidget.h"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QDateTime>
#include <QPainter>
#include <QAudioInput>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace spotlight {

namespace {

QString secs(TimeUs t)
{
    return QString::number(usToSeconds(t), 'f', 6);
}

// Runs an FFmpeg analysis with a cancellable progress dialog. Returns false
// if cancelled or failed; *log receives FFmpeg's output.
bool runAnalysis(QWidget *parent, const QString &title, const QStringList &args, QString *log)
{
    analysis::Job job;
    QString err;
    if (!job.start(args, &err)) {
        QMessageBox::warning(parent, title, err);
        return false;
    }
    QProgressDialog progress(title, QObject::tr("Cancel"), 0, 0, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    bool ok = false;
    bool done = false;
    QObject::connect(&job, &analysis::Job::finished, &progress, [&](bool success, const QString &l) {
        ok = success;
        *log = l;
        done = true;
        progress.reset();
    });
    progress.exec();
    if (!done) {
        job.cancel();
        return false;
    }
    if (!ok)
        QMessageBox::warning(parent, title, QObject::tr("FFmpeg failed:\n\n%1").arg(log->right(2000)));
    return ok;
}

// Runs FFmpeg to produce a file, with a cancellable progress dialog.
bool runFfmpeg(QWidget *parent, const QString &title, const QStringList &args, QString *error)
{
    const QString ffmpeg = Exporter::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        *error = QObject::tr("FFmpeg was not found.");
        return false;
    }
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    QProgressDialog progress(title, QObject::tr("Cancel"), 0, 0, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(200);
    QString log;
    QObject::connect(&proc, &QProcess::readyRead, &progress, [&] { log += QString::fromUtf8(proc.readAll()); });
    QObject::connect(&proc, &QProcess::finished, &progress, [&] { progress.reset(); });
    proc.start(ffmpeg, args);
    if (!proc.waitForStarted(5000)) {
        *error = QObject::tr("Could not start FFmpeg.");
        return false;
    }
    progress.exec();
    if (proc.state() != QProcess::NotRunning) {
        proc.kill();
        proc.waitForFinished(3000);
        *error = QObject::tr("Cancelled.");
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        *error = log.right(2000);
        return false;
    }
    return true;
}

// Probes a file synchronously (used for files we just generated).
bool probeFile(const QString &path, MediaItem *out, QString *error)
{
    MediaProbe probe;
    QEventLoop loop;
    bool ok = false;
    QObject::connect(&probe, &MediaProbe::probed, &loop, [&](const MediaItem &item, const QImage &) {
        *out = item;
        ok = true;
        loop.quit();
    });
    QObject::connect(&probe, &MediaProbe::failed, &loop, [&](const QString &, const QString &e) {
        *error = e;
        loop.quit();
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    probe.probe(path);
    loop.exec();
    return ok;
}

// Where generated media (freeze frames, reversed clips, recordings) goes:
// next to the project when it has been saved, else the app's data folder.
QString mediaDir(const Project *project, const QString &sub)
{
    QString base;
    if (!project->filePath().isEmpty())
        base = QFileInfo(project->filePath()).absolutePath() + QStringLiteral("/SPOTLIGHT Media");
    else
        base = titles::generatedDir();
    const QString dir = base + QLatin1Char('/') + sub;
    QDir().mkpath(dir);
    return dir;
}

// Dialog for creating / editing a title, with a live preview.
class TitleDialog : public QDialog {
public:
    TitleDialog(const QSize &frame, int preset, const QString &text, const QString &sub, bool allowPreset,
                QWidget *parent)
        : QDialog(parent)
        , m_frame(frame)
    {
        setWindowTitle(allowPreset ? tr("Add Title") : tr("Edit Title"));
        auto *form = new QFormLayout(this);
        m_preset = new QComboBox(this);
        m_preset->addItems(titles::presetNames());
        m_preset->setCurrentIndex(preset);
        m_preset->setEnabled(allowPreset);
        form->addRow(tr("Style"), m_preset);
        m_text = new QPlainTextEdit(text, this);
        m_text->setFixedHeight(64);
        form->addRow(tr("Text"), m_text);
        m_sub = new QLineEdit(sub, this);
        m_sub->setPlaceholderText(tr("Optional (name role, chapter number, quote author…)"));
        form->addRow(tr("Second line"), m_sub);
        m_duration = new QDoubleSpinBox(this);
        m_duration->setRange(0.5, 60);
        m_duration->setValue(4);
        m_duration->setSuffix(QStringLiteral(" s"));
        m_duration->setVisible(allowPreset);
        if (allowPreset)
            form->addRow(tr("Duration"), m_duration);
        m_preview = new QLabel(this);
        m_preview->setMinimumSize(480, 270);
        m_preview->setAlignment(Qt::AlignCenter);
        form->addRow(m_preview);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_preset, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
        connect(m_text, &QPlainTextEdit::textChanged, this, [this] { updatePreview(); });
        connect(m_sub, &QLineEdit::textChanged, this, [this] { updatePreview(); });
        updatePreview();
    }

    DesignDocument document() const
    {
        return titles::makeTitle(m_preset->currentIndex(), m_frame, text(), m_sub->text());
    }
    QString text() const { return m_text->toPlainText(); }
    QString subtitle() const { return m_sub->text(); }
    TimeUs duration() const { return secondsToUs(m_duration->value()); }

private:
    void updatePreview()
    {
        DesignRenderer r;
        const DesignDocument doc = document();
        QImage img(doc.canvasSize, QImage::Format_ARGB32_Premultiplied);
        // Checkerboard-free preview on a dark frame so white text shows.
        img.fill(QColor(40, 44, 52));
        QPainter p(&img);
        r.render(p, doc);
        p.end();
        m_preview->setPixmap(QPixmap::fromImage(img.scaled(480, 270, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }

    QSize m_frame;
    QComboBox *m_preset;
    QPlainTextEdit *m_text;
    QLineEdit *m_sub;
    QDoubleSpinBox *m_duration;
    QLabel *m_preview;
};

} // namespace

EditTools::EditTools(Project *project, TimelinePlayer *player, MediaBin *bin, QWidget *window)
    : QObject(window)
    , m_project(project)
    , m_player(player)
    , m_bin(bin)
    , m_window(window)
{
}

QUuid EditTools::targetClip(bool videoOnly) const
{
    const ProjectData &d = m_project->data();
    if (TimelineWidget *tl = timeline()) {
        for (const auto &id : tl->selection()) {
            int ti = -1;
            if (d.clipById(id, &ti) && (!videoOnly || d.tracks[ti].isVideo()))
                return id;
        }
        const int ti = tl->currentTrack();
        if (ti >= 0 && ti < d.tracks.size() && (!videoOnly || d.tracks[ti].isVideo())) {
            const int ci = timeline::clipIndexAt(d.tracks[ti], m_player->position());
            if (ci >= 0)
                return d.tracks[ti].clips[ci].id;
        }
    }
    if (const Clip *c = timeline::topVideoClipAt(d, m_player->position()))
        return c->id;
    return {};
}

int EditTools::freeOverlayTrack(ProjectData &d, TimeUs start, TimeUs end)
{
    const QVector<int> videos = d.videoTrackIndices();
    // Prefer the highest video track above V1 that is free over the range.
    for (int i = videos.size() - 1; i >= 1; --i) {
        bool busy = false;
        for (const auto &c : d.tracks[videos[i]].clips) {
            if (c.start < end && c.end() > start)
                busy = true;
        }
        if (!busy && !d.tracks[videos[i]].locked)
            return videos[i];
    }
    return d.addTrack(TrackType::Video);
}

void EditTools::freezeFrame()
{
    const QUuid id = targetClip(true);
    const ProjectData &d = m_project->data();
    const Clip *c = d.clipById(id);
    const TimeUs at = m_player->position();
    if (!c || !c->contains(at)) {
        emit status(tr("Put the playhead over a video clip to freeze a frame."));
        return;
    }
    const MediaItem *m = d.mediaById(c->mediaId);
    if (!m || m->isStill()) {
        emit status(tr("That clip is already a still image."));
        return;
    }
    const QString out = mediaDir(m_project, QStringLiteral("Freeze Frames"))
        + QStringLiteral("/freeze-%1.png").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    QString err;
    if (!runFfmpeg(m_window, tr("Grabbing frame…"),
                   {QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                    QStringLiteral("error"), QStringLiteral("-ss"), secs(c->sourceAt(at)), QStringLiteral("-i"),
                    m->path, QStringLiteral("-frames:v"), QStringLiteral("1"), out},
                   &err)) {
        QMessageBox::warning(m_window, tr("Freeze Frame"), err);
        return;
    }
    const QImage img(out);
    MediaItem still;
    still.id = QUuid::createUuid();
    still.path = out;
    still.name = tr("Freeze – %1").arg(m->name);
    still.kind = MediaKind::Image;
    still.resolution = img.size();
    still.hasVideo = true;
    QUuid placed;
    m_project->edit(tr("Freeze Frame"), [&](ProjectData &pd) {
        pd.media.append(still);
        placed = timeline::insertFreezeFrame(pd, id, at, still.id, 2 * kUsPerSecond);
    });
    if (TimelineWidget *tl = timeline(); tl && !placed.isNull())
        tl->setSelection({placed});
    emit status(tr("Inserted a 2 second freeze frame. Drag its edge to change the length."));
}

void EditTools::reverseClip()
{
    const QUuid id = targetClip();
    const ProjectData &d = m_project->data();
    const Clip *c = d.clipById(id);
    const MediaItem *m = c ? d.mediaById(c->mediaId) : nullptr;
    if (!c || !m || m->isStill()) {
        emit status(tr("Select a video or audio clip to reverse."));
        return;
    }
    if (c->sourceDuration() > 90 * kUsPerSecond
        && QMessageBox::question(m_window, tr("Reverse Clip"),
                                 tr("This clip is long; reversing it needs a lot of memory and time. Continue?"))
            != QMessageBox::Yes)
        return;
    const QString out = mediaDir(m_project, QStringLiteral("Reversed"))
        + QStringLiteral("/%1-reversed-%2.mp4")
              .arg(QFileInfo(m->path).completeBaseName(),
                   QUuid::createUuid().toString(QUuid::WithoutBraces).left(6));
    QStringList args{QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                     QStringLiteral("error"), QStringLiteral("-ss"), secs(c->in), QStringLiteral("-t"),
                     secs(c->sourceDuration()), QStringLiteral("-i"), m->path};
    if (m->hasVideo)
        args << QStringLiteral("-vf") << QStringLiteral("reverse") << QStringLiteral("-c:v") << QStringLiteral("libx264")
             << QStringLiteral("-crf") << QStringLiteral("16") << QStringLiteral("-preset") << QStringLiteral("veryfast")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    else
        args << QStringLiteral("-vn");
    if (m->hasAudio)
        args << QStringLiteral("-af") << QStringLiteral("areverse") << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-b:a") << QStringLiteral("256k");
    else
        args << QStringLiteral("-an");
    args << out;
    QString err;
    if (!runFfmpeg(m_window, tr("Reversing “%1”…").arg(m->name), args, &err)) {
        if (err != tr("Cancelled."))
            QMessageBox::warning(m_window, tr("Reverse Clip"), err);
        return;
    }
    MediaItem rev;
    if (!probeFile(out, &rev, &err)) {
        QMessageBox::warning(m_window, tr("Reverse Clip"), err);
        return;
    }
    rev.name = tr("%1 (reversed)").arg(m->name);
    const QUuid oldMedia = m->id;
    m_project->edit(tr("Reverse Clip"), [&](ProjectData &pd) {
        pd.media.append(rev);
        for (const auto &gid : timeline::groupOf(pd, id)) {
            Clip *gc = pd.clipById(gid);
            if (!gc || gc->mediaId != oldMedia)
                continue;
            const TimeUs len = gc->sourceDuration();
            gc->mediaId = rev.id;
            gc->in = 0;
            gc->out = std::min(len, rev.duration > 0 ? rev.duration : len);
        }
    });
    emit status(tr("Clip reversed."));
}

void EditTools::removeSilences()
{
    const QUuid id = targetClip();
    const ProjectData &d = m_project->data();
    const Clip *c = d.clipById(id);
    const MediaItem *m = c ? d.mediaById(c->mediaId) : nullptr;
    if (!c || !m || !m->hasAudio) {
        emit status(tr("Select a clip with sound (e.g. a talking-head video) to remove its pauses."));
        return;
    }
    QDialog dlg(m_window);
    dlg.setWindowTitle(tr("Remove Silences"));
    auto *form = new QFormLayout(&dlg);
    auto *info = new QLabel(tr("Finds pauses in “%1” and cuts them out of the whole timeline (jump cuts), "
                               "keeping everything in sync.")
                                .arg(m->name),
                            &dlg);
    info->setWordWrap(true);
    form->addRow(info);
    QSettings s;
    auto *threshold = new QDoubleSpinBox(&dlg);
    threshold->setRange(-70, -10);
    threshold->setSuffix(QStringLiteral(" dB"));
    threshold->setValue(s.value(QStringLiteral("silence/threshold"), -38.0).toDouble());
    threshold->setToolTip(tr("Anything quieter than this counts as silence"));
    form->addRow(tr("Silence below"), threshold);
    auto *minLen = new QDoubleSpinBox(&dlg);
    minLen->setRange(0.2, 10);
    minLen->setSingleStep(0.1);
    minLen->setSuffix(QStringLiteral(" s"));
    minLen->setValue(s.value(QStringLiteral("silence/min"), 0.6).toDouble());
    form->addRow(tr("Shortest pause"), minLen);
    auto *pad = new QDoubleSpinBox(&dlg);
    pad->setRange(0, 1);
    pad->setSingleStep(0.05);
    pad->setSuffix(QStringLiteral(" s"));
    pad->setValue(s.value(QStringLiteral("silence/pad"), 0.12).toDouble());
    pad->setToolTip(tr("Breathing room kept around words"));
    form->addRow(tr("Keep around words"), pad);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Remove Silences"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    s.setValue(QStringLiteral("silence/threshold"), threshold->value());
    s.setValue(QStringLiteral("silence/min"), minLen->value());
    s.setValue(QStringLiteral("silence/pad"), pad->value());

    const Clip clip = *c;
    QString log;
    if (!runAnalysis(m_window, tr("Finding silences…"),
                     analysis::silenceArgs(m->path, clip.in, clip.sourceDuration(), threshold->value(),
                                           secondsToUs(minLen->value())),
                     &log))
        return;
    const auto silences = analysis::padRanges(analysis::parseSilences(log, clip.sourceDuration()),
                                              secondsToUs(pad->value()), kMinClipDuration * 3);
    if (silences.isEmpty()) {
        emit status(tr("No pauses found — try a higher threshold."));
        return;
    }
    QVector<QPair<TimeUs, TimeUs>> ranges;
    for (const auto &r : silences)
        ranges.append({clip.start + clip.toTimeline(r.first), clip.start + clip.toTimeline(r.second)});
    TimeUs removed = 0;
    m_project->edit(tr("Remove Silences"), [&](ProjectData &pd) { removed = timeline::rippleDeleteRanges(pd, ranges); });
    emit status(tr("Removed %n pause(s), %1 shorter.", nullptr, ranges.size()).arg(formatDuration(removed)));
}

void EditTools::detectScenes()
{
    const QUuid id = targetClip(true);
    const ProjectData &d = m_project->data();
    const Clip *c = d.clipById(id);
    const MediaItem *m = c ? d.mediaById(c->mediaId) : nullptr;
    if (!c || !m || m->isStill() || !m->hasVideo) {
        emit status(tr("Select a video clip to split at its scene changes."));
        return;
    }
    const Clip clip = *c;
    QString log;
    if (!runAnalysis(m_window, tr("Detecting scene cuts…"), analysis::sceneArgs(m->path, clip.in, clip.sourceDuration(), 12.0),
                     &log))
        return;
    QVector<TimeUs> cuts;
    for (TimeUs t : analysis::parseScenes(log))
        cuts.append(clip.start + clip.toTimeline(t));
    if (cuts.isEmpty()) {
        emit status(tr("No scene changes found in this clip."));
        return;
    }
    int n = 0;
    m_project->edit(tr("Scene Edit Detection"), [&](ProjectData &pd) { n = timeline::splitAtTimes(pd, id, cuts); });
    emit status(tr("Split the clip at %n scene change(s).", nullptr, n));
}

void EditTools::addBeatMarkers()
{
    QDialog dlg(m_window);
    dlg.setWindowTitle(tr("Beat Markers"));
    auto *form = new QFormLayout(&dlg);
    auto *info = new QLabel(tr("Adds markers on the beat so cuts land in time with the music. "
                               "Clips snap to markers while you drag them."),
                            &dlg);
    info->setWordWrap(true);
    form->addRow(info);
    QSettings s;
    auto *bpm = new QDoubleSpinBox(&dlg);
    bpm->setRange(40, 300);
    bpm->setDecimals(1);
    bpm->setValue(s.value(QStringLiteral("beats/bpm"), 120.0).toDouble());
    form->addRow(tr("Tempo (BPM)"), bpm);
    // Tap tempo: average interval of the last taps.
    auto *tap = new QPushButton(tr("Tap"), &dlg);
    auto taps = std::make_shared<QVector<qint64>>();
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    connect(tap, &QPushButton::clicked, &dlg, [=] {
        const qint64 now = clock->elapsed();
        if (!taps->isEmpty() && now - taps->last() > 2000)
            taps->clear();
        taps->append(now);
        if (taps->size() >= 2) {
            const double avg = double(taps->last() - taps->first()) / (taps->size() - 1);
            bpm->setValue(60000.0 / avg);
        }
    });
    form->addRow(tr("Tap tempo"), tap);
    auto *offset = new QDoubleSpinBox(&dlg);
    offset->setRange(0, 36000);
    offset->setDecimals(3);
    offset->setSuffix(QStringLiteral(" s"));
    offset->setValue(usToSeconds(m_player->position()));
    offset->setToolTip(tr("Time of a downbeat (defaults to the playhead)"));
    form->addRow(tr("First beat at"), offset);
    auto *perBar = new QSpinBox(&dlg);
    perBar->setRange(1, 12);
    perBar->setValue(4);
    form->addRow(tr("Beats per bar"), perBar);
    auto *barsOnly = new QCheckBox(tr("Only mark the first beat of each bar"), &dlg);
    form->addRow(barsOnly);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    s.setValue(QStringLiteral("beats/bpm"), bpm->value());
    const TimeUs end = std::max(m_project->data().duration(), 60 * kUsPerSecond);
    int n = 0;
    m_project->edit(tr("Beat Markers"), [&](ProjectData &d) {
        n = timeline::addBeatMarkers(d, bpm->value(), secondsToUs(offset->value()), 0, end, perBar->value(),
                                     barsOnly->isChecked());
    });
    emit status(tr("Added %n beat marker(s).", nullptr, n));
}

void EditTools::addTitle(int preset)
{
    const SequenceSettings seq = m_project->data().sequence;
    TitleDialog dlg(QSize(seq.width, seq.height), preset, preset == 3 ? QStringLiteral("SUBSCRIBE") : tr("Your Title"),
                    QString(), true, m_window);
    if (dlg.exec() != QDialog::Accepted)
        return;
    MediaItem m;
    QString err;
    const QString name = tr("Title – %1").arg(dlg.text().section(QLatin1Char('\n'), 0, 0).left(30));
    if (!titles::renderTitleMedia(dlg.document(), name, &m, &err)) {
        QMessageBox::warning(m_window, tr("Add Title"), err);
        return;
    }
    const TimeUs at = m_player->position();
    QUuid placed;
    m_project->edit(tr("Add Title"), [&](ProjectData &d) {
        d.media.append(m);
        const int track = freeOverlayTrack(d, at, at + dlg.duration());
        placed = timeline::placeMedia(d, m.id, track, at, 0, dlg.duration());
        if (Clip *c = d.clipById(placed)) {
            c->fadeIn = std::min<TimeUs>(c->duration() / 4, 300 * kUsPerMs);
            c->fadeOut = c->fadeIn;
        }
    });
    if (TimelineWidget *tl = timeline(); tl && !placed.isNull())
        tl->setSelection({placed});
    emit status(tr("Title added. Use the Inspector to move, scale or animate it."));
}

void EditTools::editTitle()
{
    const QUuid id = targetClip(true);
    const ProjectData &d = m_project->data();
    const Clip *c = d.clipById(id);
    const MediaItem *m = c ? d.mediaById(c->mediaId) : nullptr;
    if (!m || !m->isTitle()) {
        emit status(tr("Select a title clip to edit it."));
        return;
    }
    DesignDocument doc = DesignDocument::fromJson(m->titleDesign);
    TitleDialog dlg(doc.canvasSize, 0, titles::mainText(doc), titles::subtitleText(doc), false, m_window);
    if (dlg.exec() != QDialog::Accepted)
        return;
    titles::setTexts(doc, dlg.text(), dlg.subtitle());
    MediaItem fresh;
    QString err;
    if (!titles::renderTitleMedia(doc, m->name, &fresh, &err)) {
        QMessageBox::warning(m_window, tr("Edit Title"), err);
        return;
    }
    QJsonObject json = doc.toJson();
    if (m->titleDesign.contains(QStringLiteral("caption")))
        json[QStringLiteral("caption")] = true;
    const QUuid mediaId = m->id;
    m_project->edit(tr("Edit Title"), [&](ProjectData &pd) {
        for (auto &mi : pd.media) {
            if (mi.id == mediaId) {
                mi.path = fresh.path;
                mi.titleDesign = json;
                mi.name = tr("Title – %1").arg(dlg.text().section(QLatin1Char('\n'), 0, 0).left(30));
            }
        }
    });
}

void EditTools::importCaptions()
{
    const QString path = QFileDialog::getOpenFileName(m_window, tr("Import Captions"), QString(),
                                                      tr("Subtitles (*.srt)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(m_window, tr("Import Captions"), f.errorString());
        return;
    }
    const auto caps = titles::parseSrt(QString::fromUtf8(f.readAll()));
    if (caps.isEmpty()) {
        QMessageBox::warning(m_window, tr("Import Captions"), tr("No captions found in that file."));
        return;
    }
    const SequenceSettings seq = m_project->data().sequence;
    QProgressDialog progress(tr("Creating captions…"), tr("Cancel"), 0, caps.size(), m_window);
    progress.setWindowModality(Qt::WindowModal);
    QVector<MediaItem> items;
    for (int i = 0; i < caps.size(); ++i) {
        progress.setValue(i);
        if (progress.wasCanceled())
            return;
        MediaItem m;
        QString err;
        if (!titles::renderTitleMedia(titles::makeTitle(titles::kCaptionPreset, QSize(seq.width, seq.height),
                                                        caps[i].text),
                                      tr("Caption %1").arg(i + 1), &m, &err)) {
            QMessageBox::warning(m_window, tr("Import Captions"), err);
            return;
        }
        m.titleDesign[QStringLiteral("caption")] = true;
        items.append(m);
    }
    progress.setValue(caps.size());
    m_project->edit(tr("Import Captions"), [&](ProjectData &d) {
        const int track = d.addTrack(TrackType::Video, tr("Captions"));
        for (int i = 0; i < caps.size(); ++i) {
            d.media.append(items[i]);
            timeline::placeMedia(d, items[i].id, track, caps[i].start, 0, caps[i].end - caps[i].start);
        }
    });
    emit status(tr("Imported %n caption(s) onto a new Captions track.", nullptr, caps.size()));
}

void EditTools::exportCaptions()
{
    const ProjectData &d = m_project->data();
    QVector<titles::Caption> caps;
    bool anyCaption = false;
    for (const auto &m : d.media)
        anyCaption |= m.titleDesign.contains(QStringLiteral("caption"));
    for (const auto &t : d.tracks) {
        if (!t.isVideo())
            continue;
        for (const auto &c : t.clips) {
            const MediaItem *m = d.mediaById(c.mediaId);
            if (!m || !m->isTitle() || (anyCaption && !m->titleDesign.contains(QStringLiteral("caption"))))
                continue;
            const QString text = titles::mainText(DesignDocument::fromJson(m->titleDesign));
            if (!text.trimmed().isEmpty())
                caps.append({c.start, c.end(), text});
        }
    }
    if (caps.isEmpty()) {
        QMessageBox::information(m_window, tr("Export Captions"),
                                 tr("There are no captions or titles on the timeline to export."));
        return;
    }
    std::sort(caps.begin(), caps.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
    QString path = QFileDialog::getSaveFileName(m_window, tr("Export Captions"), d.name + QStringLiteral(".srt"),
                                                tr("Subtitles (*.srt)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".srt")))
        path += QStringLiteral(".srt");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(m_window, tr("Export Captions"), f.errorString());
        return;
    }
    f.write(titles::toSrt(caps).toUtf8());
    emit status(tr("Exported %n caption(s) to %1", nullptr, caps.size()).arg(path));
}

void EditTools::recordVoiceover()
{
    const auto inputs = QMediaDevices::audioInputs();
    if (inputs.isEmpty()) {
        QMessageBox::warning(m_window, tr("Record Voiceover"), tr("No microphone was found."));
        return;
    }
    QDialog dlg(m_window);
    dlg.setWindowTitle(tr("Record Voiceover"));
    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *device = new QComboBox(&dlg);
    for (const auto &in : inputs)
        device->addItem(in.description());
    const QAudioDevice def = QMediaDevices::defaultAudioInput();
    for (int i = 0; i < inputs.size(); ++i) {
        if (inputs[i].id() == def.id())
            device->setCurrentIndex(i);
    }
    form->addRow(tr("Microphone"), device);
    auto *playAlong = new QCheckBox(tr("Play the timeline while recording (use headphones)"), &dlg);
    playAlong->setChecked(true);
    form->addRow(playAlong);
    auto *timeLabel = new QLabel(QStringLiteral("00:00"), &dlg);
    QFont big = timeLabel->font();
    big.setPointSizeF(big.pointSizeF() * 2.2);
    big.setBold(true);
    timeLabel->setFont(big);
    timeLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(timeLabel);
    auto *hint = new QLabel(tr("Recording starts at the playhead (%1) and is placed on an audio track when you stop.")
                                .arg(formatTimecode(m_player->position(), m_project->data().sequence.frameRate)),
                            &dlg);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *recordBtn = new QPushButton(tr("●  Record"), &dlg);
    recordBtn->setStyleSheet(QStringLiteral("QPushButton { color: #ff5050; font-weight: bold; padding: 8px; }"));
    layout->addWidget(recordBtn);

    QMediaCaptureSession session;
    QAudioInput input;
    session.setAudioInput(&input);
    QMediaRecorder recorder;
    session.setRecorder(&recorder);
    QMediaFormat fmt;
    const auto formats = fmt.supportedFileFormats(QMediaFormat::Encode);
    if (formats.contains(QMediaFormat::Wave)) {
        fmt.setFileFormat(QMediaFormat::Wave);
        fmt.setAudioCodec(QMediaFormat::AudioCodec::Wave);
    } else {
        fmt.setFileFormat(QMediaFormat::Mpeg4Audio);
        fmt.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    }
    recorder.setMediaFormat(fmt);
    recorder.setQuality(QMediaRecorder::VeryHighQuality);
    const QString ext = fmt.fileFormat() == QMediaFormat::Wave ? QStringLiteral("wav") : QStringLiteral("m4a");
    const QString path = mediaDir(m_project, QStringLiteral("Voiceovers"))
        + QStringLiteral("/voiceover-%1.%2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")), ext);
    recorder.setOutputLocation(QUrl::fromLocalFile(path));

    const TimeUs startAt = m_player->position();
    QTimer tick;
    tick.setInterval(200);
    connect(&tick, &QTimer::timeout, &dlg, [&] {
        const qint64 s = recorder.duration() / 1000;
        timeLabel->setText(QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0')));
    });
    QString recordError;
    connect(&recorder, &QMediaRecorder::errorOccurred, &dlg, [&](QMediaRecorder::Error, const QString &e) {
        recordError = e;
        dlg.reject();
    });
    bool recorded = false;
    connect(recordBtn, &QPushButton::clicked, &dlg, [&] {
        if (recorder.recorderState() == QMediaRecorder::RecordingState) {
            recorder.stop();
            tick.stop();
            m_player->pause();
            recorded = true;
            dlg.accept();
            return;
        }
        input.setDevice(inputs[device->currentIndex()]);
        device->setEnabled(false);
        playAlong->setEnabled(false);
        recorder.record();
        tick.start();
        if (playAlong->isChecked())
            m_player->play();
        recordBtn->setText(tr("■  Stop"));
    });
    dlg.exec();
    if (recorder.recorderState() == QMediaRecorder::RecordingState) {
        recorder.stop();
        m_player->pause();
    }
    if (!recordError.isEmpty()) {
        QMessageBox::warning(m_window, tr("Record Voiceover"), recordError);
        return;
    }
    if (!recorded)
        return;
    // Wait for the recorder to finalise the file.
    QElapsedTimer wait;
    wait.start();
    while (recorder.recorderState() != QMediaRecorder::StoppedState && wait.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QString actual = recorder.actualLocation().toLocalFile().isEmpty() ? path
                                                                             : recorder.actualLocation().toLocalFile();
    MediaItem item;
    QString err;
    if (!probeFile(actual, &item, &err)) {
        QMessageBox::warning(m_window, tr("Record Voiceover"), tr("The recording could not be read: %1").arg(err));
        return;
    }
    item.name = tr("Voiceover %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm")));
    m_project->edit(tr("Record Voiceover"), [&](ProjectData &d) {
        d.media.append(item);
        // A dedicated, ducking-friendly voice track.
        int track = -1;
        for (int ti : d.audioTrackIndices()) {
            if (d.tracks[ti].name == tr("Voiceover"))
                track = ti;
        }
        if (track < 0) {
            track = d.addTrack(TrackType::Audio, tr("Voiceover"));
            d.tracks[track].voice = true;
        }
        timeline::placeMedia(d, item.id, track, startAt);
    });
    emit status(tr("Voiceover recorded (%1).").arg(formatDuration(item.duration)));
}

void EditTools::insertAtPlayhead()
{
    const QUuid media = m_bin->currentMedia();
    if (media.isNull()) {
        emit status(tr("Select media in the Media Bin first."));
        return;
    }
    const TimeUs at = m_player->position();
    TimelineWidget *tl = timeline();
    const int track = tl ? tl->currentTrack() : -1;
    QUuid placed;
    m_project->edit(tr("Insert Clip"), [&](ProjectData &d) { placed = timeline::insertMedia(d, media, track, at); });
    if (tl && !placed.isNull())
        tl->setSelection({placed});
}

void EditTools::closeGaps()
{
    TimelineWidget *tl = timeline();
    int track = tl ? tl->currentTrack() : -1;
    const ProjectData &d = m_project->data();
    if (track < 0 || track >= d.tracks.size()) {
        const QVector<int> v = d.videoTrackIndices();
        track = v.isEmpty() ? -1 : v.first();
    }
    if (track < 0)
        return;
    int n = 0;
    m_project->edit(tr("Close Gaps"), [&](ProjectData &pd) { n = timeline::closeGaps(pd, track); });
    emit status(n ? tr("Closed gaps on %1.").arg(d.tracks[track].name) : tr("No gaps to close on %1.").arg(d.tracks[track].name));
}

void EditTools::autoReframe()
{
    const SequenceSettings seq = m_project->data().sequence;
    QDialog dlg(m_window);
    dlg.setWindowTitle(tr("Auto Reframe"));
    auto *form = new QFormLayout(&dlg);
    auto *aspect = new QComboBox(&dlg);
    aspect->addItem(tr("Vertical 9:16 (Shorts, Reels, TikTok)"), QSize(1080, 1920));
    aspect->addItem(tr("Square 1:1 (Instagram)"), QSize(1080, 1080));
    aspect->addItem(tr("Portrait 4:5 (Instagram feed)"), QSize(1080, 1350));
    aspect->addItem(tr("Widescreen 16:9 (YouTube)"), QSize(1920, 1080));
    form->addRow(tr("New format"), aspect);
    auto *fill = new QCheckBox(tr("Scale clips to fill the frame (crop the sides)"), &dlg);
    fill->setChecked(true);
    form->addRow(fill);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QSize frame = aspect->currentData().toSize();
    m_project->edit(tr("Auto Reframe"), [&](ProjectData &d) {
        d.sequence.width = frame.width();
        d.sequence.height = frame.height();
        for (auto &t : d.tracks) {
            if (!t.isVideo())
                continue;
            for (auto &c : t.clips) {
                const MediaItem *m = d.mediaById(c.mediaId);
                if (!m || m->isTitle() || m->resolution.isEmpty())
                    continue;
                if (fill->isChecked()) {
                    const QSize fit = fitSize(m->resolution, frame);
                    c.transform.scale = std::max(double(frame.width()) / fit.width(),
                                                 double(frame.height()) / fit.height());
                } else {
                    c.transform.scale = 1.0;
                }
                c.transform.x = 0;
                c.transform.y = 0;
            }
        }
    });
    emit status(tr("Sequence reframed to %1×%2. Fine-tune positions with Position X in the Inspector.")
                    .arg(frame.width())
                    .arg(frame.height()));
    Q_UNUSED(seq)
}

} // namespace spotlight

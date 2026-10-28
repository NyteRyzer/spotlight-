#include "ui/exportdialog.h"

#include "core/exporter.h"
#include "core/project.h"
#include "core/timelineops.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace spotlight {

ExportDialog::ExportDialog(Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_exporter(new Exporter(this))
    , m_preset(new QComboBox(this))
    , m_width(new QSpinBox(this))
    , m_height(new QSpinBox(this))
    , m_fps(new QDoubleSpinBox(this))
    , m_crf(new QSpinBox(this))
    , m_audioKbps(new QSpinBox(this))
    , m_loudness(new QComboBox(this))
    , m_range(new QComboBox(this))
    , m_path(new QLineEdit(this))
    , m_progress(new QProgressBar(this))
    , m_info(new QLabel(this))
    , m_start(new QPushButton(tr("Export"), this))
    , m_cancel(new QPushButton(tr("Close"), this))
{
    setWindowTitle(tr("Export Video / Audio"));
    setMinimumWidth(520);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(m_preset, 1);
    auto *savePresetBtn = new QPushButton(tr("Save as Preset…"), this);
    presetRow->addWidget(savePresetBtn);
    form->addRow(tr("Preset"), presetRow);

    m_width->setRange(16, 7680);
    m_height->setRange(16, 4320);
    m_fps->setRange(1, 240);
    m_crf->setRange(0, 51);
    m_crf->setToolTip(tr("Lower = higher quality and bigger file"));
    m_audioKbps->setRange(0, 512);
    m_audioKbps->setSuffix(tr(" kbps"));
    auto *res = new QHBoxLayout;
    res->addWidget(m_width);
    res->addWidget(new QLabel(QStringLiteral("×"), this));
    res->addWidget(m_height);
    form->addRow(tr("Resolution"), res);
    form->addRow(tr("Frame rate"), m_fps);
    form->addRow(tr("Quality (CRF)"), m_crf);
    form->addRow(tr("Audio bitrate"), m_audioKbps);
    m_loudness->addItem(tr("Off (keep levels as mixed)"), 0.0);
    m_loudness->addItem(tr("YouTube / TikTok / Instagram  (−14 LUFS)"), -14.0);
    m_loudness->addItem(tr("Podcast / Spotify  (−16 LUFS)"), -16.0);
    m_loudness->addItem(tr("Broadcast TV  (−23 LUFS)"), -23.0);
    m_loudness->setToolTip(tr("Normalize the final mix to the loudness the platform plays at, "
                              "so your video isn't quieter or louder than everything else."));
    form->addRow(tr("Loudness"), m_loudness);
    const ProjectData &pd = m_project->data();
    m_range->addItem(tr("Entire timeline (%1)").arg(formatDuration(pd.duration())));
    if (pd.hasRange()) {
        m_range->addItem(tr("In to Out (%1)").arg(formatDuration(pd.outPoint - pd.inPoint)));
        m_range->setCurrentIndex(1);
    } else {
        m_range->setToolTip(tr("Mark In (I) and Out (O) on the timeline to export just a part"));
    }
    form->addRow(tr("Range"), m_range);

    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    auto *browseBtn = new QPushButton(tr("Browse…"), this);
    pathRow->addWidget(browseBtn);
    form->addRow(tr("Output file"), pathRow);
    layout->addLayout(form);

    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color: #9a9aa6;"));
    layout->addWidget(m_info);
    layout->addWidget(m_progress);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_start);
    m_start->setObjectName(QStringLiteral("primary"));
    layout->addLayout(buttons);

    m_progress->setRange(0, 1000);
    m_progress->setValue(0);

    loadPresets();
    connect(m_preset, &QComboBox::currentIndexChanged, this, &ExportDialog::applyPreset);
    connect(browseBtn, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(savePresetBtn, &QPushButton::clicked, this, &ExportDialog::savePreset);
    connect(m_start, &QPushButton::clicked, this, &ExportDialog::start);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (m_exporter->isRunning())
            m_exporter->cancel();
        else
            reject();
    });
    connect(m_exporter, &Exporter::progress, this, [this](double f) { m_progress->setValue(int(f * 1000)); });
    connect(m_exporter, &Exporter::finished, this, [this](bool ok, const QString &msg) {
        m_start->setEnabled(true);
        m_cancel->setText(tr("Close"));
        m_info->setText(ok ? msg : tr("Export failed."));
        if (!ok && !msg.contains(QLatin1String("cancelled")))
            QMessageBox::warning(this, tr("Export Failed"), msg);
    });

    const QString base = m_project->displayName();
    const QString dir = QSettings().value(QStringLiteral("paths/export"),
                                          QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                            .toString();
    m_path->setText(QDir(dir).filePath(base + QLatin1Char('.') + Exporter::extensionFor(m_presets.value(0))));
    applyPreset(0);

    if (Exporter::findFfmpeg().isEmpty())
        m_info->setText(tr("FFmpeg was not found. Install it (macOS: brew install ffmpeg; Linux: your package "
                           "manager) to export."));
    else
        m_info->setText(tr("Timeline length: %1").arg(formatDuration(m_project->data().duration())));
}

QString ExportDialog::userPresetFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/export-presets.json");
}

void ExportDialog::loadPresets()
{
    m_presets = builtInExportPresets() + loadUserExportPresets(userPresetFile());
    m_preset->clear();
    for (const auto &p : m_presets)
        m_preset->addItem(p.builtIn ? p.name : tr("★ %1").arg(p.name));
}

void ExportDialog::applyPreset(int index)
{
    if (index < 0 || index >= m_presets.size())
        return;
    const ExportPreset &p = m_presets[index];
    const bool video = !p.isAudioOnly();
    for (QWidget *w : {static_cast<QWidget *>(m_width), static_cast<QWidget *>(m_height),
                       static_cast<QWidget *>(m_fps), static_cast<QWidget *>(m_crf)})
        w->setEnabled(video);
    m_width->setValue(p.width);
    m_height->setValue(p.height);
    m_fps->setValue(p.frameRate);
    m_crf->setValue(p.crf);
    m_audioKbps->setValue(p.audioBitrateKbps);
    int li = 0;
    for (int i = 0; i < m_loudness->count(); ++i) {
        if (qFuzzyCompare(1 + m_loudness->itemData(i).toDouble(), 1 + p.loudnessLufs))
            li = i;
    }
    m_loudness->setCurrentIndex(li);
    // Swap the extension to match the container.
    QFileInfo fi(m_path->text());
    if (!m_path->text().isEmpty())
        m_path->setText(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QLatin1Char('.')
                        + Exporter::extensionFor(p));
}

ExportPreset ExportDialog::currentSettings() const
{
    ExportPreset p = m_presets.value(m_preset->currentIndex());
    p.width = m_width->value();
    p.height = m_height->value();
    p.frameRate = m_fps->value();
    p.crf = m_crf->value();
    p.audioBitrateKbps = m_audioKbps->value();
    p.loudnessLufs = m_loudness->currentData().toDouble();
    return p;
}

void ExportDialog::browse()
{
    const ExportPreset p = currentSettings();
    const QString ext = Exporter::extensionFor(p);
    QString path = QFileDialog::getSaveFileName(this, tr("Export To"), m_path->text(),
                                                tr("%1 files (*.%2)").arg(ext.toUpper(), ext));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QLatin1Char('.') + ext;
    m_path->setText(path);
}

void ExportDialog::start()
{
    const QString path = m_path->text();
    if (path.isEmpty()) {
        browse();
        return;
    }
    QSettings().setValue(QStringLiteral("paths/export"), QFileInfo(path).absolutePath());
    QString err;
    m_progress->setValue(0);
    const ProjectData &pd = m_project->data();
    const ProjectData data = m_range->currentIndex() == 1 && pd.hasRange()
        ? timeline::rangeProject(pd, pd.inPoint, pd.outPoint)
        : pd;
    if (!m_exporter->start(data, currentSettings(), path, &err)) {
        QMessageBox::warning(this, tr("Cannot Export"), err);
        return;
    }
    m_start->setEnabled(false);
    m_cancel->setText(tr("Cancel"));
    m_info->setText(tr("Rendering…"));
}

void ExportDialog::savePreset()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save Preset"), tr("Preset name:"), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    ExportPreset p = currentSettings();
    p.name = name.trimmed();
    p.builtIn = false;
    QVector<ExportPreset> user = loadUserExportPresets(userPresetFile());
    user.erase(std::remove_if(user.begin(), user.end(), [&](const ExportPreset &u) { return u.name == p.name; }),
               user.end());
    user.append(p);
    saveUserExportPresets(userPresetFile(), user);
    loadPresets();
    m_preset->setCurrentIndex(m_presets.size() - 1);
}

} // namespace spotlight

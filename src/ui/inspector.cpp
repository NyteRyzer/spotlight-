#include "ui/inspector.h"

#include "core/project.h"
#include "media/waveformcache.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace spotlight {

namespace {
QSlider *makeSlider(int min, int max, QWidget *parent)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    s->setRange(min, max);
    return s;
}
} // namespace

ClipInspector::ClipInspector(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_title(new QLabel(this))
    , m_info(new QLabel(this))
    , m_audioBox(new QGroupBox(tr("Audio"), this))
    , m_colorBox(new QGroupBox(tr("Color"), this))
    , m_fadeBox(new QGroupBox(tr("Fades"), this))
    , m_gainDb(new QDoubleSpinBox(this))
    , m_fadeIn(new QDoubleSpinBox(this))
    , m_fadeOut(new QDoubleSpinBox(this))
    , m_brightness(makeSlider(-100, 100, this))
    , m_contrast(makeSlider(0, 200, this))
    , m_saturation(makeSlider(0, 300, this))
{
    auto *layout = new QVBoxLayout(this);
    QFont tf = m_title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.1);
    m_title->setFont(tf);
    m_title->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color: #8a8a96;"));
    layout->addWidget(m_title);
    layout->addWidget(m_info);

    m_gainDb->setRange(-60, 24);
    m_gainDb->setSuffix(QStringLiteral(" dB"));
    m_gainDb->setSingleStep(0.5);
    auto *af = new QFormLayout(m_audioBox);
    af->addRow(tr("Clip gain"), m_gainDb);
    auto *normalizeBtn = new QPushButton(tr("Normalize (peak −1 dB)"), m_audioBox);
    normalizeBtn->setToolTip(tr("Set the clip gain so its loudest moment peaks at −1 dB"));
    af->addRow(normalizeBtn);
    connect(normalizeBtn, &QPushButton::clicked, this, [this] {
        if (!normalize()) {
            m_pendingNormalize = m_clip;
            m_info->setText(tr("Analyzing audio… the clip will be normalized when it's done."));
        }
    });
    connect(WaveformCache::instance(), &WaveformCache::ready, this, [this] {
        if (!m_pendingNormalize.isNull() && m_pendingNormalize == m_clip && normalize())
            m_pendingNormalize = QUuid();
    });
    layout->addWidget(m_audioBox);

    for (auto *s : {m_fadeIn, m_fadeOut}) {
        s->setRange(0, 30);
        s->setSuffix(QStringLiteral(" s"));
        s->setSingleStep(0.25);
        s->setDecimals(2);
    }
    auto *ff = new QFormLayout(m_fadeBox);
    ff->addRow(tr("Fade in"), m_fadeIn);
    ff->addRow(tr("Fade out"), m_fadeOut);
    layout->addWidget(m_fadeBox);

    auto *cf = new QFormLayout(m_colorBox);
    cf->addRow(tr("Brightness"), m_brightness);
    cf->addRow(tr("Contrast"), m_contrast);
    cf->addRow(tr("Saturation"), m_saturation);
    auto *reset = new QPushButton(tr("Reset Color"), m_colorBox);
    cf->addRow(reset);
    layout->addWidget(m_colorBox);
    layout->addStretch();

    connect(m_gainDb, &QDoubleSpinBox::valueChanged, this, [this] { apply(QStringLiteral("gain")); });
    connect(m_fadeIn, &QDoubleSpinBox::valueChanged, this, [this] { apply(QStringLiteral("fade")); });
    connect(m_fadeOut, &QDoubleSpinBox::valueChanged, this, [this] { apply(QStringLiteral("fade")); });
    for (auto *s : {m_brightness, m_contrast, m_saturation})
        connect(s, &QSlider::valueChanged, this, [this] { apply(QStringLiteral("color")); });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_project->edit(tr("Reset Color"), [&](ProjectData &d) {
            if (Clip *c = d.clipById(m_clip))
                c->color = ColorAdjust();
        });
    });
    connect(project, &Project::changed, this, &ClipInspector::refresh);
    refresh();
}

void ClipInspector::setClip(const QUuid &clipId)
{
    m_clip = clipId;
    refresh();
}

void ClipInspector::refresh()
{
    int ti = -1;
    const Clip *c = m_project->data().clipById(m_clip, &ti);
    const bool has = c != nullptr;
    m_audioBox->setVisible(false);
    m_colorBox->setVisible(false);
    m_fadeBox->setVisible(has);
    if (!has) {
        m_title->setText(tr("No clip selected"));
        m_info->setText(tr("Select a clip on the timeline to adjust it."));
        return;
    }
    const ProjectData &d = m_project->data();
    const Track &t = d.tracks[ti];
    const MediaItem *m = d.mediaById(c->mediaId);
    m_title->setText(m ? m->name : tr("Clip"));
    m_info->setText(tr("%1 · starts %2 · length %3")
                        .arg(t.name, formatTimecode(c->start, d.sequence.frameRate), formatDuration(c->duration())));
    m_audioBox->setVisible(t.isAudio());
    m_colorBox->setVisible(t.isVideo());
    m_fadeBox->setTitle(t.isVideo() ? tr("Dip to Black") : tr("Fades"));

    m_updating = true;
    m_gainDb->setValue(c->gain > 0 ? 20.0 * std::log10(c->gain) : -60.0);
    m_fadeIn->setValue(usToSeconds(c->fadeIn));
    m_fadeOut->setValue(usToSeconds(c->fadeOut));
    m_brightness->setValue(int(std::lround(c->color.brightness * 100)));
    m_contrast->setValue(int(std::lround(c->color.contrast * 100)));
    m_saturation->setValue(int(std::lround(c->color.saturation * 100)));
    m_updating = false;
}

bool ClipInspector::normalize()
{
    const Clip *c = m_project->data().clipById(m_clip);
    const MediaItem *m = c ? m_project->data().mediaById(c->mediaId) : nullptr;
    if (!c || !m || !m->hasAudio)
        return true; // nothing to do
    const QVector<float> *peaks = WaveformCache::instance()->peaks(m->path);
    if (!peaks)
        return false;
    const qsizetype b0 = qsizetype(c->in * WaveformCache::kPeaksPerSecond / kUsPerSecond);
    const qsizetype b1 = qsizetype(c->out * WaveformCache::kPeaksPerSecond / kUsPerSecond);
    float peak = 0;
    for (qsizetype b = std::max<qsizetype>(0, b0); b < std::min(b1 + 1, peaks->size()); ++b)
        peak = std::max(peak, (*peaks)[b]);
    if (peak <= 0.0001f) {
        m_info->setText(tr("This clip is silent — nothing to normalize."));
        return true;
    }
    const double target = std::pow(10.0, -1.0 / 20.0); // -1 dBFS
    const double gain = std::clamp(target / peak, 0.0, std::pow(10.0, 24.0 / 20.0));
    const QUuid id = m_clip;
    m_project->edit(tr("Normalize Clip"), [&](ProjectData &d) {
        if (Clip *cl = d.clipById(id))
            cl->gain = gain;
    });
    return true;
}

void ClipInspector::apply(const QString &what)
{
    if (m_updating || m_clip.isNull())
        return;
    const QString key = what + m_clip.toString();
    m_project->edit(
        tr("Adjust Clip"),
        [&](ProjectData &d) {
            Clip *c = d.clipById(m_clip);
            if (!c)
                return;
            if (what == QLatin1String("gain"))
                c->gain = m_gainDb->value() <= -60.0 ? 0.0 : std::pow(10.0, m_gainDb->value() / 20.0);
            if (what == QLatin1String("fade")) {
                const TimeUs len = c->duration();
                c->fadeIn = std::min(secondsToUs(m_fadeIn->value()), len);
                c->fadeOut = std::min(secondsToUs(m_fadeOut->value()), len - c->fadeIn);
            }
            if (what == QLatin1String("color")) {
                c->color.brightness = m_brightness->value() / 100.0;
                c->color.contrast = m_contrast->value() / 100.0;
                c->color.saturation = m_saturation->value() / 100.0;
            }
        },
        key);
}

} // namespace spotlight

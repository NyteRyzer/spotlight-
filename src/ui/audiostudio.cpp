#include "ui/audiostudio.h"

#include "core/project.h"
#include "media/timelineplayer.h"
#include "ui/inspector.h"
#include "ui/mediabin.h"
#include "ui/theme.h"
#include "ui/timelinewidget.h"

#include <QCheckBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace spotlight {

namespace {

double linearToDb(double v)
{
    return v > 0 ? 20.0 * std::log10(v) : -60.0;
}

double dbToLinear(double db)
{
    return db <= -60.0 ? 0.0 : std::pow(10.0, db / 20.0);
}

QToolButton *toggle(const QString &text, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(true);
    b->setFixedWidth(30);
    return b;
}

} // namespace

// ---------------------------------------------------------------------------

ChannelStrip::ChannelStrip(Project *project, const QUuid &trackId, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_trackId(trackId)
    , m_name(new QLabel(this))
    , m_db(new QLabel(this))
    , m_volume(new QSlider(Qt::Vertical, this))
    , m_pan(new QSlider(Qt::Horizontal, this))
    , m_mute(toggle(QStringLiteral("M"), tr("Mute"), this))
    , m_solo(toggle(QStringLiteral("S"), tr("Solo"), this))
    , m_fx(new QToolButton(this))
{
    setFixedWidth(96);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, theme::panelAlt);
    setPalette(pal);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    m_name->setAlignment(Qt::AlignCenter);
    QFont f = m_name->font();
    f.setBold(true);
    m_name->setFont(f);
    layout->addWidget(m_name);

    m_fx->setText(tr("FX"));
    m_fx->setToolTip(tr("Edit EQ, compressor and cleanup for this track"));
    layout->addWidget(m_fx, 0, Qt::AlignHCenter);

    m_pan->setRange(-100, 100);
    m_pan->setToolTip(tr("Pan (double-click to center)"));
    layout->addWidget(new QLabel(tr("Pan"), this), 0, Qt::AlignHCenter);
    layout->addWidget(m_pan);

    m_volume->setRange(-600, 120); // tenths of dB
    m_volume->setMinimumHeight(110);
    layout->addWidget(m_volume, 1, Qt::AlignHCenter);
    m_db->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_db);

    auto *ms = new QHBoxLayout;
    ms->addWidget(m_mute);
    ms->addWidget(m_solo);
    layout->addLayout(ms);

    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        m_db->setText(v <= -600 ? QStringLiteral("-∞ dB") : QStringLiteral("%1 dB").arg(v / 10.0, 0, 'f', 1));
        edit(QStringLiteral("volume"), [v](Track &t) { t.volume = dbToLinear(v / 10.0); });
    });
    connect(m_pan, &QSlider::valueChanged, this,
            [this](int v) { edit(QStringLiteral("pan"), [v](Track &t) { t.pan = v / 100.0; }); });
    connect(m_mute, &QToolButton::toggled, this,
            [this](bool on) { edit(QString(), [on](Track &t) { t.muted = on; }); });
    connect(m_solo, &QToolButton::toggled, this,
            [this](bool on) { edit(QString(), [on](Track &t) { t.solo = on; }); });
    connect(m_fx, &QToolButton::clicked, this, [this] { emit selectTrack(m_trackId); });
    refresh();
}

void ChannelStrip::edit(const QString &what, const std::function<void(Track &)> &fn)
{
    if (m_updating)
        return;
    m_project->edit(
        tr("Mixer"),
        [&](ProjectData &d) {
            const int i = d.trackIndex(m_trackId);
            if (i >= 0)
                fn(d.tracks[i]);
        },
        what.isEmpty() ? QString() : what + m_trackId.toString());
}

void ChannelStrip::refresh()
{
    const int i = m_project->data().trackIndex(m_trackId);
    if (i < 0)
        return;
    const Track &t = m_project->data().tracks[i];
    m_updating = true;
    m_name->setText(t.name);
    if (!m_volume->isSliderDown())
        m_volume->setValue(int(std::lround(linearToDb(t.volume) * 10)));
    m_db->setText(t.volume <= 0 ? QStringLiteral("-∞ dB") : QStringLiteral("%1 dB").arg(linearToDb(t.volume), 0, 'f', 1));
    if (!m_pan->isSliderDown())
        m_pan->setValue(int(std::lround(t.pan * 100)));
    m_mute->setChecked(t.muted);
    m_solo->setChecked(t.solo);
    m_fx->setStyleSheet(t.fx.isNeutral() ? QString() : QStringLiteral("color: #ffc400; font-weight: bold;"));
    m_updating = false;
}

// ---------------------------------------------------------------------------

TrackFxPanel::TrackFxPanel(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_title(new QLabel(this))
    , m_cleanup(new QCheckBox(tr("Dialogue cleanup (rumble + noise reduction)"), this))
    , m_eq(new QGroupBox(tr("3-Band EQ"), this))
    , m_low(new QSlider(Qt::Horizontal, this))
    , m_mid(new QSlider(Qt::Horizontal, this))
    , m_high(new QSlider(Qt::Horizontal, this))
    , m_comp(new QGroupBox(tr("Compressor"), this))
    , m_threshold(new QSlider(Qt::Horizontal, this))
    , m_ratio(new QSlider(Qt::Horizontal, this))
    , m_values(new QLabel(this))
{
    auto *layout = new QVBoxLayout(this);
    QFont f = m_title->font();
    f.setBold(true);
    m_title->setFont(f);
    layout->addWidget(m_title);
    layout->addWidget(m_cleanup);

    m_eq->setCheckable(true);
    auto *eqf = new QFormLayout(m_eq);
    for (auto *s : {m_low, m_mid, m_high})
        s->setRange(-120, 120); // tenths of dB
    eqf->addRow(tr("Low (100 Hz)"), m_low);
    eqf->addRow(tr("Mid (1 kHz)"), m_mid);
    eqf->addRow(tr("High (8 kHz)"), m_high);
    layout->addWidget(m_eq);

    m_comp->setCheckable(true);
    auto *cf = new QFormLayout(m_comp);
    m_threshold->setRange(-50, 0);
    m_ratio->setRange(10, 200); // ratio x10
    cf->addRow(tr("Threshold"), m_threshold);
    cf->addRow(tr("Ratio"), m_ratio);
    layout->addWidget(m_comp);

    m_values->setWordWrap(true);
    m_values->setStyleSheet(QStringLiteral("color: #9a9aa6;"));
    layout->addWidget(m_values);
    auto *note = new QLabel(tr("Track effects are rendered on export. The preview plays the dry signal."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #6f6f7a; font-style: italic;"));
    layout->addWidget(note);
    layout->addStretch();

    connect(m_cleanup, &QCheckBox::toggled, this, &TrackFxPanel::apply);
    connect(m_eq, &QGroupBox::toggled, this, &TrackFxPanel::apply);
    connect(m_comp, &QGroupBox::toggled, this, &TrackFxPanel::apply);
    for (auto *s : {m_low, m_mid, m_high, m_threshold, m_ratio})
        connect(s, &QSlider::valueChanged, this, &TrackFxPanel::apply);
    connect(project, &Project::changed, this, &TrackFxPanel::refresh);
    refresh();
}

void TrackFxPanel::setTrack(const QUuid &trackId)
{
    m_trackId = trackId;
    refresh();
}

void TrackFxPanel::refresh()
{
    const int i = m_project->data().trackIndex(m_trackId);
    const bool ok = i >= 0 && m_project->data().tracks[i].isAudio();
    setEnabled(ok);
    if (!ok) {
        m_title->setText(tr("Select an audio track (click its header or FX button)"));
        return;
    }
    const AudioFx &fx = m_project->data().tracks[i].fx;
    m_updating = true;
    m_title->setText(tr("Effects: %1").arg(m_project->data().tracks[i].name));
    m_cleanup->setChecked(fx.dialogueCleanup);
    m_eq->setChecked(fx.eqEnabled);
    m_low->setValue(int(std::lround(fx.lowGainDb * 10)));
    m_mid->setValue(int(std::lround(fx.midGainDb * 10)));
    m_high->setValue(int(std::lround(fx.highGainDb * 10)));
    m_comp->setChecked(fx.compressorEnabled);
    m_threshold->setValue(int(std::lround(fx.thresholdDb)));
    m_ratio->setValue(int(std::lround(fx.ratio * 10)));
    m_values->setText(tr("EQ  %1 / %2 / %3 dB   ·   Comp  %4 dB, %5:1")
                          .arg(fx.lowGainDb, 0, 'f', 1)
                          .arg(fx.midGainDb, 0, 'f', 1)
                          .arg(fx.highGainDb, 0, 'f', 1)
                          .arg(fx.thresholdDb, 0, 'f', 0)
                          .arg(fx.ratio, 0, 'f', 1));
    m_updating = false;
}

void TrackFxPanel::apply()
{
    if (m_updating)
        return;
    m_project->edit(
        tr("Track Effects"),
        [&](ProjectData &d) {
            const int i = d.trackIndex(m_trackId);
            if (i < 0)
                return;
            AudioFx &fx = d.tracks[i].fx;
            fx.dialogueCleanup = m_cleanup->isChecked();
            fx.eqEnabled = m_eq->isChecked();
            fx.lowGainDb = m_low->value() / 10.0;
            fx.midGainDb = m_mid->value() / 10.0;
            fx.highGainDb = m_high->value() / 10.0;
            fx.compressorEnabled = m_comp->isChecked();
            fx.thresholdDb = m_threshold->value();
            fx.ratio = m_ratio->value() / 10.0;
        },
        QStringLiteral("fx") + m_trackId.toString());
}

// ---------------------------------------------------------------------------

AudioStudio::AudioStudio(Project *project, TimelinePlayer *player, MediaBin *bin, QWidget *parent)
    : QMainWindow(parent)
    , m_project(project)
    , m_player(player)
    , m_timeline(new TimelineWidget(project, TimelineWidget::Mode::AudioOnly, this))
    , m_inspector(new ClipInspector(project, this))
    , m_fx(new TrackFxPanel(project, this))
    , m_mixer(new QWidget(this))
    , m_mixerLayout(new QHBoxLayout(m_mixer))
    , m_time(new QLabel(this))
    , m_playBtn(new QToolButton(this))
{
    setObjectName(QStringLiteral("AudioStudio"));
    setWindowFlags(Qt::Widget);
    setDockNestingEnabled(true);
    setCentralWidget(m_timeline);

    auto *transport = new QToolBar(tr("Transport"), this);
    transport->setObjectName(QStringLiteral("audio.transport"));
    transport->setMovable(false);
    m_playBtn->setText(QStringLiteral("▶"));
    m_playBtn->setToolTip(tr("Play / Pause (Space)"));
    transport->addAction(QStringLiteral("⏮"), this, [this] { m_player->seek(0); });
    transport->addWidget(m_playBtn);
    transport->addSeparator();
    transport->addWidget(m_time);
    transport->addSeparator();
    transport->addAction(tr("Import Audio…"), bin, &MediaBin::importDialog);
    transport->addAction(tr("Zoom In"), m_timeline, &TimelineWidget::zoomIn);
    transport->addAction(tr("Zoom Out"), m_timeline, &TimelineWidget::zoomOut);
    addToolBar(Qt::TopToolBarArea, transport);

    auto makeDock = [this](const QString &title, const QString &name, QWidget *w) {
        auto *d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(w);
        return d;
    };
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(m_mixer);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(290);
    m_mixerLayout->setContentsMargins(6, 6, 6, 6);
    m_mixerLayout->addStretch();
    auto *mixerDock = makeDock(tr("Mixer"), QStringLiteral("audio.mixer"), scroll);
    auto *fxDock = makeDock(tr("Track Effects"), QStringLiteral("audio.fx"), m_fx);
    auto *clipDock = makeDock(tr("Clip"), QStringLiteral("audio.clip"), m_inspector);
    addDockWidget(Qt::BottomDockWidgetArea, mixerDock);
    addDockWidget(Qt::RightDockWidgetArea, fxDock);
    addDockWidget(Qt::RightDockWidgetArea, clipDock);
    tabifyDockWidget(fxDock, clipDock);
    fxDock->raise();
    resizeDocks({mixerDock}, {280}, Qt::Vertical);
    resizeDocks({fxDock}, {320}, Qt::Horizontal);

    connect(m_playBtn, &QToolButton::clicked, player, &TimelinePlayer::togglePlay);
    connect(player, &TimelinePlayer::playingChanged, this,
            [this](bool p) { m_playBtn->setText(p ? QStringLiteral("❚❚") : QStringLiteral("▶")); });
    auto updateTime = [this] {
        const double fps = m_project->data().sequence.frameRate;
        m_time->setText(QStringLiteral("  %1 / %2  ").arg(formatTimecode(m_player->position(), fps),
                                                         formatTimecode(m_project->data().duration(), fps)));
    };
    connect(player, &TimelinePlayer::positionChanged, this, updateTime);
    connect(project, &Project::changed, this, updateTime);
    updateTime();

    connect(m_timeline, &TimelineWidget::seekRequested, player, &TimelinePlayer::seek);
    connect(player, &TimelinePlayer::positionChanged, m_timeline, &TimelineWidget::setPlayhead);
    connect(m_timeline, &TimelineWidget::filesDropped, bin, &MediaBin::importFiles);
    connect(m_timeline, &TimelineWidget::selectionChanged, this, [this, clipDock] {
        const auto sel = m_timeline->selection();
        m_inspector->setClip(sel.isEmpty() ? QUuid() : sel.first());
        if (!sel.isEmpty())
            clipDock->raise();
    });
    connect(m_timeline, &TimelineWidget::currentTrackChanged, this, [this, fxDock](int ti) {
        if (ti >= 0 && ti < m_project->data().tracks.size()) {
            m_fx->setTrack(m_project->data().tracks[ti].id);
            fxDock->raise();
        }
    });

    connect(project, &Project::changed, this, &AudioStudio::rebuildMixer);
    rebuildMixer();
    const QVector<int> audio = project->data().audioTrackIndices();
    if (!audio.isEmpty())
        m_fx->setTrack(project->data().tracks[audio.first()].id);
}

void AudioStudio::rebuildMixer()
{
    const ProjectData &d = m_project->data();
    QVector<QUuid> ids;
    for (int ti : d.audioTrackIndices())
        ids.append(d.tracks[ti].id);

    QVector<QUuid> existing;
    for (auto *s : m_strips)
        existing.append(s->trackId());
    if (ids == existing) {
        for (auto *s : m_strips)
            s->refresh();
        return;
    }

    for (auto *s : m_strips)
        s->deleteLater();
    m_strips.clear();
    for (int i = 0; i < ids.size(); ++i) {
        auto *strip = new ChannelStrip(m_project, ids[i], m_mixer);
        connect(strip, &ChannelStrip::selectTrack, m_fx, &TrackFxPanel::setTrack);
        m_mixerLayout->insertWidget(i, strip);
        m_strips.append(strip);
    }
}

} // namespace spotlight

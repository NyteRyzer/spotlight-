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
#include <QGridLayout>
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
    m_fx->setToolTip(tr("Edit this track's effects: cleanup, EQ, compressor, voice changer, reverb, ducking"));
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
    if (!m_volume->isSliderDown())
        m_volume->setValue(int(std::lround(linearToDb(t.volume) * 10)));
    m_db->setText(t.volume <= 0 ? QStringLiteral("-∞ dB") : QStringLiteral("%1 dB").arg(linearToDb(t.volume), 0, 'f', 1));
    if (!m_pan->isSliderDown())
        m_pan->setValue(int(std::lround(t.pan * 100)));
    m_mute->setChecked(t.muted);
    m_solo->setChecked(t.solo);
    m_fx->setStyleSheet(t.fx.isNeutral() && !t.fx.duckUnderVoice ? QString()
                                                                  : QStringLiteral("color: #ffc400; font-weight: bold;"));
    m_name->setText(t.voice ? QStringLiteral("🎙 %1").arg(t.name) : t.name);
    m_updating = false;
}

// ---------------------------------------------------------------------------

TrackFxPanel::TrackFxPanel(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_title(new QLabel(this))
    , m_body(new QWidget(this))
{
    setMinimumWidth(330);
    auto *outer = new QVBoxLayout(this);
    QFont f = m_title->font();
    f.setBold(true);
    m_title->setFont(f);
    m_title->setWordWrap(true);
    outer->addWidget(m_title);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_body);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll, 1);

    auto *page = new QVBoxLayout(m_body);
    auto section = [&](const QString &title) {
        auto *label = new QLabel(title, m_body);
        QFont lf = label->font();
        lf.setBold(true);
        label->setFont(lf);
        label->setStyleSheet(QStringLiteral("color: #ffc400; margin-top: 6px;"));
        page->addWidget(label);
        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        page->addLayout(form);
        return form;
    };

    // One-click presets.
    auto *presets = section(tr("Presets"));
    auto *presetRow = new QWidget(m_body);
    auto *grid = new QGridLayout(presetRow);
    grid->setContentsMargins(0, 0, 0, 0);
    struct Preset {
        QString name;
        QString tip;
        std::function<void(Track &)> apply;
    };
    const QVector<Preset> list{
        {tr("Podcast Voice"), tr("Cleanup, gate, de-esser, warm EQ, compression and a limiter"),
         [](Track &t) {
             AudioFx &x = t.fx;
             x.dialogueCleanup = x.gateEnabled = x.deEsser = x.eqEnabled = x.compressorEnabled = x.limiterEnabled = true;
             x.lowGainDb = 2;
             x.midGainDb = 1.5;
             x.highGainDb = 2.5;
             x.thresholdDb = -20;
             x.ratio = 4;
             x.limiterCeilingDb = -1;
             t.voice = true;
         }},
        {tr("Music Bed"), tr("Duck under voice tracks and carve room for dialogue"),
         [](Track &t) {
             t.fx.duckUnderVoice = true;
             t.fx.duckAmountDb = -14;
             t.fx.eqEnabled = true;
             t.fx.midGainDb = -3;
             t.voice = false;
         }},
        {tr("Deep Voice"), tr("Pitch down 4 semitones"), [](Track &t) { t.fx.pitchSemitones = -4; }},
        {tr("Chipmunk"), tr("Pitch up 7 semitones"), [](Track &t) { t.fx.pitchSemitones = 7; }},
        {tr("Big Hall"), tr("Large, wet reverb"),
         [](Track &t) {
             t.fx.reverbEnabled = true;
             t.fx.reverbMix = 0.45;
             t.fx.reverbSize = 0.9;
         }},
        {tr("Phone Call"), tr("Narrow, lo-fi telephone voice"), [](Track &t) { t.fx.telephone = true; }},
        {tr("Underwater"), tr("Muffled: low-pass at 600 Hz plus reverb"),
         [](Track &t) {
             t.fx.lowpassHz = 600;
             t.fx.reverbEnabled = true;
             t.fx.reverbMix = 0.3;
         }},
        {tr("Clear All"), tr("Remove every effect from this track"), [](Track &t) {
             t.fx = AudioFx();
             t.voice = false;
         }},
    };
    for (int i = 0; i < list.size(); ++i) {
        auto *b = new QToolButton(presetRow);
        b->setText(list[i].name);
        b->setToolTip(list[i].tip);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        grid->addWidget(b, i / 2, i % 2);
        const auto fn = list[i].apply;
        connect(b, &QToolButton::clicked, this, [this, fn] { editFx(QString(), fn); });
    }
    presets->addRow(presetRow);

    auto *clean = section(tr("Cleanup"));
    check(clean, tr("Dialogue cleanup (denoise)"), [](const Track &t) { return t.fx.dialogueCleanup; },
          [](Track &t, bool on) { t.fx.dialogueCleanup = on; });
    check(clean, tr("Noise gate"), [](const Track &t) { return t.fx.gateEnabled; },
          [](Track &t, bool on) { t.fx.gateEnabled = on; });
    slider(clean, tr("Gate threshold"), -70, -15, 1, QStringLiteral(" dB"), [](const Track &t) { return t.fx.gateThresholdDb; },
           [](Track &t, double v) { t.fx.gateThresholdDb = v; });
    check(clean, tr("De-esser (soft “s” sounds)"), [](const Track &t) { return t.fx.deEsser; },
          [](Track &t, bool on) { t.fx.deEsser = on; });

    auto *eq = section(tr("3-Band EQ"));
    check(eq, tr("Enable EQ"), [](const Track &t) { return t.fx.eqEnabled; }, [](Track &t, bool on) { t.fx.eqEnabled = on; });
    slider(eq, tr("Low (100 Hz)"), -120, 120, 0.1, QStringLiteral(" dB"), [](const Track &t) { return t.fx.lowGainDb; },
           [](Track &t, double v) { t.fx.lowGainDb = v; });
    slider(eq, tr("Mid (1 kHz)"), -120, 120, 0.1, QStringLiteral(" dB"), [](const Track &t) { return t.fx.midGainDb; },
           [](Track &t, double v) { t.fx.midGainDb = v; });
    slider(eq, tr("High (8 kHz)"), -120, 120, 0.1, QStringLiteral(" dB"), [](const Track &t) { return t.fx.highGainDb; },
           [](Track &t, double v) { t.fx.highGainDb = v; });

    auto *comp = section(tr("Compressor"));
    check(comp, tr("Enable compressor"), [](const Track &t) { return t.fx.compressorEnabled; },
          [](Track &t, bool on) { t.fx.compressorEnabled = on; });
    slider(comp, tr("Threshold"), -50, 0, 1, QStringLiteral(" dB"), [](const Track &t) { return t.fx.thresholdDb; },
           [](Track &t, double v) { t.fx.thresholdDb = v; });
    slider(comp, tr("Ratio"), 10, 200, 0.1, QStringLiteral(":1"), [](const Track &t) { return t.fx.ratio; },
           [](Track &t, double v) { t.fx.ratio = v; });

    auto *voice = section(tr("Voice Changer"));
    slider(voice, tr("Pitch"), -12, 12, 1, QStringLiteral(" st"), [](const Track &t) { return t.fx.pitchSemitones; },
           [](Track &t, double v) { t.fx.pitchSemitones = v; });
    check(voice, tr("Telephone / radio"), [](const Track &t) { return t.fx.telephone; },
          [](Track &t, bool on) { t.fx.telephone = on; });
    check(voice, tr("Chorus (thicker, doubled)"), [](const Track &t) { return t.fx.chorus; },
          [](Track &t, bool on) { t.fx.chorus = on; });

    auto *filters = section(tr("Filters"));
    slider(filters, tr("High-pass (cut lows)"), 0, 100, 10, QStringLiteral(" Hz"),
           [](const Track &t) { return t.fx.highpassHz; }, [](Track &t, double v) { t.fx.highpassHz = v; });
    slider(filters, tr("Low-pass (cut highs)"), 0, 200, 100, QStringLiteral(" Hz"),
           [](const Track &t) { return t.fx.lowpassHz; }, [](Track &t, double v) { t.fx.lowpassHz = v; });
    auto *filterNote = new QLabel(tr("0 Hz = off"), m_body);
    filterNote->setStyleSheet(QStringLiteral("color: #6f6f7a;"));
    filters->addRow(filterNote);

    auto *space = section(tr("Space"));
    check(space, tr("Reverb"), [](const Track &t) { return t.fx.reverbEnabled; },
          [](Track &t, bool on) { t.fx.reverbEnabled = on; });
    slider(space, tr("Reverb mix"), 0, 100, 1, QStringLiteral("%"), [](const Track &t) { return t.fx.reverbMix * 100; },
           [](Track &t, double v) { t.fx.reverbMix = v / 100.0; });
    slider(space, tr("Room size"), 0, 100, 1, QStringLiteral("%"), [](const Track &t) { return t.fx.reverbSize * 100; },
           [](Track &t, double v) { t.fx.reverbSize = v / 100.0; });
    check(space, tr("Echo / delay"), [](const Track &t) { return t.fx.echoEnabled; },
          [](Track &t, bool on) { t.fx.echoEnabled = on; });
    slider(space, tr("Delay"), 50, 1500, 1, QStringLiteral(" ms"), [](const Track &t) { return t.fx.echoDelayMs; },
           [](Track &t, double v) { t.fx.echoDelayMs = v; });
    slider(space, tr("Feedback"), 0, 90, 1, QStringLiteral("%"), [](const Track &t) { return t.fx.echoFeedback * 100; },
           [](Track &t, double v) { t.fx.echoFeedback = v / 100.0; });

    auto *out = section(tr("Output"));
    check(out, tr("Mono (fold to centre)"), [](const Track &t) { return t.fx.mono; },
          [](Track &t, bool on) { t.fx.mono = on; });
    check(out, tr("Limiter (never clip)"), [](const Track &t) { return t.fx.limiterEnabled; },
          [](Track &t, bool on) { t.fx.limiterEnabled = on; });
    slider(out, tr("Ceiling"), -12, 0, 1, QStringLiteral(" dB"), [](const Track &t) { return t.fx.limiterCeilingDb; },
           [](Track &t, double v) { t.fx.limiterCeilingDb = v; });

    auto *mix = section(tr("Auto-Ducking"));
    check(mix, tr("Voice track (dialogue)"), [](const Track &t) { return t.voice; },
          [](Track &t, bool on) { t.voice = on; });
    check(mix, tr("Duck under voice tracks"), [](const Track &t) { return t.fx.duckUnderVoice; },
          [](Track &t, bool on) { t.fx.duckUnderVoice = on; });
    slider(mix, tr("Duck by"), -30, -3, 1, QStringLiteral(" dB"), [](const Track &t) { return t.fx.duckAmountDb; },
           [](Track &t, double v) { t.fx.duckAmountDb = v; });

    auto *note = new QLabel(tr("Ducking, volume and pan play live. The other effects are rendered on export."), m_body);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #6f6f7a; font-style: italic;"));
    page->addWidget(note);
    page->addStretch();

    connect(project, &Project::changed, this, &TrackFxPanel::refresh);
    refresh();
}

void TrackFxPanel::editFx(const QString &key, const std::function<void(Track &)> &fn)
{
    if (m_updating)
        return;
    m_project->edit(
        tr("Track Effects"),
        [&](ProjectData &d) {
            const int i = d.trackIndex(m_trackId);
            if (i >= 0)
                fn(d.tracks[i]);
        },
        key.isEmpty() ? QString() : key + m_trackId.toString());
}

QSlider *TrackFxPanel::slider(QFormLayout *form, const QString &label, int min, int max, double scale,
                              const QString &suffix, std::function<double(const Track &)> get,
                              std::function<void(Track &, double)> set)
{
    auto *row = new QWidget(m_body);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto *s = new QSlider(Qt::Horizontal, row);
    s->setRange(min, max);
    auto *value = new QLabel(row);
    value->setMinimumWidth(52);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setStyleSheet(QStringLiteral("color: #b0b0bc;"));
    h->addWidget(s, 1);
    h->addWidget(value);
    form->addRow(label, row);
    auto show = [value, scale, suffix](int v) {
        value->setText(QString::number(v * scale, 'f', scale < 1 ? 1 : 0) + suffix);
    };
    connect(s, &QSlider::valueChanged, this, [this, scale, set, show, label](int v) {
        show(v);
        editFx(label, [&](Track &t) { set(t, v * scale); });
    });
    m_refreshers << [s, scale, get, show](const Track &t) {
        if (!s->isSliderDown())
            s->setValue(int(std::lround(get(t) / scale)));
        show(s->value());
    };
    return s;
}

QCheckBox *TrackFxPanel::check(QFormLayout *form, const QString &label, std::function<bool(const Track &)> get,
                               std::function<void(Track &, bool)> set)
{
    auto *cb = new QCheckBox(label, m_body);
    form->addRow(cb);
    connect(cb, &QCheckBox::toggled, this, [this, set, label](bool on) { editFx(label, [&](Track &t) { set(t, on); }); });
    m_refreshers << [cb, get](const Track &t) { cb->setChecked(get(t)); };
    return cb;
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
    m_body->setEnabled(ok);
    if (!ok) {
        m_title->setText(tr("Select an audio track (click its header or FX button)"));
        return;
    }
    const Track &t = m_project->data().tracks[i];
    m_updating = true;
    m_title->setText(tr("Effects: %1").arg(t.name));
    for (const auto &r : m_refreshers)
        r(t);
    m_updating = false;
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
    auto *rec = new QToolButton(transport);
    rec->setText(tr("●  Record"));
    rec->setToolTip(tr("Record audio from your microphone at the playhead (Ctrl+R)"));
    rec->setStyleSheet(QStringLiteral("QToolButton { color: #ff5050; font-weight: bold; padding: 4px 10px; }"));
    connect(rec, &QToolButton::clicked, this, &AudioStudio::recordRequested);
    transport->addWidget(rec);
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

    // Detach old strips right away; deleteLater alone leaves them visible
    // until the event loop gets round to deleting them.
    for (auto *s : m_strips) {
        m_mixerLayout->removeWidget(s);
        s->hide();
        s->deleteLater();
    }
    m_strips.clear();
    for (int i = 0; i < ids.size(); ++i) {
        auto *strip = new ChannelStrip(m_project, ids[i], m_mixer);
        connect(strip, &ChannelStrip::selectTrack, m_fx, &TrackFxPanel::setTrack);
        m_mixerLayout->insertWidget(i, strip);
        m_strips.append(strip);
    }
}

} // namespace spotlight

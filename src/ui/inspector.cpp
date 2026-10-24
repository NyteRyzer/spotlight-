#include "ui/inspector.h"

#include "core/looks.h"
#include "core/project.h"
#include "core/timelineops.h"
#include "media/waveformcache.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>

namespace spotlight {

namespace {

QWidget *scrollPage(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

QFormLayout *section(QVBoxLayout *page, const QString &title)
{
    auto *label = new QLabel(title);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    label->setStyleSheet(QStringLiteral("color: #ffc400; margin-top: 6px;"));
    page->addWidget(label);
    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    page->addLayout(form);
    return form;
}

} // namespace

ClipInspector::ClipInspector(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_title(new QLabel(this))
    , m_info(new QLabel(this))
    , m_tabs(new QTabWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    QFont tf = m_title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.1);
    m_title->setFont(tf);
    m_title->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color: #8a8a96;"));
    m_info->setWordWrap(true);
    layout->addWidget(m_title);
    layout->addWidget(m_info);
    layout->addWidget(m_tabs, 1);
    m_tabs->setDocumentMode(true);

    m_videoTab = buildVideoTab();
    m_colorTab = buildColorTab();
    m_effectsTab = buildEffectsTab();
    m_audioTab = buildAudioTab();

    connect(WaveformCache::instance(), &WaveformCache::ready, this, [this] {
        if (!m_pendingNormalize.isNull() && m_pendingNormalize == m_clip && normalize())
            m_pendingNormalize = QUuid();
    });
    connect(project, &Project::changed, this, &ClipInspector::refresh);
    refresh();
}

void ClipInspector::editClip(const QString &key, const std::function<void(Clip &)> &fn)
{
    if (m_updating || m_clip.isNull())
        return;
    const QUuid id = m_clip;
    m_project->edit(
        tr("Adjust Clip"),
        [&](ProjectData &d) {
            if (Clip *c = d.clipById(id))
                fn(*c);
        },
        key + id.toString());
}

QSlider *ClipInspector::addSlider(QFormLayout *form, const QString &label, int min, int max, double scale, Getter get,
                                  Setter set, const QString &suffix)
{
    auto *row = new QWidget;
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto *s = new QSlider(Qt::Horizontal, row);
    s->setRange(min, max);
    auto *value = new QLabel(row);
    value->setMinimumWidth(42);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setStyleSheet(QStringLiteral("color: #b0b0bc;"));
    h->addWidget(s, 1);
    h->addWidget(value);
    form->addRow(label, row);
    auto show = [value, scale, suffix](int v) {
        const double x = v * scale;
        value->setText(QString::number(x, 'f', scale < 0.1 ? 2 : (scale < 1 ? 1 : 0)) + suffix);
    };
    connect(s, &QSlider::valueChanged, this, [this, s, scale, set, show, label](int v) {
        show(v);
        editClip(label, [&](Clip &c) { set(c, s->value() * scale); });
    });
    m_refreshers << [s, scale, get, show](const Clip &c) {
        s->setValue(int(std::lround(get(c) / scale)));
        show(s->value());
    };
    return s;
}

QDoubleSpinBox *ClipInspector::addSpin(QFormLayout *form, const QString &label, double min, double max, double step,
                                       int decimals, const QString &suffix, Getter get, Setter set)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    form->addRow(label, s);
    connect(s, &QDoubleSpinBox::valueChanged, this,
            [this, set, label](double v) { editClip(label, [&](Clip &c) { set(c, v); }); });
    m_refreshers << [s, get](const Clip &c) { s->setValue(get(c)); };
    return s;
}

QCheckBox *ClipInspector::addCheck(QFormLayout *form, const QString &label, std::function<bool(const Clip &)> get,
                                   std::function<void(Clip &, bool)> set)
{
    auto *cb = new QCheckBox(label);
    form->addRow(cb);
    connect(cb, &QCheckBox::toggled, this,
            [this, set, label](bool on) { editClip(label, [&](Clip &c) { set(c, on); }); });
    m_refreshers << [cb, get](const Clip &c) { cb->setChecked(get(c)); };
    return cb;
}

QWidget *ClipInspector::buildVideoTab()
{
    auto *content = new QWidget;
    auto *page = new QVBoxLayout(content);

    auto *speed = section(page, tr("Speed"));
    auto *speedRow = new QWidget;
    auto *sh = new QHBoxLayout(speedRow);
    sh->setContentsMargins(0, 0, 0, 0);
    auto *speedSpin = new QDoubleSpinBox(speedRow);
    speedSpin->setRange(0.1, 8.0);
    speedSpin->setSingleStep(0.25);
    speedSpin->setDecimals(2);
    speedSpin->setSuffix(QStringLiteral("×"));
    speedSpin->setKeyboardTracking(false);
    sh->addWidget(speedSpin, 1);
    for (double v : {0.5, 1.0, 2.0}) {
        auto *b = new QPushButton(QStringLiteral("%1×").arg(v), speedRow);
        b->setFixedWidth(44);
        sh->addWidget(b);
        connect(b, &QPushButton::clicked, speedSpin, [speedSpin, v] { speedSpin->setValue(v); });
    }
    speed->addRow(tr("Rate"), speedRow);
    connect(speedSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating || m_clip.isNull())
            return;
        const QUuid id = m_clip;
        m_project->edit(tr("Change Speed"), [&](ProjectData &d) { timeline::setClipSpeed(d, id, v); },
                        QStringLiteral("speed") + id.toString());
    });
    m_refreshers << [speedSpin](const Clip &c) { speedSpin->setValue(c.speed); };
    auto *actions = new QWidget;
    auto *ah = new QHBoxLayout(actions);
    ah->setContentsMargins(0, 0, 0, 0);
    auto *freeze = new QPushButton(tr("Freeze Frame"), actions);
    freeze->setToolTip(tr("Hold the frame at the playhead for 2 seconds"));
    auto *reverse = new QPushButton(tr("Reverse"), actions);
    reverse->setToolTip(tr("Play this clip backwards (renders a reversed copy)"));
    ah->addWidget(freeze);
    ah->addWidget(reverse);
    speed->addRow(actions);
    connect(freeze, &QPushButton::clicked, this, &ClipInspector::freezeFrameRequested);
    connect(reverse, &QPushButton::clicked, this, &ClipInspector::reverseRequested);

    auto *tf = section(page, tr("Transform"));
    addSlider(tf, tr("Scale"), 5, 400, 1, [](const Clip &c) { return c.transform.scale * 100; },
              [](Clip &c, double v) { c.transform.scale = v / 100.0; }, QStringLiteral("%"));
    addSlider(tf, tr("Position X"), -100, 100, 1, [](const Clip &c) { return c.transform.x * 100; },
              [](Clip &c, double v) { c.transform.x = v / 100.0; }, QStringLiteral("%"));
    addSlider(tf, tr("Position Y"), -100, 100, 1, [](const Clip &c) { return c.transform.y * 100; },
              [](Clip &c, double v) { c.transform.y = v / 100.0; }, QStringLiteral("%"));
    addSlider(tf, tr("Rotation"), -180, 180, 1, [](const Clip &c) { return c.transform.rotation; },
              [](Clip &c, double v) { c.transform.rotation = v; }, QStringLiteral("°"));
    addSlider(tf, tr("Opacity"), 0, 100, 1, [](const Clip &c) { return c.transform.opacity * 100; },
              [](Clip &c, double v) { c.transform.opacity = v / 100.0; }, QStringLiteral("%"));
    auto *flips = new QWidget;
    auto *fh = new QHBoxLayout(flips);
    fh->setContentsMargins(0, 0, 0, 0);
    auto *flipH = new QCheckBox(tr("Flip horizontal"), flips);
    auto *flipV = new QCheckBox(tr("Flip vertical"), flips);
    fh->addWidget(flipH);
    fh->addWidget(flipV);
    tf->addRow(flips);
    connect(flipH, &QCheckBox::toggled, this,
            [this](bool on) { editClip(QStringLiteral("flipH"), [&](Clip &c) { c.transform.flipH = on; }); });
    connect(flipV, &QCheckBox::toggled, this,
            [this](bool on) { editClip(QStringLiteral("flipV"), [&](Clip &c) { c.transform.flipV = on; }); });
    m_refreshers << [flipH, flipV](const Clip &c) {
        flipH->setChecked(c.transform.flipH);
        flipV->setChecked(c.transform.flipV);
    };
    // Quick layouts for picture-in-picture.
    auto *pip = new QComboBox;
    pip->addItems({tr("Full frame"), tr("PIP top-left"), tr("PIP top-right"), tr("PIP bottom-left"),
                   tr("PIP bottom-right"), tr("Split left"), tr("Split right")});
    tf->addRow(tr("Layout"), pip);
    connect(pip, &QComboBox::activated, this, [this](int i) {
        editClip(QStringLiteral("pip"), [&](Clip &c) {
            Transform &t = c.transform;
            t.scale = 1;
            t.x = t.y = 0;
            t.cropLeft = t.cropRight = 0;
            switch (i) {
            case 1: t.scale = 0.33; t.x = -0.30; t.y = -0.28; break;
            case 2: t.scale = 0.33; t.x = 0.30; t.y = -0.28; break;
            case 3: t.scale = 0.33; t.x = -0.30; t.y = 0.28; break;
            case 4: t.scale = 0.33; t.x = 0.30; t.y = 0.28; break;
            case 5: t.cropLeft = 0.25; t.cropRight = 0.25; t.x = -0.25; break;
            case 6: t.cropLeft = 0.25; t.cropRight = 0.25; t.x = 0.25; break;
            default: break;
            }
        });
    });

    auto *crop = section(page, tr("Crop"));
    addSlider(crop, tr("Left"), 0, 90, 1, [](const Clip &c) { return c.transform.cropLeft * 100; },
              [](Clip &c, double v) { c.transform.cropLeft = v / 100.0; }, QStringLiteral("%"));
    addSlider(crop, tr("Right"), 0, 90, 1, [](const Clip &c) { return c.transform.cropRight * 100; },
              [](Clip &c, double v) { c.transform.cropRight = v / 100.0; }, QStringLiteral("%"));
    addSlider(crop, tr("Top"), 0, 90, 1, [](const Clip &c) { return c.transform.cropTop * 100; },
              [](Clip &c, double v) { c.transform.cropTop = v / 100.0; }, QStringLiteral("%"));
    addSlider(crop, tr("Bottom"), 0, 90, 1, [](const Clip &c) { return c.transform.cropBottom * 100; },
              [](Clip &c, double v) { c.transform.cropBottom = v / 100.0; }, QStringLiteral("%"));

    auto *mo = section(page, tr("Motion"));
    auto *motion = new QComboBox;
    motion->addItems(motionNames());
    motion->setToolTip(tr("Ken Burns: a slow zoom or pan across the clip"));
    mo->addRow(tr("Ken Burns"), motion);
    connect(motion, &QComboBox::activated, this,
            [this](int i) { editClip(QStringLiteral("motion"), [&](Clip &c) { c.motion = Motion(i); }); });
    m_refreshers << [motion](const Clip &c) { motion->setCurrentIndex(int(c.motion)); };
    addCheck(mo, tr("Stabilize (smooth camera shake on export)"), [](const Clip &c) { return c.stabilize; },
             [](Clip &c, bool on) { c.stabilize = on; });

    auto *reset = new QPushButton(tr("Reset Transform"));
    page->addWidget(reset);
    connect(reset, &QPushButton::clicked, this, [this] {
        editClip(QStringLiteral("resetTransform"), [](Clip &c) {
            c.transform = Transform();
            c.motion = Motion::None;
        });
    });
    m_editTitle = new QPushButton(tr("Edit Title Text…"));
    page->addWidget(m_editTitle);
    connect(m_editTitle, &QPushButton::clicked, this, &ClipInspector::editTitleRequested);
    page->addStretch();
    return scrollPage(content, this);
}

QWidget *ClipInspector::buildColorTab()
{
    auto *content = new QWidget;
    auto *page = new QVBoxLayout(content);
    auto *looks = section(page, tr("Looks"));
    m_look = new QComboBox;
    m_look->addItem(tr("Custom"));
    for (const auto &l : lookPresets())
        m_look->addItem(l.name);
    m_look->setToolTip(tr("One-click color filters"));
    looks->addRow(tr("Filter"), m_look);
    connect(m_look, &QComboBox::activated, this, [this](int i) {
        if (i == 0)
            return;
        const ColorAdjust a = lookPresets()[i - 1].adjust;
        editClip(QStringLiteral("look"), [&](Clip &c) { c.color = a; });
    });

    auto *basic = section(page, tr("Basic"));
    addSlider(basic, tr("Brightness"), -100, 100, 0.01, [](const Clip &c) { return c.color.brightness; },
              [](Clip &c, double v) { c.color.brightness = v; });
    addSlider(basic, tr("Contrast"), 0, 200, 0.01, [](const Clip &c) { return c.color.contrast; },
              [](Clip &c, double v) { c.color.contrast = v; });
    addSlider(basic, tr("Saturation"), 0, 300, 0.01, [](const Clip &c) { return c.color.saturation; },
              [](Clip &c, double v) { c.color.saturation = v; });
    addSlider(basic, tr("Temperature"), -100, 100, 0.01, [](const Clip &c) { return c.color.temperature; },
              [](Clip &c, double v) { c.color.temperature = v; });
    addSlider(basic, tr("Tint"), -100, 100, 0.01, [](const Clip &c) { return c.color.tint; },
              [](Clip &c, double v) { c.color.tint = v; });

    auto *fx = section(page, tr("Creative"));
    auto *mono = new QComboBox;
    mono->addItems({tr("Color"), tr("Black & White"), tr("Sepia")});
    fx->addRow(tr("Tone"), mono);
    connect(mono, &QComboBox::activated, this,
            [this](int i) { editClip(QStringLiteral("mono"), [&](Clip &c) { c.color.mono = MonoMode(i); }); });
    m_refreshers << [mono](const Clip &c) { mono->setCurrentIndex(int(c.color.mono)); };
    addSlider(fx, tr("Vignette"), 0, 100, 0.01, [](const Clip &c) { return c.color.vignette; },
              [](Clip &c, double v) { c.color.vignette = v; });
    addSlider(fx, tr("Blur"), 0, 100, 0.01, [](const Clip &c) { return c.color.blur; },
              [](Clip &c, double v) { c.color.blur = v; });

    auto *reset = new QPushButton(tr("Reset Color"));
    page->addWidget(reset);
    connect(reset, &QPushButton::clicked, this,
            [this] { editClip(QStringLiteral("resetColor"), [](Clip &c) { c.color = ColorAdjust(); }); });
    m_refreshers << [this](const Clip &c) {
        int idx = 0;
        const auto presets = lookPresets();
        for (int i = 1; i < presets.size(); ++i) {
            if (presets[i].adjust == c.color)
                idx = i + 1;
        }
        if (c.color.isIdentity())
            idx = 1;
        m_look->setCurrentIndex(idx);
    };
    page->addStretch();
    return scrollPage(content, this);
}

QWidget *ClipInspector::buildEffectsTab()
{
    auto *content = new QWidget;
    auto *page = new QVBoxLayout(content);

    auto *tr_ = section(page, tr("Transition In"));
    m_transition = new QComboBox;
    m_transition->addItem(tr("None"), QString());
    for (const auto &t : transitionTypes())
        m_transition->addItem(t.name, t.id);
    tr_->addRow(tr("Type"), m_transition);
    connect(m_transition, &QComboBox::activated, this, [this](int i) {
        const QString id = m_transition->itemData(i).toString();
        editClip(QStringLiteral("transition"), [&](Clip &c) {
            c.transition.type = id;
            if (!id.isEmpty() && c.transition.duration <= 0)
                c.transition.duration = std::min<TimeUs>(kUsPerSecond, c.duration() / 2);
        });
    });
    m_refreshers << [this](const Clip &c) {
        m_transition->setCurrentIndex(std::max(0, m_transition->findData(c.transition.type)));
    };
    addSpin(tr_, tr("Duration"), 0.1, 5.0, 0.1, 2, QStringLiteral(" s"),
            [](const Clip &c) { return c.transition.duration > 0 ? usToSeconds(c.transition.duration) : 1.0; },
            [](Clip &c, double v) { c.transition.duration = std::min(secondsToUs(v), c.duration()); });
    m_transitionHint = new QLabel;
    m_transitionHint->setWordWrap(true);
    m_transitionHint->setStyleSheet(QStringLiteral("color: #8a8a96;"));
    tr_->addRow(m_transitionHint);

    auto *fades = section(page, tr("Fades"));
    addSpin(fades, tr("Fade in"), 0, 30, 0.25, 2, QStringLiteral(" s"), [](const Clip &c) { return usToSeconds(c.fadeIn); },
            [](Clip &c, double v) { c.fadeIn = std::min(secondsToUs(v), c.duration()); });
    addSpin(fades, tr("Fade out"), 0, 30, 0.25, 2, QStringLiteral(" s"),
            [](const Clip &c) { return usToSeconds(c.fadeOut); },
            [](Clip &c, double v) { c.fadeOut = std::min(secondsToUs(v), c.duration() - c.fadeIn); });

    auto *key = section(page, tr("Chroma Key (Green Screen)"));
    addCheck(key, tr("Remove background color"), [](const Clip &c) { return c.key.enabled; },
             [](Clip &c, bool on) { c.key.enabled = on; });
    m_keyColor = new QPushButton;
    key->addRow(tr("Key color"), m_keyColor);
    connect(m_keyColor, &QPushButton::clicked, this, [this] {
        const Clip *c = m_project->data().clipById(m_clip);
        if (!c)
            return;
        const QColor col = QColorDialog::getColor(c->key.color, this, tr("Key Color"));
        if (col.isValid())
            editClip(QStringLiteral("keyColor"), [&](Clip &cl) { cl.key.color = col; });
    });
    auto *presets = new QWidget;
    auto *ph = new QHBoxLayout(presets);
    ph->setContentsMargins(0, 0, 0, 0);
    for (const auto &[name, color] : {std::pair{tr("Green"), QColor(0, 255, 0)}, {tr("Blue"), QColor(0, 70, 255)}}) {
        auto *b = new QPushButton(name, presets);
        ph->addWidget(b);
        const QColor col = color;
        connect(b, &QPushButton::clicked, this, [this, col] {
            editClip(QStringLiteral("keyColor"), [&](Clip &c) {
                c.key.color = col;
                c.key.enabled = true;
            });
        });
    }
    key->addRow(presets);
    m_refreshers << [this](const Clip &c) {
        m_keyColor->setStyleSheet(QStringLiteral("background: %1; min-height: 18px;").arg(c.key.color.name()));
    };
    addSlider(key, tr("Similarity"), 1, 60, 0.01, [](const Clip &c) { return c.key.similarity; },
              [](Clip &c, double v) { c.key.similarity = v; });
    addSlider(key, tr("Edge softness"), 0, 50, 0.01, [](const Clip &c) { return c.key.blend; },
              [](Clip &c, double v) { c.key.blend = v; });
    page->addStretch();
    return scrollPage(content, this);
}

QWidget *ClipInspector::buildAudioTab()
{
    auto *content = new QWidget;
    auto *page = new QVBoxLayout(content);
    auto *gain = section(page, tr("Level"));
    addSpin(gain, tr("Clip gain"), -60, 24, 0.5, 1, QStringLiteral(" dB"),
            [](const Clip &c) { return c.gain > 0 ? 20.0 * std::log10(c.gain) : -60.0; },
            [](Clip &c, double v) { c.gain = v <= -60.0 ? 0.0 : std::pow(10.0, v / 20.0); });
    auto *normalizeBtn = new QPushButton(tr("Normalize (peak −1 dB)"));
    normalizeBtn->setToolTip(tr("Set the clip gain so its loudest moment peaks at −1 dB"));
    gain->addRow(normalizeBtn);
    connect(normalizeBtn, &QPushButton::clicked, this, [this] {
        if (!normalize()) {
            m_pendingNormalize = m_clip;
            m_info->setText(tr("Analyzing audio… the clip will be normalized when it's done."));
        }
    });
    auto *fades = section(page, tr("Fades"));
    addSpin(fades, tr("Fade in"), 0, 30, 0.25, 2, QStringLiteral(" s"), [](const Clip &c) { return usToSeconds(c.fadeIn); },
            [](Clip &c, double v) { c.fadeIn = std::min(secondsToUs(v), c.duration()); });
    addSpin(fades, tr("Fade out"), 0, 30, 0.25, 2, QStringLiteral(" s"),
            [](const Clip &c) { return usToSeconds(c.fadeOut); },
            [](Clip &c, double v) { c.fadeOut = std::min(secondsToUs(v), c.duration() - c.fadeIn); });
    auto *speed = section(page, tr("Speed"));
    auto *spin = new QDoubleSpinBox;
    spin->setRange(0.1, 8.0);
    spin->setSingleStep(0.25);
    spin->setDecimals(2);
    spin->setSuffix(QStringLiteral("×"));
    spin->setKeyboardTracking(false);
    speed->addRow(tr("Rate"), spin);
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating || m_clip.isNull())
            return;
        const QUuid id = m_clip;
        m_project->edit(tr("Change Speed"), [&](ProjectData &d) { timeline::setClipSpeed(d, id, v); },
                        QStringLiteral("speed") + id.toString());
    });
    m_refreshers << [spin](const Clip &c) { spin->setValue(c.speed); };
    page->addStretch();
    return scrollPage(content, this);
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
    if (!c) {
        m_title->setText(tr("No clip selected"));
        m_info->setText(tr("Select a clip on the timeline to adjust it."));
        m_tabs->setVisible(false);
        return;
    }
    const ProjectData &d = m_project->data();
    const Track &t = d.tracks[ti];
    const MediaItem *m = d.mediaById(c->mediaId);
    m_title->setText(m ? m->name : tr("Clip"));
    QString info = tr("%1 · starts %2 · length %3")
                       .arg(t.name, formatTimecode(c->start, d.sequence.frameRate), formatDuration(c->duration()));
    if (!qFuzzyCompare(c->speed, 1.0))
        info += tr(" · %1×").arg(c->speed, 0, 'g', 3);
    m_info->setText(info);

    // Rebuild the tab set for the clip's track type, keeping the current tab.
    const QString current = m_tabs->tabText(m_tabs->currentIndex());
    QVector<QPair<QWidget *, QString>> wanted;
    if (t.isVideo())
        wanted = {{m_videoTab, tr("Video")}, {m_colorTab, tr("Color")}, {m_effectsTab, tr("Effects")}};
    else
        wanted = {{m_audioTab, tr("Audio")}};
    bool same = m_tabs->count() == wanted.size();
    for (int i = 0; same && i < wanted.size(); ++i)
        same = m_tabs->widget(i) == wanted[i].first;
    if (!same) {
        while (m_tabs->count() > 0)
            m_tabs->removeTab(0);
        for (const auto &w : wanted)
            m_tabs->addTab(w.first, w.second);
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (m_tabs->tabText(i) == current)
                m_tabs->setCurrentIndex(i);
        }
    }
    m_tabs->setVisible(true);

    m_updating = true;
    for (const auto &r : m_refreshers)
        r(*c);
    if (m_editTitle)
        m_editTitle->setVisible(m && m->isTitle());
    const bool adjacent = timeline::previousAdjacent(t, *c) != nullptr;
    m_transitionHint->setText(adjacent ? tr("Blends from the previous clip into this one.")
                                       : tr("Place this clip right after another clip on the same track to "
                                            "use a transition."));
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

} // namespace spotlight

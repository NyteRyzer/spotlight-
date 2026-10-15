#include "ui/settingsdialog.h"

#include "core/autosave.h"
#include "core/project.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace spotlight {

SettingsDialog::SettingsDialog(Project *project, AutosaveManager *autosave, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_autosave(autosave)
    , m_resolution(new QComboBox(this))
    , m_width(new QSpinBox(this))
    , m_height(new QSpinBox(this))
    , m_fps(new QDoubleSpinBox(this))
    , m_autosaveOn(new QCheckBox(tr("Autosave unsaved work"), this))
    , m_interval(new QSpinBox(this))
{
    setWindowTitle(tr("Settings"));
    auto *layout = new QVBoxLayout(this);

    auto *seq = new QGroupBox(tr("Sequence (this project)"), this);
    auto *sf = new QFormLayout(seq);
    const QList<QPair<QString, QSize>> presets{{tr("1080p Landscape"), {1920, 1080}},
                                               {tr("4K UHD"), {3840, 2160}},
                                               {tr("720p"), {1280, 720}},
                                               {tr("Vertical 1080×1920 (Shorts/Reels)"), {1080, 1920}},
                                               {tr("Square 1080"), {1080, 1080}},
                                               {tr("Custom"), {}}};
    for (const auto &p : presets)
        m_resolution->addItem(p.first, p.second);
    m_width->setRange(16, 7680);
    m_height->setRange(16, 4320);
    m_fps->setRange(1, 240);
    m_fps->setDecimals(3);
    sf->addRow(tr("Preset"), m_resolution);
    sf->addRow(tr("Width"), m_width);
    sf->addRow(tr("Height"), m_height);
    sf->addRow(tr("Frame rate"), m_fps);
    layout->addWidget(seq);

    auto *app = new QGroupBox(tr("Application"), this);
    auto *af = new QFormLayout(app);
    m_interval->setRange(10, 3600);
    m_interval->setSuffix(tr(" s"));
    af->addRow(m_autosaveOn);
    af->addRow(tr("Autosave every"), m_interval);
    layout->addWidget(app);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    const SequenceSettings &s = project->data().sequence;
    m_width->setValue(s.width);
    m_height->setValue(s.height);
    m_fps->setValue(s.frameRate);
    int idx = m_resolution->count() - 1;
    for (int i = 0; i < m_resolution->count(); ++i) {
        if (m_resolution->itemData(i).toSize() == QSize(s.width, s.height))
            idx = i;
    }
    m_resolution->setCurrentIndex(idx);
    connect(m_resolution, &QComboBox::currentIndexChanged, this, [this](int i) {
        const QSize sz = m_resolution->itemData(i).toSize();
        if (sz.isValid()) {
            m_width->setValue(sz.width());
            m_height->setValue(sz.height());
        }
    });

    QSettings st;
    m_autosaveOn->setChecked(st.value(QStringLiteral("autosave/enabled"), true).toBool());
    m_interval->setValue(autosave->intervalSeconds());
}

void SettingsDialog::accept()
{
    const int w = m_width->value() & ~1;
    const int h = m_height->value() & ~1;
    const double fps = m_fps->value();
    m_project->edit(tr("Sequence Settings"), [&](ProjectData &d) {
        d.sequence.width = w;
        d.sequence.height = h;
        d.sequence.frameRate = fps;
    });
    QSettings st;
    st.setValue(QStringLiteral("autosave/enabled"), m_autosaveOn->isChecked());
    st.setValue(QStringLiteral("autosave/interval"), m_interval->value());
    m_autosave->setEnabled(m_autosaveOn->isChecked());
    m_autosave->setIntervalSeconds(m_interval->value());
    QDialog::accept();
}

} // namespace spotlight

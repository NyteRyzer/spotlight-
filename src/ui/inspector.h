#pragma once

#include "core/timeline.h"

#include <QUuid>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTabWidget;

namespace spotlight {

class Project;

// Properties of the selected clip, in tabs:
//  Video   — speed, transform (scale/position/rotation/opacity/flip/crop),
//            Ken Burns motion, stabilization, freeze frame, reverse
//  Color   — one-click looks plus brightness/contrast/saturation,
//            temperature, tint, black & white / sepia, vignette, blur
//  Effects — transition in, fades, stylize (sharpen, grain, pixelate,
//            invert, cinema bars), blur fill and border, chroma key
//  Audio   — clip gain, normalize, speed, fades
class ClipInspector : public QWidget {
    Q_OBJECT
public:
    explicit ClipInspector(Project *project, QWidget *parent = nullptr);

    QUuid clip() const { return m_clip; }

public slots:
    void setClip(const QUuid &clipId);

signals:
    void freezeFrameRequested();
    void reverseRequested();
    void editTitleRequested();

private:
    using Getter = std::function<double(const Clip &)>;
    using Setter = std::function<void(Clip &, double)>;

    void refresh();
    // Applies `set` to the selected clip as one (merged) undo step.
    void editClip(const QString &key, const std::function<void(Clip &)> &fn);
    QSlider *addSlider(QFormLayout *form, const QString &label, int min, int max, double scale, Getter get, Setter set,
                       const QString &suffix = QString());
    QDoubleSpinBox *addSpin(QFormLayout *form, const QString &label, double min, double max, double step, int decimals,
                            const QString &suffix, Getter get, Setter set);
    QCheckBox *addCheck(QFormLayout *form, const QString &label, std::function<bool(const Clip &)> get,
                        std::function<void(Clip &, bool)> set);
    QWidget *buildVideoTab();
    QWidget *buildColorTab();
    QWidget *buildEffectsTab();
    QWidget *buildAudioTab();
    // Sets clip gain so the loudest peak in the clip reaches -1 dBFS.
    // Returns false if the waveform is still being analysed.
    bool normalize();

    Project *m_project;
    QUuid m_clip;
    QLabel *m_title;
    QLabel *m_info;
    QTabWidget *m_tabs;
    QWidget *m_videoTab = nullptr;
    QWidget *m_colorTab = nullptr;
    QWidget *m_effectsTab = nullptr;
    QWidget *m_audioTab = nullptr;
    QComboBox *m_look = nullptr;
    QComboBox *m_transition = nullptr;
    QPushButton *m_keyColor = nullptr;
    QPushButton *m_borderColor = nullptr;
    QLineEdit *m_name = nullptr;
    QComboBox *m_label = nullptr;
    QCheckBox *m_enabled = nullptr;
    QWidget *m_clipRow = nullptr;
    QPushButton *m_editTitle = nullptr;
    QLabel *m_transitionHint = nullptr;
    QVector<std::function<void(const Clip &)>> m_refreshers;
    bool m_updating = false;
    QUuid m_pendingNormalize; // clip waiting for waveform analysis
};

} // namespace spotlight

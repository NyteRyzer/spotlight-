#pragma once

#include <QUuid>
#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QGroupBox;
class QSlider;

namespace spotlight {

class Project;

// Properties of the selected clip: fades, audio gain and colour correction.
class ClipInspector : public QWidget {
    Q_OBJECT
public:
    explicit ClipInspector(Project *project, QWidget *parent = nullptr);

public slots:
    void setClip(const QUuid &clipId);

private:
    void refresh();
    void apply(const QString &what);
    // Sets clip gain so the loudest peak in the clip reaches -1 dBFS.
    // Returns false if the waveform is still being analysed.
    bool normalize();

    Project *m_project;
    QUuid m_clip;
    QLabel *m_title;
    QLabel *m_info;
    QGroupBox *m_audioBox;
    QGroupBox *m_colorBox;
    QGroupBox *m_fadeBox;
    QDoubleSpinBox *m_gainDb;
    QDoubleSpinBox *m_fadeIn;
    QDoubleSpinBox *m_fadeOut;
    QSlider *m_brightness;
    QSlider *m_contrast;
    QSlider *m_saturation;
    bool m_updating = false;
    QUuid m_pendingNormalize; // clip waiting for waveform analysis
};

} // namespace spotlight

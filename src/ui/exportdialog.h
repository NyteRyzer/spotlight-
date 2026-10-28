#pragma once

#include "core/exportpreset.h"

#include <QDialog>

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QLabel;

namespace spotlight {

class Project;
class Exporter;

// Renders the timeline with a preset (built-in or user-saved).
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(Project *project, QWidget *parent = nullptr);

private:
    void loadPresets();
    void applyPreset(int index);
    ExportPreset currentSettings() const;
    void browse();
    void start();
    void savePreset();
    static QString userPresetFile();

    Project *m_project;
    Exporter *m_exporter;
    QVector<ExportPreset> m_presets;
    QComboBox *m_preset;
    QSpinBox *m_width;
    QSpinBox *m_height;
    QDoubleSpinBox *m_fps;
    QSpinBox *m_crf;
    QSpinBox *m_audioKbps;
    QComboBox *m_loudness;
    QComboBox *m_range;
    QLineEdit *m_path;
    QProgressBar *m_progress;
    QLabel *m_info;
    QPushButton *m_start;
    QPushButton *m_cancel;
};

} // namespace spotlight

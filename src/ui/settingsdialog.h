#pragma once

#include <QDialog>

class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;

namespace spotlight {

class Project;
class AutosaveManager;

// Sequence settings (per project) and application preferences.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Project *project, AutosaveManager *autosave, QWidget *parent = nullptr);

    void accept() override;

private:
    Project *m_project;
    AutosaveManager *m_autosave;
    QComboBox *m_resolution;
    QSpinBox *m_width;
    QSpinBox *m_height;
    QDoubleSpinBox *m_fps;
    QCheckBox *m_autosaveOn;
    QSpinBox *m_interval;
};

} // namespace spotlight

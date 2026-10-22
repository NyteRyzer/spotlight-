#pragma once

#include <QByteArray>
#include <QMainWindow>

class QTabBar;
class QStackedWidget;
class QMenu;
class QLabel;

namespace spotlight {

class Project;
class AutosaveManager;
class VideoStudio;
class AudioStudio;
class DesignStudio;
class TimelineWidget;

// The unified SPOTLIGHT! window: one project, three connected studios.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Opens a project file (from the command line or file association).
    bool openProject(const QString &path);
    // Checks the autosave directory for work from an interrupted session.
    void offerRecovery();

    enum Workspace { Video = 0, Audio = 1, Design = 2 };
    void setWorkspace(int index);

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildMenus();
    bool maybeSave();
    bool save();
    bool saveAs();
    void newProject();
    void openDialog();
    void updateTitle();
    void addRecent(const QString &path);
    void rebuildRecentMenu();
    void captureFrame(const QImage &frame);
    TimelineWidget *activeTimeline() const;

    QMainWindow *studio(int index) const;
    void saveLayouts();
    void restoreLayouts();
    void saveNamedLayout();
    void rebuildLayoutMenu();

    Project *m_project;
    AutosaveManager *m_autosave;
    QTabBar *m_tabs;
    QStackedWidget *m_stack;
    VideoStudio *m_video;
    AudioStudio *m_audio;
    DesignStudio *m_design;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_layoutMenu = nullptr;
    QLabel *m_autosaveLabel;
    QByteArray m_defaultLayouts[3];
};

} // namespace spotlight

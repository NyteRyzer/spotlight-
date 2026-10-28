#pragma once

#include <QWidget>

class QTableWidget;
class QPushButton;

namespace spotlight {

class Project;
class TimelinePlayer;

// Lists the timeline markers: click to jump, edit the name in place, add at
// the playhead or delete. Marker names become YouTube chapter titles.
class MarkersPanel : public QWidget {
    Q_OBJECT
public:
    MarkersPanel(Project *project, TimelinePlayer *player, QWidget *parent = nullptr);

signals:
    void chaptersRequested();

private:
    void rebuild();

    Project *m_project;
    TimelinePlayer *m_player;
    QTableWidget *m_table;
    QPushButton *m_delete;
    QString m_shown; // signature of the listed markers
    bool m_updating = false;
};

} // namespace spotlight

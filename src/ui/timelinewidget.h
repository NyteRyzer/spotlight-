#pragma once

#include "core/projectdata.h"

#include <QHash>
#include <QImage>
#include <QPointer>
#include <QWidget>

#include <functional>

class QScrollBar;
class QMenu;

namespace spotlight {

class Project;

// Multitrack timeline editor shared by the Video Studio (all tracks) and the
// Audio Studio (audio tracks only, taller rows with waveforms).
//
// Mouse: click to select (Shift/Ctrl to add), drag to move (snaps to edges and
// the playhead; drag up/down to change track), drag clip edges to trim, click
// or drag the ruler to scrub, Ctrl+wheel to zoom. Media can be dropped from
// the Media Bin or straight from the file manager.
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    enum class Mode { All, AudioOnly };

    TimelineWidget(Project *project, Mode mode, QWidget *parent = nullptr);

    using ThumbnailProvider = std::function<QImage(const QString &path)>;
    void setThumbnailProvider(ThumbnailProvider provider) { m_thumbs = std::move(provider); }
    // Lets the main window add tool actions (freeze frame, reverse…) to the
    // clip context menu.
    using MenuExtender = std::function<void(QMenu *menu, const QUuid &clipId)>;
    void setClipMenuExtender(MenuExtender fn) { m_menuExtender = std::move(fn); }

    TimeUs playhead() const { return m_playhead; }
    QVector<QUuid> selection() const { return m_selection; }
    void setSelection(const QVector<QUuid> &ids);
    int currentTrack() const { return m_currentTrack; }

    bool snapping() const { return m_snapping; }
    void setSnapping(bool on) { m_snapping = on; }

public slots:
    void setPlayhead(spotlight::TimeUs t);
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void splitAtPlayhead();
    void deleteSelection(bool ripple);
    void selectAll();

signals:
    void seekRequested(spotlight::TimeUs t);
    void selectionChanged();
    void currentTrackChanged(int trackIndex);
    void filesDropped(const QStringList &paths);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    enum class Drag { None, Scrub, Move, TrimStart, TrimEnd };
    enum class HeaderButton { None, Mute, Solo, Lock };

    struct Row {
        int trackIndex;
        int y;
        int height;
    };

    const ProjectData &shown() const;
    QVector<Row> rows() const;
    int rowAt(int y) const;
    QRect contentRect() const;
    int xForTime(TimeUs t) const;
    TimeUs timeForX(int x) const;
    QRect clipRect(const Row &row, const Clip &c) const;
    bool hitClip(const QPoint &pos, QUuid *clipId, int *trackIndex, Drag *edge) const;
    HeaderButton hitHeaderButton(const QPoint &pos, int *trackIndex) const;
    QVector<QPair<QRect, HeaderButton>> headerButtons(const Row &row) const;
    TimeUs snap(TimeUs t, const QSet<QUuid> &exclude) const;
    void updateScrollBars();
    void ensurePlayheadVisible();
    void applyDragPreview(const QPoint &pos);
    void commitDrag();

    void paintRuler(QPainter &p);
    void paintRow(QPainter &p, const Row &row);
    void paintClip(QPainter &p, const Row &row, const Clip &c);
    void paintWaveform(QPainter &p, const QRect &r, const Clip &c, const MediaItem &m);

    Project *m_project;
    Mode m_mode;
    ThumbnailProvider m_thumbs;
    MenuExtender m_menuExtender;
    QScrollBar *m_hbar;
    QScrollBar *m_vbar;

    double m_pxPerSecond = 40.0;
    TimeUs m_playhead = 0;
    QVector<QUuid> m_selection;
    int m_currentTrack = -1;
    bool m_snapping = true;

    // Drag state
    Drag m_drag = Drag::None;
    QPoint m_pressPos;
    QUuid m_dragClip;
    TimeUs m_dragOrigStart = 0;
    TimeUs m_dragOrigEnd = 0;
    bool m_dragMoved = false;
    bool m_previewActive = false;
    ProjectData m_preview;
    TimeUs m_snapLine = -1;

    // External drop indicator
    int m_dropRow = -1;
    TimeUs m_dropTime = -1;
};

} // namespace spotlight

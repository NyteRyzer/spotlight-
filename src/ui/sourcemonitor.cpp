#include "ui/sourcemonitor.h"

#include "media/videoview.h"

#include <QAudioOutput>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace spotlight {

SourceMonitor::SourceMonitor(QWidget *parent)
    : QWidget(parent)
    , m_view(new VideoView(this))
    , m_player(new QMediaPlayer(this))
    , m_audio(new QAudioOutput(this))
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_time(new QLabel(this))
    , m_marks(new QLabel(this))
    , m_playBtn(new QToolButton(this))
{
    m_player->setVideoSink(m_view->videoSink());
    m_player->setAudioOutput(m_audio);
    m_view->setPlaceholderText(tr("Double-click media in the bin to preview it here"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_slider);

    auto *bar = new QHBoxLayout;
    m_playBtn->setText(QStringLiteral("▶"));
    m_playBtn->setToolTip(tr("Play / Pause"));
    auto *inBtn = new QToolButton(this);
    inBtn->setText(tr("Mark In"));
    auto *outBtn = new QToolButton(this);
    outBtn->setText(tr("Mark Out"));
    auto *grab = new QToolButton(this);
    grab->setText(tr("Capture Frame"));
    grab->setToolTip(tr("Capture this frame and open it in the Design Studio"));
    auto *add = new QPushButton(tr("Add to Timeline"), this);
    add->setObjectName(QStringLiteral("primary"));
    add->setToolTip(tr("Place the marked range on the timeline at the playhead"));
    bar->addWidget(m_playBtn);
    bar->addWidget(m_time);
    bar->addStretch();
    bar->addWidget(m_marks);
    bar->addWidget(inBtn);
    bar->addWidget(outBtn);
    bar->addWidget(grab);
    bar->addWidget(add);
    layout->addLayout(bar);

    connect(m_playBtn, &QToolButton::clicked, this, &SourceMonitor::togglePlay);
    connect(inBtn, &QToolButton::clicked, this, &SourceMonitor::markIn);
    connect(outBtn, &QToolButton::clicked, this, &SourceMonitor::markOut);
    connect(grab, &QToolButton::clicked, this, [this] {
        const QImage f = currentFrame();
        if (!f.isNull())
            emit captureFrame(f);
    });
    connect(add, &QPushButton::clicked, this, [this] {
        if (!m_item.id.isNull())
            emit addToTimeline(m_item.id, m_in, m_out);
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        if (!m_slider->isSliderDown())
            m_slider->setValue(int(ms));
        updateLabels();
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) { m_slider->setRange(0, int(ms)); });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        m_playBtn->setText(s == QMediaPlayer::PlayingState ? QStringLiteral("❚❚") : QStringLiteral("▶"));
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        // Show the first frame instead of a black screen after loading.
        if (s == QMediaPlayer::LoadedMedia && m_player->playbackState() == QMediaPlayer::StoppedState)
            m_player->pause();
        if (s == QMediaPlayer::LoadedMedia && m_pendingSeekMs >= 0) {
            const qint64 ms = m_pendingSeekMs;
            m_pendingSeekMs = -1;
            // Queued: the backend ignores seeks made inside this notification.
            QMetaObject::invokeMethod(this, [this, ms] { m_player->setPosition(ms); }, Qt::QueuedConnection);
        }
    });
    connect(m_slider, &QSlider::sliderMoved, m_player, &QMediaPlayer::setPosition);
    updateLabels();
}

void SourceMonitor::load(const MediaItem &item)
{
    m_item = item;
    m_in = 0;
    m_out = -1;
    m_pendingSeekMs = -1;
    m_player->stop();
    if (item.isStill()) {
        m_player->setSource(QUrl());
        QImageReader r(item.path);
        r.setAutoTransform(true);
        m_view->setStillImage(r.read());
        m_slider->setRange(0, 0);
    } else {
        m_view->showBlack();
        m_player->setSource(QUrl::fromLocalFile(item.path));
    }
    m_view->setPlaceholderText(item.hasVideo || item.isStill() ? QString() : tr("♫  %1").arg(item.name));
    updateLabels();
}

void SourceMonitor::loadRange(const MediaItem &item, TimeUs in, TimeUs out)
{
    load(item);
    m_in = in;
    m_out = out;
    m_pendingSeekMs = usToMs(in);
    updateLabels();
}

QImage SourceMonitor::currentFrame() const
{
    return m_view->currentFrame();
}

void SourceMonitor::togglePlay()
{
    if (m_item.id.isNull() || m_item.isStill())
        return;
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void SourceMonitor::markIn()
{
    if (m_item.isStill())
        return;
    m_in = msToUs(m_player->position());
    if (m_out >= 0 && m_out <= m_in)
        m_out = -1;
    updateLabels();
}

void SourceMonitor::markOut()
{
    if (m_item.isStill())
        return;
    m_out = msToUs(m_player->position());
    if (m_out <= m_in)
        m_in = 0;
    updateLabels();
}

void SourceMonitor::updateLabels()
{
    const double fps = m_item.frameRate > 0 ? m_item.frameRate : 30.0;
    m_time->setText(QStringLiteral("%1 / %2").arg(formatTimecode(msToUs(m_player->position()), fps),
                                                  formatTimecode(m_item.duration, fps)));
    if (m_item.id.isNull() || m_item.isStill()) {
        m_marks->clear();
        return;
    }
    const TimeUs out = m_out < 0 ? m_item.duration : m_out;
    m_marks->setText(tr("In %1  Out %2").arg(formatDuration(m_in), formatDuration(out)));
}

} // namespace spotlight

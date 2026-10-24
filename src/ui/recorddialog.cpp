#include "ui/recorddialog.h"

#include "core/project.h"
#include "core/timelineops.h"
#include "core/titles.h"
#include "media/mediaprobe.h"
#include "media/timelineplayer.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QApplication>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#endif
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstring>

namespace spotlight {

namespace {

constexpr double kMeterFloorDb = -60.0;

double toMeter(double peak)
{
    if (peak <= 0)
        return 0;
    const double db = 20.0 * std::log10(peak);
    return std::clamp((db - kMeterFloorDb) / -kMeterFloorDb, 0.0, 1.0);
}

// Peak of a buffer of interleaved samples in the given format (0..1).
double bufferPeak(const QByteArray &data, const QAudioFormat &fmt)
{
    double peak = 0;
    const char *p = data.constData();
    switch (fmt.sampleFormat()) {
    case QAudioFormat::Int16:
        for (qsizetype i = 0; i + 1 < data.size(); i += 2) {
            qint16 v;
            std::memcpy(&v, p + i, 2);
            peak = std::max(peak, std::abs(v / 32768.0));
        }
        break;
    case QAudioFormat::Int32:
        for (qsizetype i = 0; i + 3 < data.size(); i += 4) {
            qint32 v;
            std::memcpy(&v, p + i, 4);
            peak = std::max(peak, std::abs(v / 2147483648.0));
        }
        break;
    case QAudioFormat::Float:
        for (qsizetype i = 0; i + 3 < data.size(); i += 4) {
            float v;
            std::memcpy(&v, p + i, 4);
            peak = std::max(peak, double(std::abs(v)));
        }
        break;
    case QAudioFormat::UInt8:
        for (qsizetype i = 0; i < data.size(); ++i)
            peak = std::max(peak, std::abs((quint8(p[i]) - 128) / 128.0));
        break;
    default:
        break;
    }
    return std::min(1.0, peak);
}

bool probeFile(const QString &path, MediaItem *out, QString *error)
{
    MediaProbe probe;
    QEventLoop loop;
    bool ok = false;
    QObject::connect(&probe, &MediaProbe::probed, &loop, [&](const MediaItem &item, const QImage &) {
        *out = item;
        ok = true;
        loop.quit();
    });
    QObject::connect(&probe, &MediaProbe::failed, &loop, [&](const QString &, const QString &e) {
        *error = e;
        loop.quit();
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    probe.probe(path);
    loop.exec();
    return ok;
}

} // namespace

// ---------------------------------------------------------------------------

LevelMeter::LevelMeter(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(18);
}

void LevelMeter::setLevel(double peak)
{
    const double v = toMeter(peak);
    // Fast attack, slow release, like a hardware meter.
    m_level = v > m_level ? v : m_level * 0.85 + v * 0.15;
    if (v >= m_hold) {
        m_hold = v;
        m_holdFrames = 30;
    } else if (--m_holdFrames <= 0) {
        m_hold = std::max(0.0, m_hold - 0.02);
    }
    update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = rect().adjusted(0, 0, -1, -1);
    p.fillRect(r, QColor(24, 24, 30));
    // Zones: green up to -12 dB, yellow to -3 dB, red above.
    auto x = [&](double db) { return int(r.width() * (db - kMeterFloorDb) / -kMeterFloorDb); };
    const int w = int(r.width() * m_level);
    p.fillRect(QRect(r.left(), r.top(), std::min(w, x(-12)), r.height()), QColor(60, 200, 110));
    if (w > x(-12))
        p.fillRect(QRect(x(-12), r.top(), std::min(w, x(-3)) - x(-12), r.height()), QColor(240, 200, 40));
    if (w > x(-3))
        p.fillRect(QRect(x(-3), r.top(), w - x(-3), r.height()), QColor(240, 60, 50));
    const int hx = int(r.width() * m_hold);
    p.fillRect(QRect(hx - 1, r.top(), 2, r.height()), Qt::white);
    p.setPen(QColor(255, 255, 255, 60));
    for (int db : {-48, -36, -24, -12, -6, -3})
        p.drawLine(x(db), r.top(), x(db), r.bottom());
    p.setPen(QColor(60, 60, 70));
    p.drawRect(r);
}

// ---------------------------------------------------------------------------

RecordDialog::RecordDialog(Project *project, TimelinePlayer *player, const QUuid &preferredTrack, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_player(player)
    , m_device(new QComboBox(this))
    , m_track(new QComboBox(this))
    , m_countIn(new QCheckBox(tr("3-second count-in"), this))
    , m_playAlong(new QCheckBox(tr("Play the timeline while recording (use headphones)"), this))
    , m_voice(new QCheckBox(tr("Treat as voice (music ducks under it)"), this))
    , m_meter(new LevelMeter(this))
    , m_peakLabel(new QLabel(this))
    , m_time(new QLabel(QStringLiteral("00:00.0"), this))
    , m_info(new QLabel(this))
    , m_record(new QPushButton(this))
    , m_tick(new QTimer(this))
    , m_countdown(new QTimer(this))
    , m_preferredTrack(preferredTrack)
{
    setWindowTitle(tr("Record Audio"));
    setMinimumWidth(460);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    const auto inputs = QMediaDevices::audioInputs();
    const QAudioDevice def = QMediaDevices::defaultAudioInput();
    for (int i = 0; i < inputs.size(); ++i) {
        m_device->addItem(inputs[i].description());
        if (inputs[i].id() == def.id())
            m_device->setCurrentIndex(i);
    }
    form->addRow(tr("Input"), m_device);
    form->addRow(tr("Record onto"), m_track);
    auto *meterRow = new QHBoxLayout;
    meterRow->addWidget(m_meter, 1);
    m_peakLabel->setMinimumWidth(64);
    m_peakLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    meterRow->addWidget(m_peakLabel);
    form->addRow(tr("Level"), meterRow);
    m_countIn->setChecked(true);
    m_playAlong->setChecked(true);
    m_voice->setChecked(true);
    form->addRow(m_countIn);
    form->addRow(m_playAlong);
    form->addRow(m_voice);

    QFont big = m_time->font();
    big.setPointSizeF(big.pointSizeF() * 2.4);
    big.setBold(true);
    m_time->setFont(big);
    m_time->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_time);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color: #9a9aa8;"));
    layout->addWidget(m_info);

    auto *buttons = new QHBoxLayout;
    m_record->setMinimumHeight(40);
    buttons->addWidget(m_record, 1);
    auto *close = new QPushButton(tr("Done"), this);
    close->setMinimumHeight(40);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    m_tick->setInterval(100);
    connect(m_tick, &QTimer::timeout, this, [this] {
        const qint64 ms = m_source ? m_source->format().durationForBytes(qint32(m_dataBytes)) / 1000 : 0;
        m_time->setText(QStringLiteral("%1:%2.%3")
                            .arg(ms / 60000, 2, 10, QLatin1Char('0'))
                            .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
                            .arg(ms / 100 % 10));
    });
    m_countdown->setInterval(1000);
    connect(m_countdown, &QTimer::timeout, this, [this] {
        if (--m_countLeft <= 0) {
            m_countdown->stop();
            beginRecording();
        } else {
            m_time->setText(QString::number(m_countLeft));
        }
    });
    connect(m_record, &QPushButton::clicked, this, &RecordDialog::toggleRecord);
    connect(close, &QPushButton::clicked, this, &RecordDialog::reject);
    connect(m_device, &QComboBox::currentIndexChanged, this, [this] {
        stopMeter();
        startMeter();
    });
    m_record->setStyleSheet(QStringLiteral("QPushButton { color: #ff5050; font-weight: bold; font-size: 15px; }"));
    m_record->setText(tr("●  Record"));
    refreshTracks();
    m_info->setText(tr("Recording starts at the playhead (%1). Press Record again to stop; you can record as many "
                       "takes as you like.")
                        .arg(formatTimecode(m_player->position(), m_project->data().sequence.frameRate)));
    if (inputs.isEmpty()) {
        m_record->setEnabled(false);
        m_info->setText(tr("No microphone or audio input was found. Connect one and reopen this window."));
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0) && QT_CONFIG(permissions)
    // macOS (and sandboxed Linux) only deliver microphone audio after the
    // user allows it; Qt doesn't ask by itself, so request access here.
    const QMicrophonePermission mic;
    switch (qApp->checkPermission(mic)) {
    case Qt::PermissionStatus::Granted:
        startMeter();
        break;
    case Qt::PermissionStatus::Undetermined:
        m_record->setEnabled(false);
        m_info->setText(tr("Waiting for permission to use the microphone…"));
        qApp->requestPermission(mic, this, [this](const QPermission &p) {
            if (p.status() == Qt::PermissionStatus::Granted) {
                m_record->setEnabled(true);
                m_info->setText(tr("Microphone ready. Recording starts at the playhead."));
                startMeter();
            } else {
                showPermissionDenied();
            }
        });
        break;
    case Qt::PermissionStatus::Denied:
        showPermissionDenied();
        break;
    }
#else
    startMeter();
#endif
}

void RecordDialog::showPermissionDenied()
{
    m_record->setEnabled(false);
#ifdef Q_OS_MACOS
    m_info->setText(tr("SPOTLIGHT! isn't allowed to use the microphone. Open System Settings → Privacy & "
                       "Security → Microphone, turn on SPOTLIGHT!, then reopen this window."));
#else
    m_info->setText(tr("SPOTLIGHT! isn't allowed to use the microphone. Allow microphone access for SPOTLIGHT! "
                       "in your system's privacy settings, then reopen this window."));
#endif
}

RecordDialog::~RecordDialog()
{
    stopMeter();
}

void RecordDialog::refreshTracks()
{
    const QString current = m_track->currentData().toString();
    m_track->clear();
    m_track->addItem(tr("New track for each take"), QString());
    const ProjectData &d = m_project->data();
    for (int ti : d.audioTrackIndices()) {
        const Track &t = d.tracks[ti];
        if (!t.locked)
            m_track->addItem(t.name, t.id.toString());
    }
    const QString want = current.isEmpty() && !m_preferredTrack.isNull() ? m_preferredTrack.toString() : current;
    const int i = m_track->findData(want);
    m_track->setCurrentIndex(i >= 0 ? i : 0);
}

void RecordDialog::startMeter()
{
    const auto inputs = QMediaDevices::audioInputs();
    const int i = m_device->currentIndex();
    if (i < 0 || i >= inputs.size())
        return;
    const QAudioDevice dev = inputs[i];
    QAudioFormat fmt = dev.preferredFormat();
    // WAV holds 16/32-bit integer or float PCM; fall back to 16-bit 48 kHz.
    if (fmt.sampleFormat() != QAudioFormat::Int16 && fmt.sampleFormat() != QAudioFormat::Int32
        && fmt.sampleFormat() != QAudioFormat::Float)
        fmt.setSampleFormat(QAudioFormat::Int16);
    if (!dev.isFormatSupported(fmt)) {
        fmt.setSampleFormat(QAudioFormat::Int16);
        fmt.setSampleRate(48000);
        fmt.setChannelCount(std::min(2, dev.maximumChannelCount()));
    }
    m_source = new QAudioSource(dev, fmt, this);
    m_source->setBufferSize(fmt.bytesForDuration(100000));
    m_sourceIo = m_source->start();
    if (!m_sourceIo) {
        m_peakLabel->setText(tr("no signal"));
        return;
    }
    connect(m_sourceIo, &QIODevice::readyRead, this, &RecordDialog::readMeter);
}

void RecordDialog::stopMeter()
{
    if (!m_source)
        return;
    m_source->stop();
    m_source->deleteLater();
    m_source = nullptr;
    m_sourceIo = nullptr;
}

void RecordDialog::readMeter()
{
    if (!m_sourceIo || !m_source)
        return;
    const QByteArray data = m_sourceIo->readAll();
    if (data.isEmpty())
        return;
    if (m_file) {
        m_file->write(data);
        m_dataBytes += data.size();
    }
    const double peak = bufferPeak(data, m_source->format());
    m_meter->setLevel(peak);
    const double db = peak > 0 ? 20.0 * std::log10(peak) : -99.0;
    m_peakLabel->setText(db <= -60 ? QStringLiteral("−∞ dB") : QStringLiteral("%1 dB").arg(db, 0, 'f', 1));
    m_peakLabel->setStyleSheet(db > -1.0 ? QStringLiteral("color: #ff5050; font-weight: bold;") : QString());
}

QString RecordDialog::nextTakePath() const
{
    QString base;
    if (!m_project->filePath().isEmpty())
        base = QFileInfo(m_project->filePath()).absolutePath() + QStringLiteral("/SPOTLIGHT Media/Recordings");
    else
        base = titles::generatedDir() + QStringLiteral("/Recordings");
    QDir().mkpath(base);
    return base + QStringLiteral("/recording-%1.wav")
                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
}

void RecordDialog::writeWavHeader(quint32 dataBytes)
{
    const QAudioFormat fmt = m_source->format();
    const quint16 channels = quint16(fmt.channelCount());
    const quint32 rate = quint32(fmt.sampleRate());
    const quint16 bits = quint16(fmt.bytesPerSample() * 8);
    const quint16 tag = fmt.sampleFormat() == QAudioFormat::Float ? 3 : 1; // IEEE float / PCM
    QByteArray h;
    auto u32 = [&](quint32 v) {
        char b[4];
        qToLittleEndian(v, b);
        h.append(b, 4);
    };
    auto u16 = [&](quint16 v) {
        char b[2];
        qToLittleEndian(v, b);
        h.append(b, 2);
    };
    h.append("RIFF");
    u32(36 + dataBytes);
    h.append("WAVEfmt ");
    u32(16);
    u16(tag);
    u16(channels);
    u32(rate);
    u32(rate * channels * bits / 8);
    u16(quint16(channels * bits / 8));
    u16(bits);
    h.append("data");
    u32(dataBytes);
    const qint64 pos = m_file->pos();
    m_file->seek(0);
    m_file->write(h);
    m_file->seek(std::max<qint64>(pos, h.size()));
}

void RecordDialog::toggleRecord()
{
    if (m_countdown->isActive()) { // cancel the count-in
        m_countdown->stop();
        m_record->setText(tr("●  Record"));
        m_device->setEnabled(true);
        m_time->setText(QStringLiteral("00:00.0"));
        return;
    }
    if (m_file) {
        stopRecording();
        return;
    }
    if (!m_source || !m_sourceIo) {
        QMessageBox::warning(this, tr("Record Audio"), tr("The audio input could not be opened."));
        return;
    }
    m_startAt = m_player->position();
    m_device->setEnabled(false);
    if (m_countIn->isChecked()) {
        m_countLeft = 3;
        m_time->setText(QStringLiteral("3"));
        m_record->setText(tr("Cancel count-in"));
        m_countdown->start();
    } else {
        beginRecording();
    }
}

void RecordDialog::beginRecording()
{
    m_file = new QFile(nextTakePath(), this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Record Audio"), m_file->errorString());
        delete m_file;
        m_file = nullptr;
        m_device->setEnabled(true);
        return;
    }
    m_dataBytes = 0;
    writeWavHeader(0);
    if (m_sourceIo)
        m_sourceIo->readAll(); // drop audio buffered before the take
    m_tick->start();
    if (m_playAlong->isChecked()) {
        m_player->seek(m_startAt);
        m_player->play();
    }
    m_record->setText(tr("■  Stop"));
    m_record->setStyleSheet(QStringLiteral(
        "QPushButton { background: #d42a35; color: white; font-weight: bold; font-size: 15px; border: 0; }"));
}

void RecordDialog::stopRecording()
{
    if (!m_file)
        return;
    readMeter(); // flush what's buffered
    m_tick->stop();
    m_player->pause();
    writeWavHeader(quint32(std::min<qint64>(m_dataBytes, 0xFFFFFF00)));
    const QString path = m_file->fileName();
    m_file->close();
    delete m_file;
    m_file = nullptr;
    finishTake(path);
}

void RecordDialog::finishTake(const QString &path)
{
    m_record->setText(tr("●  Record"));
    m_record->setStyleSheet(QStringLiteral("QPushButton { color: #ff5050; font-weight: bold; font-size: 15px; }"));
    m_device->setEnabled(true);
    MediaItem item;
    QString err;
    if (m_dataBytes <= 0) {
        QFile::remove(path);
        QMessageBox::warning(this, tr("Record Audio"),
                             tr("No audio arrived from the input. Check that the microphone is connected and that "
                                "SPOTLIGHT! may use it (macOS: System Settings → Privacy & Security → Microphone)."));
        return;
    }
    if (path.isEmpty() || !probeFile(path, &item, &err)) {
        QMessageBox::warning(this, tr("Record Audio"), tr("The recording could not be read: %1").arg(err));
        return;
    }
    ++m_takes;
    item.name = tr("Recording %1 (take %2)").arg(QTime::currentTime().toString(QStringLiteral("HH:mm"))).arg(m_takes);
    const QString trackId = m_track->currentData().toString();
    const bool voice = m_voice->isChecked();
    const TimeUs at = m_startAt;
    QString trackName;
    m_project->edit(tr("Record Audio"), [&](ProjectData &d) {
        d.media.append(item);
        int track = trackId.isEmpty() ? -1 : d.trackIndex(QUuid::fromString(trackId));
        if (track < 0) {
            track = d.addTrack(TrackType::Audio, tr("Rec %1").arg(m_takes));
            d.tracks[track].voice = voice;
        } else if (voice) {
            d.tracks[track].voice = true;
        }
        trackName = d.tracks[track].name;
        timeline::placeMedia(d, item.id, track, at);
    });
    refreshTracks();
    m_info->setText(tr("Take %1 (%2) added to %3. Record again for another take, or press Done.")
                        .arg(m_takes)
                        .arg(formatDuration(item.duration), trackName));
    emit status(tr("Recorded %1 onto %2.").arg(formatDuration(item.duration), trackName));
    m_player->seek(m_startAt);
}

void RecordDialog::reject()
{
    if (m_countdown->isActive())
        m_countdown->stop();
    stopRecording(); // keep a take that's still running
    stopMeter();
    QDialog::reject();
}

} // namespace spotlight

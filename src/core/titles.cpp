#include "core/titles.h"

#include "core/designrender.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace spotlight::titles {

namespace {

// A text layer's "align" doubles as its anchor (0 left edge, 1 centre,
// 2 right edge), so edited text stays where the preset put it.
DesignLayer textLayer(const QString &name, const QString &text, int px, const QColor &fill, const QColor &outline,
                      double outlineW, bool shadow, int align)
{
    DesignLayer l;
    l.id = QUuid::createUuid();
    l.type = LayerType::Text;
    l.name = name;
    l.text = text;
    l.fontSize = std::max(8, px);
    l.bold = true;
    l.textColor = fill;
    l.outlineColor = outline;
    l.outlineWidth = outlineW;
    l.shadow = shadow;
    l.align = align;
    return l;
}

DesignLayer box(const QString &name, const QRectF &r, const QColor &c1, const QColor &c2, double radius,
                ShapeKind kind = ShapeKind::Rectangle)
{
    DesignLayer l;
    l.id = QUuid::createUuid();
    l.type = LayerType::Shape;
    l.name = name;
    l.shape = kind;
    l.position = r.topLeft();
    l.shapeSize = r.size().toSize();
    l.fillColor = c1;
    l.fillColor2 = c2;
    l.gradient = c1 != c2;
    l.gradientAngle = 0;
    l.cornerRadius = radius;
    l.outlineWidth = 0;
    return l;
}

// Places a text layer so its anchor point (by l.align) sits at (x, y), where
// y is the vertical centre.
void place(DesignRenderer &r, DesignLayer &l, double x, double y)
{
    const QRectF b = r.localRect(l);
    double left = x;
    if (l.align == 1)
        left = x - b.width() / 2;
    else if (l.align == 2)
        left = x - b.width();
    l.position = QPointF(left, y - b.height() / 2);
}

QPointF anchorOf(DesignRenderer &r, const DesignLayer &l)
{
    const QRectF b = r.localRect(l);
    double x = l.position.x();
    if (l.align == 1)
        x += b.width() / 2;
    else if (l.align == 2)
        x += b.width();
    return QPointF(x, l.position.y() + b.height() / 2);
}

} // namespace

QStringList presetNames()
{
    auto tr = [](const char *s) { return QCoreApplication::translate("Titles", s); };
    return {tr("Big Title"),     tr("Lower Third"),   tr("Caption"),       tr("Subscribe Button"),
            tr("Chapter Card"),  tr("Neon Glow"),     tr("Breaking News"), tr("Quote")};
}

DesignDocument makeTitle(int preset, const QSize &frame, const QString &text, const QString &subtitle)
{
    DesignDocument doc;
    doc.canvasSize = frame;
    doc.background = Qt::transparent;
    const double W = frame.width(), H = frame.height();
    const double k = std::min(W / 1920.0, H / 1080.0);
    const auto px = [&](double v) { return int(std::lround(v * k)); };
    DesignRenderer r;
    const QString sub = subtitle;

    switch (preset) {
    default:
    case 0: { // Big Title
        DesignLayer t = textLayer(QStringLiteral("Title"), text, px(150), Qt::white, Qt::black, 9 * k, true, 1);
        place(r, t, W / 2, H / 2 - (sub.isEmpty() ? 0 : 50 * k));
        doc.layers << t;
        if (!sub.isEmpty()) {
            DesignLayer s = textLayer(QStringLiteral("Subtitle"), sub, px(60), QColor(255, 210, 60), Qt::black,
                                      5 * k, true, 1);
            place(r, s, W / 2, H / 2 + 90 * k);
            doc.layers << s;
        }
        break;
    }
    case 1: { // Lower Third
        const double x = 110 * k, y = H - 250 * k;
        DesignLayer t = textLayer(QStringLiteral("Title"), text, px(66), Qt::white, Qt::black, 0, false, 0);
        DesignLayer s = textLayer(QStringLiteral("Subtitle"), sub.isEmpty() ? QStringLiteral(" ") : sub, px(40),
                                  QColor(30, 30, 36), Qt::black, 0, false, 0);
        const double tw = r.localRect(t).width(), sw = r.localRect(s).width();
        const double w = std::max(tw, sw) + 80 * k;
        doc.layers << box(QStringLiteral("Accent"), QRectF(x - 14 * k, y, 14 * k, 160 * k), QColor(255, 196, 0),
                          QColor(255, 196, 0), 0);
        doc.layers << box(QStringLiteral("Name Bar"), QRectF(x, y, w, 96 * k), QColor(20, 20, 26, 235),
                          QColor(40, 40, 52, 235), 0);
        doc.layers << box(QStringLiteral("Role Bar"), QRectF(x, y + 96 * k, std::max(sw + 80 * k, w * 0.6), 64 * k),
                          QColor(255, 255, 255, 240), QColor(255, 255, 255, 240), 0);
        place(r, t, x + 40 * k, y + 48 * k);
        place(r, s, x + 40 * k, y + 128 * k);
        doc.layers << t << s;
        break;
    }
    case 2: { // Caption
        DesignLayer t = textLayer(QStringLiteral("Title"), text, px(58), Qt::white, Qt::black, 0, false, 1);
        t.textBox = true;
        t.textBoxColor = QColor(0, 0, 0, 175);
        t.textBoxPadding = 0.35;
        place(r, t, W / 2, H - 140 * k);
        doc.layers << t;
        break;
    }
    case 3: { // Subscribe Button
        DesignLayer t = textLayer(QStringLiteral("Title"), text.isEmpty() ? QStringLiteral("SUBSCRIBE") : text,
                                  px(64), Qt::white, Qt::black, 0, false, 1);
        const QRectF tb = r.localRect(t);
        const QRectF btn(W - tb.width() - 200 * k, H - 210 * k, tb.width() + 110 * k, 110 * k);
        DesignLayer b = box(QStringLiteral("Button"), btn, QColor(230, 30, 40), QColor(200, 10, 30), 22 * k);
        b.dropShadow = true;
        b.shadowBlur = 16 * k;
        b.shadowDistance = 8 * k;
        doc.layers << b;
        place(r, t, btn.center().x(), btn.center().y());
        doc.layers << t;
        break;
    }
    case 4: { // Chapter Card
        DesignLayer num = textLayer(QStringLiteral("Subtitle"), sub.isEmpty() ? QStringLiteral("01") : sub, px(220),
                                    QColor(255, 196, 0), Qt::black, 0, false, 0);
        DesignLayer t = textLayer(QStringLiteral("Title"), text, px(96), Qt::white, Qt::black, 0, true, 0);
        doc.layers << box(QStringLiteral("Backdrop"), QRectF(0, H * 0.3, W * 0.7, H * 0.4), QColor(0, 0, 0, 200),
                          QColor(0, 0, 0, 0), 0);
        place(r, num, 120 * k, H / 2);
        place(r, t, 120 * k + r.localRect(num).width() + 40 * k, H / 2);
        doc.layers << num << t;
        break;
    }
    case 5: { // Neon Glow
        DesignLayer t = textLayer(QStringLiteral("Title"), text, px(140), QColor(255, 240, 255), QColor(255, 60, 200),
                                  3 * k, false, 1);
        t.glow = true;
        t.glowColor = QColor(255, 40, 190);
        t.glowSize = 26 * k;
        place(r, t, W / 2, H / 2);
        doc.layers << t;
        break;
    }
    case 6: { // Breaking News
        const double y = H - 230 * k;
        DesignLayer tag = textLayer(QStringLiteral("Subtitle"), sub.isEmpty() ? QStringLiteral("BREAKING") : sub,
                                    px(48), Qt::white, Qt::black, 0, false, 1);
        DesignLayer t = textLayer(QStringLiteral("Title"), text, px(56), QColor(20, 20, 24), Qt::black, 0, false, 0);
        const double tagW = r.localRect(tag).width() + 60 * k;
        doc.layers << box(QStringLiteral("Headline Bar"), QRectF(0, y, W, 100 * k), QColor(250, 250, 250),
                          QColor(250, 250, 250), 0);
        doc.layers << box(QStringLiteral("Tag"), QRectF(60 * k, y - 20 * k, tagW, 140 * k), QColor(210, 20, 30),
                          QColor(170, 10, 20), 0);
        place(r, tag, 60 * k + tagW / 2, y + 50 * k);
        place(r, t, 60 * k + tagW + 40 * k, y + 50 * k);
        doc.layers << tag << t;
        break;
    }
    case 7: { // Quote
        DesignLayer t = textLayer(QStringLiteral("Title"), QStringLiteral("“%1”").arg(text), px(84), Qt::white,
                                  Qt::black, 0, true, 1);
        t.italic = true;
        t.bold = false;
        place(r, t, W / 2, H / 2 - 30 * k);
        doc.layers << t;
        DesignLayer s = textLayer(QStringLiteral("Subtitle"),
                                  sub.isEmpty() ? QString() : QStringLiteral("— %1").arg(sub), px(48),
                                  QColor(220, 220, 230), Qt::black, 0, true, 1);
        place(r, s, W / 2, H / 2 + 90 * k);
        if (!sub.isEmpty())
            doc.layers << s;
        break;
    }
    }
    return doc;
}

QString mainText(const DesignDocument &doc)
{
    for (const auto &l : doc.layers) {
        if (l.type == LayerType::Text && l.name == QLatin1String("Title")) {
            QString t = l.text;
            if (t.startsWith(QStringLiteral("“")) && t.endsWith(QStringLiteral("”")))
                t = t.mid(1, t.size() - 2);
            return t;
        }
    }
    return {};
}

QString subtitleText(const DesignDocument &doc)
{
    for (const auto &l : doc.layers) {
        if (l.type == LayerType::Text && l.name == QLatin1String("Subtitle")) {
            QString t = l.text;
            if (t.startsWith(QStringLiteral("— ")))
                t = t.mid(2);
            return t.trimmed();
        }
    }
    return {};
}

void setTexts(DesignDocument &doc, const QString &text, const QString &subtitle)
{
    DesignRenderer r;
    for (auto &l : doc.layers) {
        if (l.type != LayerType::Text)
            continue;
        QString value;
        if (l.name == QLatin1String("Title")) {
            const bool quoted = l.text.startsWith(QStringLiteral("“"));
            value = quoted ? QStringLiteral("“%1”").arg(text) : text;
        } else if (l.name == QLatin1String("Subtitle")) {
            const bool dashed = l.text.startsWith(QStringLiteral("— "));
            value = dashed ? QStringLiteral("— %1").arg(subtitle) : (subtitle.isEmpty() ? QStringLiteral(" ") : subtitle);
        } else {
            continue;
        }
        const QPointF anchor = anchorOf(r, l);
        l.text = value;
        place(r, l, anchor.x(), anchor.y());
    }
}

QString generatedDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty())
        base = QDir::tempPath() + QStringLiteral("/spotlight");
    const QString dir = base + QStringLiteral("/generated");
    QDir().mkpath(dir);
    return dir;
}

bool renderTitleMedia(const DesignDocument &doc, const QString &name, MediaItem *out, QString *error)
{
    DesignRenderer r;
    const QImage img = r.renderToImage(doc);
    const QUuid id = QUuid::createUuid();
    const QString path = generatedDir() + QStringLiteral("/title-%1.png").arg(id.toString(QUuid::WithoutBraces));
    if (img.isNull() || !img.save(path, "PNG")) {
        if (error)
            *error = QStringLiteral("Could not write %1").arg(path);
        return false;
    }
    out->id = id;
    out->path = path;
    out->name = name;
    out->kind = MediaKind::Image;
    out->duration = 0;
    out->resolution = img.size();
    out->frameRate = 0;
    out->hasVideo = true;
    out->hasAudio = false;
    out->titleDesign = doc.toJson();
    return true;
}

int restoreMissingTitles(QVector<MediaItem> &media)
{
    int n = 0;
    for (auto &m : media) {
        if (!m.isTitle() || QFileInfo::exists(m.path))
            continue;
        MediaItem fresh;
        if (renderTitleMedia(DesignDocument::fromJson(m.titleDesign), m.name, &fresh, nullptr)) {
            m.path = fresh.path;
            ++n;
        }
    }
    return n;
}

// ---- SRT -------------------------------------------------------------------

namespace {

TimeUs parseSrtTime(const QString &s)
{
    // HH:MM:SS,mmm (also accepts '.')
    static const QRegularExpression re(QStringLiteral("(\\d+):(\\d+):(\\d+)[,.](\\d+)"));
    const auto m = re.match(s);
    if (!m.hasMatch())
        return -1;
    const qint64 ms = m.captured(1).toLongLong() * 3600000 + m.captured(2).toLongLong() * 60000
        + m.captured(3).toLongLong() * 1000 + m.captured(4).left(3).leftJustified(3, QLatin1Char('0')).toLongLong();
    return msToUs(ms);
}

QString srtTime(TimeUs t)
{
    const qint64 ms = std::max<qint64>(0, usToMs(t));
    return QStringLiteral("%1:%2:%3,%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace

QVector<Caption> parseSrt(const QString &content)
{
    QString text = content;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    QVector<Caption> out;
    static const QRegularExpression blank(QStringLiteral("\n\\s*\n"));
    static const QRegularExpression tags(QStringLiteral("<[^>]+>"));
    for (const QString &block : text.split(blank, Qt::SkipEmptyParts)) {
        QStringList lines = block.trimmed().split(QLatin1Char('\n'));
        int ti = -1;
        for (int i = 0; i < lines.size() && i < 2; ++i) {
            if (lines[i].contains(QStringLiteral("-->")))
                ti = i;
        }
        if (ti < 0)
            continue;
        const QStringList parts = lines[ti].split(QStringLiteral("-->"));
        if (parts.size() != 2)
            continue;
        Caption c;
        c.start = parseSrtTime(parts[0]);
        c.end = parseSrtTime(parts[1]);
        if (c.start < 0 || c.end <= c.start)
            continue;
        c.text = lines.mid(ti + 1).join(QLatin1Char('\n')).remove(tags).trimmed();
        if (!c.text.isEmpty())
            out.append(c);
    }
    std::sort(out.begin(), out.end(), [](const Caption &a, const Caption &b) { return a.start < b.start; });
    return out;
}

QString toSrt(const QVector<Caption> &captions)
{
    QString s;
    int n = 1;
    for (const auto &c : captions) {
        s += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(n++).arg(srtTime(c.start), srtTime(c.end), c.text);
    }
    return s;
}

} // namespace spotlight::titles

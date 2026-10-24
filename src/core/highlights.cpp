#include "core/highlights.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace spotlight::highlights {

namespace {

constexpr double kFloorDb = -90.0;

double median(QVector<double> v)
{
    if (v.isEmpty())
        return 0;
    const auto mid = v.begin() + v.size() / 2;
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

double percentile(QVector<double> v, double p)
{
    if (v.isEmpty())
        return 0;
    std::sort(v.begin(), v.end());
    return v[std::clamp(int(p * (v.size() - 1)), 0, int(v.size()) - 1)];
}

} // namespace

QStringList analysisArgs(const QString &path, bool hasVideo, bool useScenes)
{
    // Audio: 1-second RMS levels. Video (optional): scene changes on a tiny,
    // 4 fps copy of the picture, which keeps hour-long VODs fast to scan.
    QString graph = QStringLiteral("[0:a]aresample=16000,asetnsamples=n=16000:p=0,astats=metadata=1:reset=1,"
                                   "ametadata=mode=print:key=lavfi.astats.Overall.RMS_level:file=-[a]");
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-nostdin"),
                     QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-i"), path};
    const bool scenes = hasVideo && useScenes;
    if (scenes)
        graph += QStringLiteral(";[0:v]fps=4,scale=160:-2,scdet=threshold=12[v]");
    args << QStringLiteral("-filter_complex") << graph << QStringLiteral("-map") << QStringLiteral("[a]")
         << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");
    if (scenes)
        args << QStringLiteral("-map") << QStringLiteral("[v]") << QStringLiteral("-f") << QStringLiteral("null")
             << QStringLiteral("-");
    return args;
}

Signals parseAnalysis(const QString &log, double durationSeconds)
{
    static const QRegularExpression ptsRe(QStringLiteral("pts_time:([0-9.]+)"));
    static const QRegularExpression rmsRe(QStringLiteral("lavfi\\.astats\\.Overall\\.RMS_level=(\\S+)"));
    static const QRegularExpression sceneRe(QStringLiteral("lavfi\\.scd\\.time:\\s*([0-9.]+)"));
    Signals s;
    s.duration = durationSeconds;
    const int n = std::max(1, int(std::ceil(durationSeconds)));
    s.loudnessDb = QVector<double>(n, kFloorDb);
    int second = -1;
    for (const QString &line : log.split(QLatin1Char('\n'))) {
        auto pm = ptsRe.match(line);
        if (pm.hasMatch()) {
            second = int(std::floor(pm.captured(1).toDouble()));
            continue;
        }
        auto rm = rmsRe.match(line);
        if (rm.hasMatch() && second >= 0) {
            bool ok = false;
            double db = rm.captured(1).toDouble(&ok);
            if (!ok || !std::isfinite(db))
                db = kFloorDb; // "-inf" = digital silence
            if (second >= s.loudnessDb.size())
                s.loudnessDb.resize(second + 1);
            s.loudnessDb[second] = std::max(kFloorDb, db);
            continue;
        }
        auto sm = sceneRe.match(line);
        if (sm.hasMatch())
            s.sceneCuts.append(sm.captured(1).toDouble());
    }
    if (s.duration <= 0)
        s.duration = s.loudnessDb.size();
    return s;
}

QVector<double> excitement(const Signals &s, bool useScenes)
{
    const QVector<double> &L = s.loudnessDb;
    const int n = L.size();
    QVector<double> score(n, 0.0);
    if (n == 0)
        return score;

    // Typical level of the whole video and its spread: a moment counts as
    // "loud" relative to how this particular video normally sounds.
    QVector<double> voiced;
    for (double v : L) {
        if (v > -70)
            voiced.append(v);
    }
    const double typical = voiced.isEmpty() ? -40 : median(voiced);
    const double spread = voiced.isEmpty() ? 6 : std::max(3.0, percentile(voiced, 0.9) - percentile(voiced, 0.1));

    // Local baseline: median of the surrounding minute, so a hype moment
    // stands out against its own scene rather than the whole stream.
    const int half = 30;
    QVector<double> window;
    for (int i = 0; i < n; ++i) {
        window.clear();
        for (int j = std::max(0, i - half); j <= std::min(n - 1, i + half); ++j)
            window.append(L[j]);
        const double base = median(window);
        const double rise = std::max(0.0, L[i] - base) / spread;        // sudden burst
        const double loud = std::max(0.0, L[i] - typical) / spread;     // generally loud
        score[i] = 2.0 * rise + 1.0 * loud;
    }

    if (useScenes && !s.sceneCuts.isEmpty()) {
        // Cuts per second in a 6-second neighbourhood: action, montages.
        QVector<double> cuts(n, 0.0);
        for (double t : s.sceneCuts) {
            const int i = int(t);
            for (int j = std::max(0, i - 3); j <= std::min(n - 1, i + 3); ++j)
                cuts[j] += 1.0;
        }
        for (int i = 0; i < n; ++i)
            score[i] += 0.35 * std::min(cuts[i], 6.0);
    }

    // Smooth over 3 seconds so single blips don't dominate.
    QVector<double> smooth(n, 0.0);
    for (int i = 0; i < n; ++i) {
        double sum = 0;
        int c = 0;
        for (int j = std::max(0, i - 1); j <= std::min(n - 1, i + 1); ++j, ++c)
            sum += score[j];
        smooth[i] = sum / c;
    }
    return smooth;
}

QVector<Highlight> findHighlights(const Signals &s, const Options &opt)
{
    QVector<Highlight> out;
    const QVector<double> E = excitement(s, opt.useScenes);
    const int n = E.size();
    if (n < 3)
        return out;
    const int len = std::clamp(int(std::lround(opt.clipSeconds)), 5, std::max(5, n));
    const int lead = int(len * 0.6); // the build-up matters: most of the clip comes before the peak

    // Only moments clearly above this video's normal level qualify.
    const double sens = std::clamp(opt.sensitivity, 0.0, 1.0);
    const double threshold = percentile(E, 0.97 - 0.25 * sens) + 0.15;

    // Candidate peaks: local maxima above the threshold, strongest first.
    QVector<int> peaks;
    for (int i = 0; i < n; ++i) {
        const double v = E[i];
        if (v < threshold)
            continue;
        if ((i > 0 && E[i - 1] > v) || (i + 1 < n && E[i + 1] >= v))
            continue;
        peaks.append(i);
    }
    std::sort(peaks.begin(), peaks.end(), [&](int a, int b) { return E[a] > E[b]; });

    const double maxE = std::max(1e-6, *std::max_element(E.begin(), E.end()));
    struct Window {
        int a, b;
    };
    QVector<Window> taken;
    const int gap = std::max(3, len / 6);
    for (int p : peaks) {
        if (out.size() >= opt.maxClips)
            break;
        int a = std::clamp(p - lead, 0, std::max(0, n - len));
        int b = std::min(n, a + len);
        // Start/end on the quietest second nearby: cleaner cuts.
        auto quietest = [&](int from, int to) {
            int best = std::clamp(from, 0, n - 1);
            for (int i = std::max(0, from); i <= std::min(n - 1, to); ++i) {
                if (s.loudnessDb[i] < s.loudnessDb[best])
                    best = i;
            }
            return best;
        };
        a = std::min(quietest(a - 2, a + 2), p - 1);
        b = std::max(quietest(b - 2, b + 2) + 1, p + 2);
        a = std::max(0, a);
        b = std::min(n, b);
        bool overlaps = false;
        for (const auto &w : taken) {
            if (a < w.b + gap && b > w.a - gap)
                overlaps = true;
        }
        if (overlaps || b - a < 3)
            continue;
        taken.append({a, b});

        // Score: the peak plus how strong the whole clip is.
        double sum = 0;
        for (int i = a; i < b; ++i)
            sum += E[i];
        const double mean = sum / (b - a);
        Highlight h;
        h.start = secondsToUs(a);
        h.end = std::min(secondsToUs(b), secondsToUs(s.duration));
        h.score = std::clamp(100.0 * (0.7 * E[p] + 0.3 * mean) / maxE, 1.0, 100.0);

        // Why it was picked, from the dominant signal at the peak.
        int cutsNear = 0;
        for (double t : s.sceneCuts)
            cutsNear += std::abs(t - p) <= 4 ? 1 : 0;
        const double before = s.loudnessDb[std::max(0, p - 8)];
        if (opt.useScenes && cutsNear >= 4)
            h.reason = QStringLiteral("Fast action");
        else if (s.loudnessDb[p] - before > 10)
            h.reason = QStringLiteral("Big reaction");
        else
            h.reason = QStringLiteral("Loud moment");
        out.append(h);
    }
    std::sort(out.begin(), out.end(), [](const Highlight &x, const Highlight &y) { return x.score > y.score; });
    return out;
}

} // namespace spotlight::highlights

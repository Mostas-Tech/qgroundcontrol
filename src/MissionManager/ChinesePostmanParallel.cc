#include "ChinesePostmanParallel.h"

#include <algorithm>
#include <limits>
#include <cmath>

// -------- small math helpers --------
QPointF ChinesePostmanParallel::makeUnit(const QPointF& v) {
    const double n = std::hypot(v.x(), v.y());
    if (n <= 0) return QPointF(1, 0);
    return QPointF(v.x() / n, v.y() / n);
}
double  ChinesePostmanParallel::dot(const QPointF& a, const QPointF& b) { return a.x()*b.x() + a.y()*b.y(); }
QPointF ChinesePostmanParallel::sub(const QPointF& a, const QPointF& b) { return {a.x()-b.x(), a.y()-b.y()}; }
double  ChinesePostmanParallel::norm(const QPointF& a) { return std::hypot(a.x(), a.y()); }
double  ChinesePostmanParallel::dist(const QPointF& a, const QPointF& b) { return norm(sub(a, b)); }
bool    ChinesePostmanParallel::finitePoint(const QPointF& p) { return qIsFinite(p.x()) && qIsFinite(p.y()); }

// -------- ctor --------
ChinesePostmanParallel::ChinesePostmanParallel(const QList<QLineF>& segments,
                                               const QList<QPolygonF>& fencePolys)
    : segs(segments),
      polys(fencePolys.begin(), fencePolys.end()) {}

// -------- public API --------
QList<QPointF> ChinesePostmanParallel::solve() {
    lastFeasible = false;
    errorMsg.clear();

    if (segs.isEmpty()) {
        lastFeasible = true;
        return {};
    }
    if (!prepare()) {
        return {};
    }

    QVector<int> order = initialOrderGreedy();
    if (order.isEmpty()) order = initialOrderSweep();
    if (order.isEmpty()) {
        errorMsg = "Failed to construct an initial visiting order.";
        return {};
    }

    improveOrder2Opt(order, deterministicPasses);

    QVector<int> entrySide, parentSide;
    int startSide = 0;
    double bestCost = orientationRingDP(order, &entrySide, &parentSide, &startSide);
    if (!(bestCost < std::numeric_limits<double>::infinity())) {
        QVector<int> alt = initialOrderSweep();
        if (!alt.isEmpty()) {
            improveOrder2Opt(alt, deterministicPasses + 1);
            bestCost = orientationRingDP(alt, &entrySide, &parentSide, &startSide);
            if (bestCost < std::numeric_limits<double>::infinity()) order = alt;
        }
    }

    if (!(bestCost < std::numeric_limits<double>::infinity())) {
        errorMsg = "No closed feasible tour (fences block required connections).";
        return {};
    }

    QList<QPointF> route = buildRoute(order, entrySide, startSide);
    if (route.isEmpty()) {
        errorMsg = "Failed to build route polyline.";
        return {};
    }
    lastFeasible = true;
    return route;
}

// -------- pipeline pieces --------
bool ChinesePostmanParallel::prepare() {
    // Axis: use first segment direction
    const QLineF s0 = segs.first();
    const QPointF dir(s0.p2().x() - s0.p1().x(), s0.p2().y() - s0.p1().y());
    axisU = makeUnit(dir);
    axisV = QPointF(-axisU.y(), axisU.x());

    // Validate polygons (must be 3+ vertices)
    for (int i = 0; i < polys.size(); ++i) {
        if (polys[i].size() < 3) {
            errorMsg = "A fence polygon has fewer than 3 vertices.";
            return false;
        }
    }

    // Build endpoints
    classifySegmentsLR();
    // Build costs (straight hops only, subject to fences)
    buildEndpointCostMatrix();

    // quick feasibility sanity: at least one finite hop exists
    bool anyHop = false;
    for (int si = 0; si < segs.size() && !anyHop; ++si) {
        for (int ei = 0; ei < 2 && !anyHop; ++ei) {
            const int e_from = epIndex(si, 1 - ei);
            for (int sj = 0; sj < segs.size() && !anyHop; ++sj) if (sj != si) {
                for (int ej = 0; ej < 2; ++ej) {
                    const int e_to = epIndex(sj, ej);
                    if (epCostAt(e_from, e_to) < 1e99) { anyHop = true; break; }
                }
            }
        }
    }
    if (!anyHop) {
        errorMsg = "No feasible hops with current fences.";
        return false;
    }

    return true;
}

void ChinesePostmanParallel::classifySegmentsLR() {
    const int N = segs.size();
    segLen.resize(N);
    segA.resize(N);
    segB.resize(N);
    endpoints.resize(qsizetype(N) * 2);

    for (int i = 0; i < N; ++i) {
        const QLineF l = segs[i];
        const QPointF p1 = l.p1();
        const QPointF p2 = l.p2();
        const double s1 = dot(p1, axisU);
        const double s2 = dot(p2, axisU);
        if (s1 <= s2) { segA[i] = p1; segB[i] = p2; }
        else          { segA[i] = p2; segB[i] = p1; }
        segLen[i] = dist(segA[i], segB[i]);

        endpoints[2 * i + 0] = { segA[i], i, 0, -1, 0.0 };
        endpoints[2 * i + 1] = { segB[i], i, 1, -1, 0.0 };
    }
}

void ChinesePostmanParallel::buildEndpointCostMatrix() {
    const qsizetype m = endpoints.size();

    // overflow-safe check for m*m
    if (m > 0 && m > std::numeric_limits<qsizetype>::max() / m) {
        errorMsg = "Too many segments (cost matrix overflow).";
        epCost.clear();
        return;
    }
    epCost.clear();
    epCost.resize(m * m, std::numeric_limits<double>::infinity());
    Q_ASSERT_X(epCost.size() == m * m, "buildEndpointCostMatrix", "resize failed");

    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < m; ++j) {
            if (i == j) continue;
            const QPointF& a = endpoints[i].p;
            const QPointF& b = endpoints[j].p;
            if (isStraightHopAllowed(a, b)) {
                const double d = dist(a, b);
                if (qIsFinite(d)) setEpCostAt(i, j, d);
            }
        }
    }
}

// -------- feasibility checks --------
bool ChinesePostmanParallel::isStraightHopAllowed(const QPointF& a, const QPointF& b) const {
    if (polys.isEmpty()) return true;
    if (dist(a, b) <= epsilon) return true;
    return !segmentCrossesInteriorAnyPoly(a, b);
}

bool ChinesePostmanParallel::segmentCrossesInteriorAnyPoly(const QPointF& a, const QPointF& b) const {
    if (!finitePoint(a) || !finitePoint(b)) {
        qWarning("NaN/Inf in segment endpoints");
        return true; // treat as blocked
    }

    const QLineF seg(a, b);
    for (int p = 0; p < polys.size(); ++p) {
        const QPolygonF& poly = polys[p];
        const int n = poly.size();
        if (n < 3) continue;

        // If either endpoint is inside, the hop is invalid.
        if (poly.containsPoint(a, Qt::OddEvenFill) || poly.containsPoint(b, Qt::OddEvenFill))
            return true;

        // Quick AABB reject
        const QRectF bb(std::min(a.x(), b.x()), std::min(a.y(), b.y()),
                        std::abs(a.x() - b.x()), std::abs(a.y() - b.y()));
        if (!bb.intersects(poly.boundingRect()))
            continue;

        // Any bounded intersection with an edge -> crossing
        for (int i = 0; i < n; ++i) {
            const QLineF edge(poly[i], poly[(i + 1) % n]);
            QPointF ip;
            if (seg.intersects(edge, &ip) == QLineF::BoundedIntersection)
                return true;
        }
    }
    return false;
}

// -------- ordering & DP --------
QVector<int> ChinesePostmanParallel::initialOrderGreedy() {
    const int N = segs.size();
    if (N == 0) return {};
    QVector<int> order; order.reserve(N);
    QVector<bool> used(N, false);

    int cur = 0;
    order.push_back(cur);
    used[cur] = true;

    for (int k = 1; k < N; ++k) {
        double best = std::numeric_limits<double>::infinity();
        int bestj = -1;
        for (int j = 0; j < N; ++j) if (!used[j]) {
            double m = std::numeric_limits<double>::infinity();
            for (int ei = 0; ei < 2; ++ei)
                for (int ej = 0; ej < 2; ++ej)
                    m = std::min(m, hopCostEndpoint(cur, ei, j, ej));
            if (m < best) { best = m; bestj = j; }
        }
        if (bestj < 0) return {};
        cur = bestj; order.push_back(cur); used[cur] = true;
    }
    return order;
}

QVector<int> ChinesePostmanParallel::initialOrderSweep() {
    const int N = segs.size();
    struct Item { int idx; double y; };
    QVector<Item> v; v.reserve(N);
    for (int i = 0; i < N; ++i) {
        const QPointF mid(0.5 * (segs[i].p1().x() + segs[i].p2().x()),
                          0.5 * (segs[i].p1().y() + segs[i].p2().y()));
        v.push_back({i, dot(mid, axisV)});
    }
    std::sort(v.begin(), v.end(), [](const Item& a, const Item& b){ return a.y < b.y; });
    QVector<int> ord; ord.reserve(N);
    for (auto& it : v) ord.push_back(it.idx);
    return ord;
}

double ChinesePostmanParallel::orientationRingDP(const QVector<int>& order,
                                                 QVector<int>* entrySideOut,
                                                 QVector<int>* /*parentSideOut*/,
                                                 int* bestStartSideOut) const {
    const int N = order.size();
    if (N == 0) {
        if (entrySideOut) entrySideOut->clear();
        if (bestStartSideOut) *bestStartSideOut = 0;
        return 0.0;
    }

    QVector<double> dpPrev(2, std::numeric_limits<double>::infinity()),
                    dpCur (2, std::numeric_limits<double>::infinity());
    QVector<QVector<int>> parent(N, QVector<int>(2, -1));
    double totalSegLen = 0.0;
    for (int k = 0; k < N; ++k) totalSegLen += segLen[order[k]];

    double bestTotal = std::numeric_limits<double>::infinity();
    int bestStartSide = 0;

    for (int startSide = 0; startSide < 2; ++startSide) {
        std::fill(dpPrev.begin(), dpPrev.end(), std::numeric_limits<double>::infinity());
        dpPrev[startSide] = 0.0;
        for (int k = 0; k < N - 1; ++k) {
            const int i = order[k];
            const int j = order[k + 1];
            dpCur[0] = dpCur[1] = std::numeric_limits<double>::infinity();
            for (int ei = 0; ei < 2; ++ei) {
                const double dpi = dpPrev[ei];
                if (!(dpi < 1e99)) continue;
                for (int ej = 0; ej < 2; ++ej) {
                    const double h = hopCostEndpoint(i, ei, j, ej);
                    if (!(h < 1e99)) continue;
                    const double val = dpi + h;
                    if (val < dpCur[ej]) { dpCur[ej] = val; parent[k + 1][ej] = ei; }
                }
            }
            dpPrev = dpCur;
        }
        for (int eLast = 0; eLast < 2; ++eLast) {
            const double costOpen = dpPrev[eLast];
            if (!(costOpen < 1e99)) continue;
            const double hClose = hopCostEndpoint(order[N - 1], eLast, order[0], startSide);
            if (!(hClose < 1e99)) continue;
            const double total = totalSegLen + (costOpen + hClose);
            if (total < bestTotal) { bestTotal = total; bestStartSide = startSide; }
        }
    }

    if (!(bestTotal < 1e99)) {
        if (entrySideOut) entrySideOut->clear();
        if (bestStartSideOut) *bestStartSideOut = 0;
        return std::numeric_limits<double>::infinity();
    }

    // Re-run to reconstruct entry sides with the best start
    const int startSide = bestStartSide;
    QVector<QVector<int>> parent2(N, QVector<int>(2, -1));
    std::fill(dpPrev.begin(), dpPrev.end(), std::numeric_limits<double>::infinity());
    dpPrev[startSide] = 0.0;
    for (int k = 0; k < N - 1; ++k) {
        const int i = order[k];
        const int j = order[k + 1];
        dpCur[0] = dpCur[1] = std::numeric_limits<double>::infinity();
        for (int ei = 0; ei < 2; ++ei) {
            const double dpi = dpPrev[ei];
            if (!(dpi < 1e99)) continue;
            for (int ej = 0; ej < 2; ++ej) {
                const double h = hopCostEndpoint(i, ei, j, ej);
                if (!(h < 1e99)) continue;
                const double val = dpi + h;
                if (val < dpCur[ej]) { dpCur[ej] = val; parent2[k + 1][ej] = ei; }
            }
        }
        dpPrev = dpCur;
    }
    int bestLastSide = -1; double bestVal = std::numeric_limits<double>::infinity();
    for (int eLast = 0; eLast < 2; ++eLast) {
        const double costOpen = dpPrev[eLast];
        if (!(costOpen < 1e99)) continue;
        const double hClose = hopCostEndpoint(order[N - 1], eLast, order[0], startSide);
        if (!(hClose < 1e99)) continue;
        const double val = costOpen + hClose;
        if (val < bestVal) { bestVal = val; bestLastSide = eLast; }
    }
    if (bestLastSide < 0) return std::numeric_limits<double>::infinity();

    QVector<int> entrySide(N, 0);
    entrySide[0] = startSide;
    entrySide[N - 1] = bestLastSide;
    for (int k = N - 1; k >= 1; --k) {
        const int e_k = entrySide[k];
        int e_prev = parent2[k][e_k];
        if (e_prev < 0) e_prev = 0;
        entrySide[k - 1] = e_prev;
    }

    if (entrySideOut) *entrySideOut = entrySide;
    if (bestStartSideOut) *bestStartSideOut = startSide;
    return totalSegLen + bestVal;
}

bool ChinesePostmanParallel::improveOrder2Opt(QVector<int>& order, int maxPasses) {
    const int N = order.size();
    if (N < 4) return false;
    bool improvedAny = false;

    QVector<int> e0, p0; int s0 = 0;
    double bestCost = orientationRingDP(order, &e0, &p0, &s0);
    if (!(bestCost < 1e99)) return false;

    for (int pass = 0; pass < maxPasses; ++pass) {
        bool improvedThisPass = false;
        for (int i = 0; i <= N - 4; ++i) {
            for (int j = i + 2; j <= N - 1; ++j) {
                QVector<int> cand = order;
                std::reverse(cand.begin() + i + 1, cand.begin() + j + 1);

                QVector<int> entry, parent; int startSide = 0;
                const double candCost = orientationRingDP(cand, &entry, &parent, &startSide);
                if (candCost < bestCost - 1e-9) {
                    order.swap(cand);
                    bestCost = candCost;
                    improvedAny = improvedThisPass = true;
                }
            }
        }
        if (!improvedThisPass) break; // local optimum reached
    }
    return improvedAny;
}

// -------- helpers --------
int ChinesePostmanParallel::epIndex(int s, int side) const {
    Q_ASSERT_X(s >= 0 && s < segs.size(), "epIndex", "segment index out of range");
    Q_ASSERT_X(side == 0 || side == 1, "epIndex", "side must be 0 or 1");
    const qsizetype idx = qsizetype(2) * s + side;
    Q_ASSERT_X(idx >= 0 && idx < M(), "epIndex", "endpoint index out of range");
    return int(idx);
}

double ChinesePostmanParallel::epCostAt(int ei, int ej) const {
    const qsizetype m = M();
    Q_ASSERT_X(ei >= 0 && ei < m, "epCostAt", "ei out of range");
    Q_ASSERT_X(ej >= 0 && ej < m, "epCostAt", "ej out of range");
    const qsizetype k = qsizetype(ei) * m + ej;
    Q_ASSERT_X(k >= 0 && k < epCost.size(), "epCostAt", "flat index out of range");
    return epCost[k];
}

void ChinesePostmanParallel::setEpCostAt(int ei, int ej, double v) {
    const qsizetype m = M();
    Q_ASSERT_X(ei >= 0 && ei < m, "setEpCostAt", "ei out of range");
    Q_ASSERT_X(ej >= 0 && ej < m, "setEpCostAt", "ej out of range");
    const qsizetype k = qsizetype(ei) * m + ej;
    Q_ASSERT_X(k >= 0 && k < epCost.size(), "setEpCostAt", "flat index out of range");
    epCost[k] = v;
}

double ChinesePostmanParallel::hopCostEndpoint(int si, int entrySideFrom, int sj, int entrySideTo) const {
    const int ei = epIndex(si, 1 - entrySideFrom);  // exit of i
    const int ej = epIndex(sj, entrySideTo);        // entry of j
    return epCostAt(ei, ej);
}

// -------- route build --------
QList<QPointF> ChinesePostmanParallel::buildRoute(const QVector<int>& order,
                                                  const QVector<int>& entrySide, int /*startSide*/) const {
    QList<QPointF> path;
    const int N = order.size();
    if (N == 0) return path;

    Q_ASSERT_X(N == entrySide.size(), "buildRoute", "entrySide length mismatch");
    for (int k = 0; k < N; ++k) {
        Q_ASSERT_X(order[k] >= 0 && order[k] < segs.size(), "buildRoute", "order[k] invalid");
        Q_ASSERT_X(entrySide[k] == 0 || entrySide[k] == 1, "buildRoute", "entrySide[k] invalid");
    }

    auto pushNoDup = [&](const QPointF& p) {
        if (!path.isEmpty() && dist(path.back(), p) <= epsilon) return;
        path.push_back(p);
    };

    auto addHop = [&](int ei, int ej) -> bool {
        const double best = epCostAt(ei, ej);
        if (!(best < 1e99)) return false;
        pushNoDup(endpoints[ej].p);
        return true;
    };

    // start at first segment entry endpoint
    const int s0 = order[0];
    const int e0 = entrySide[0];
    const int ep0 = epIndex(s0, e0);
    pushNoDup(endpoints[ep0].p);

    // traverse first segment
    int exit0 = epIndex(s0, 1 - e0);
    pushNoDup(endpoints[exit0].p);

    for (int k = 0; k < N - 1; ++k) {
        const int si = order[k];
        const int sj = order[k + 1];
        const int ei = epIndex(si, 1 - entrySide[k]);
        const int ej = epIndex(sj, entrySide[k + 1]);
        if (!addHop(ei, ej)) return {};
        const int exitj = epIndex(sj, 1 - entrySide[k + 1]);
        pushNoDup(endpoints[exitj].p);
    }

    // close tour
    const int slast = order[N - 1];
    const int eLastExit = epIndex(slast, 1 - entrySide[N - 1]);
    if (!addHop(eLastExit, ep0)) return {};
    if (dist(path.front(), path.back()) > epsilon) pushNoDup(path.front());
    return path;
}

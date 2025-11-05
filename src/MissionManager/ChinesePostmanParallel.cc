#include "ChinesePostmanParallel.h"
#include <QtGlobal>
#include <QtMath>
#include <QRandomGenerator>
#include <limits>
#include <algorithm>

static constexpr double INF = 1e100;

static QPointF makeUnit(const QPointF& v) {
    double n = std::hypot(v.x(), v.y());
    if (n <= 0) return QPointF(1, 0);
    return QPointF(v.x() / n, v.y() / n);
}

ChinesePostmanParallel::ChinesePostmanParallel(const QList<QLineF>& segments, const QList<QLineF>& fences)
    : segs(segments), fencesIn(fences) { }

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

    // Try to get an initial order and refine it.
    QVector<int> order = initialOrderGreedy();
    if (order.isEmpty()) {
        order = initialOrderSweep();
    }
    if (order.isEmpty()) {
        errorMsg = "Failed to construct an initial visiting order.";
        return {};
    }

    // Improve order with sampled 2-opt
    improveOrder2Opt(order, maxImproveRounds);

    // Optimize orientations on this cycle via ring DP
    QVector<int> entrySide, parentSide;
    int startSide = 0;
    double bestCost = orientationRingDP(order, &entrySide, &parentSide, &startSide);

    if (!(bestCost < INF / 2)) {
        // Try a fallback: alternative initial order
        QVector<int> alt = initialOrderSweep();
        if (!alt.isEmpty()) {
            improveOrder2Opt(alt, maxImproveRounds);
            bestCost = orientationRingDP(alt, &entrySide, &parentSide, &startSide);
            if (bestCost < INF / 2) order = alt;
        }
    }

    if (!(bestCost < INF / 2)) {
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

// ------------------ Preparation ------------------

bool ChinesePostmanParallel::prepare() {
    // Axis: use first segment direction
    QLineF s0 = segs.first();
    QPointF dir = QPointF(s0.p2().x() - s0.p1().x(), s0.p2().y() - s0.p1().y());
    axisU = makeUnit(dir);
    axisV = QPointF(-axisU.y(), axisU.x());

    // Build polygons from fence lines
    if (!buildPolygons()) {
        errorMsg = "Fences do not form closed polygons.";
        return false;
    }

    // Classify segments L/R and build endpoint array
    classifySegmentsLR();

    // Precompute boundary location (if endpoints lie on fence boundaries)
    precomputeBoundaryLocationsForEndpoints();

    // Build endpoint cost matrix
    buildEndpointCostMatrix();

    return true;
}

bool ChinesePostmanParallel::buildPolygons() {
    polys.clear();
    if (fencesIn.isEmpty()) return true; // no fences = no polygons
    return stitchPolygons(fencesIn, polys);
}

static uint64_t hashPoint(const QPointF& p, double eps) {
    // Grid hash with epsilon cell size
    long long xi = (long long) std::llround(p.x() / eps);
    long long yi = (long long) std::llround(p.y() / eps);
    return (uint64_t) (xi * 1315423911ULL) ^ (uint64_t) (yi * 2654435761ULL);
}

bool ChinesePostmanParallel::stitchPolygons(QList<QLineF> lines, QVector<FencePolygon>& out) {
    // Build adjacency using epsilon merge
    struct Edge { QPointF a, b; bool used = false; };

    QVector<Edge> edges;
    edges.reserve(lines.size());
    for (const QLineF& l : lines) {
        edges.push_back({ l.p1(), l.p2(), false });
    }

    // Map hashed point -> list of indices
    QHash<uint64_t, QVector<int>> buckets;
    for (int i = 0; i < edges.size(); ++i) {
        buckets[hashPoint(edges[i].a, epsilon)].push_back(i * 2 + 0);
        buckets[hashPoint(edges[i].b, epsilon)].push_back(i * 2 + 1);
    }

    auto canonical = [&](const QPointF& p) -> QPointF {
        uint64_t h = hashPoint(p, epsilon);
        // find a representative close to p to average? Keep original
        return p;
    };

    // Build a multimap point -> incident edge indices (with orientation)
    QHash<uint64_t, QVector<QPair<int, bool>>> incident; // (edgeIdx, isStart)
    for (int i = 0; i < edges.size(); ++i) {
        incident[hashPoint(edges[i].a, epsilon)].push_back({ i, true });
        incident[hashPoint(edges[i].b, epsilon)].push_back({ i, false });
    }

    // Follow loops
    int safeGuard = edges.size() * 4 + 1000;
    QVector<bool> used(edges.size(), false);

    for (int startIdx = 0; startIdx < edges.size(); ++startIdx) {
        if (used[startIdx]) continue;

        // Start a new loop
        QVector<QPointF> loop;
        int eIdx = startIdx;
        bool atStart = true; // we will walk from a->b
        QPointF cur = edges[eIdx].a;
        QPointF next = edges[eIdx].b;
        used[eIdx] = true;
        loop.push_back(cur);

        int safety = safeGuard;
        while (safety-- > 0) {
            loop.push_back(next);

            // Find next edge incident to 'next' that is not used
            uint64_t h = hashPoint(next, epsilon);
            auto it = incident.find(h);
            if (it == incident.end()) break;

            int candidate = -1;
            bool candStart = true;
            for (const auto& pr : it.value()) {
                int idx = pr.first;
                if (used[idx]) continue;
                candidate = idx;
                candStart = pr.second;
                break;
            }
            if (candidate == -1) break; // open chain -> cannot form polygon

            used[candidate] = true;
            QPointF a = edges[candidate].a;
            QPointF b = edges[candidate].b;
            if (!candStart) std::swap(a, b); // orient from 'next'

            cur = next;
            next = b;

            // If we are back to the first vertex, close loop
            if (dist(next, loop.front()) <= epsilon) {
                // Ensure simple loop with at least 3 unique points
                if (loop.size() >= 4) {
                    FencePolygon poly;

                    // Remove duplicate last vertex if exists
                    if (dist(loop.back(), loop.front()) <= epsilon) loop.pop_back();

                    poly.verts = loop;

                    // Compute prefix and perimeter
                    poly.prefix.resize(poly.verts.size() + 1);
                    poly.prefix[0] = 0.0;
                    for (int i = 0; i < poly.verts.size(); ++i) {
                        QPointF u = poly.verts[i];
                        QPointF v = poly.verts[(i + 1) % poly.verts.size()];
                        poly.prefix[i + 1] = poly.prefix[i] + dist(u, v);
                    }
                    poly.perimeter = poly.prefix.back();

                    // bbox
                    poly.bbox = polygonBBox(poly.verts);

                    out.push_back(poly);
                }
                break;
            }
        }

        // If chain didn't close, ignore (not a polygon)
    }

    // Accept even if some lines were not used (they may belong to separate polygons)
    return true;
}

void ChinesePostmanParallel::classifySegmentsLR() {
    int N = segs.size();
    segLen.resize(N);
    segA.resize(N);
    segB.resize(N);
    endpoints.resize(N * 2);

    for (int i = 0; i < N; ++i) {
        QLineF l = segs[i];
        QPointF p1 = l.p1();
        QPointF p2 = l.p2();

        // Project onto axisU to decide L/R
        double s1 = dot(p1, axisU);
        double s2 = dot(p2, axisU);

        if (s1 <= s2) {
            segA[i] = p1; // left
            segB[i] = p2; // right
        } else {
            segA[i] = p2;
            segB[i] = p1;
        }

        segLen[i] = dist(segA[i], segB[i]);
        endpoints[2 * i + 0] = { segA[i], i, 0, -1, 0.0 };
        endpoints[2 * i + 1] = { segB[i], i, 1, -1, 0.0 };
    }
}

void ChinesePostmanParallel::precomputeBoundaryLocationsForEndpoints() {
    for (int ei = 0; ei < endpoints.size(); ++ei) {
        EndpointInfo& E = endpoints[ei];
        for (int p = 0; p < polys.size(); ++p) {
            double s;
            int edgeIdx;
            double t;
            if (locateOnPolygon(p, E.p, s, edgeIdx, t)) {
                E.polyIdx = p;
                E.sPerim = s;
                break;
            }
        }
    }
}

void ChinesePostmanParallel::buildEndpointCostMatrix() {
    int M = endpoints.size();
    epCost.resize(M * M);
    std::fill(epCost.begin(), epCost.end(), INF);

    // Precompute straight-allowed flags between endpoints using polygon bboxes pruning
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < M; ++j) {
            if (i == j) continue;

            const QPointF& a = endpoints[i].p;
            const QPointF& b = endpoints[j].p;
            double best = INF;

            // Straight hop test
            if (isStraightHopAllowed(a, b)) {
                best = dist(a, b);
            }

            // Boundary arc if both lie on same polygon
            int pi = endpoints[i].polyIdx;
            if (pi >= 0 && pi == endpoints[j].polyIdx) {
                double sA = endpoints[i].sPerim;
                double sB = endpoints[j].sPerim;
                double arc = boundaryArcLength(pi, sA, sB);
                if (arc < best) best = arc;
            }

            setEpCostAt(i, j, best);
        }
    }
}

bool ChinesePostmanParallel::isStraightHopAllowed(const QPointF& a, const QPointF& b) const {
    // If no polygons, allowed
    if (polys.isEmpty()) return true;

    // If a==b
    if (dist(a, b) <= epsilon) return true;

    return !segmentCrossesInteriorAnyPoly(a, b);
}

bool ChinesePostmanParallel::segmentCrossesInteriorAnyPoly(const QPointF& a, const QPointF& b) const {
    const QLineF seg(a, b);
    for (int p = 0; p < polys.size(); ++p) {
        const FencePolygon& poly = polys[p];
        const auto& v = poly.verts;
        const int n = v.size();
        if (n < 2) continue;

        for (int i = 0; i < n; ++i) {
            const QPointF& e0 = v[i];
            const QPointF& e1 = v[(i + 1) % n];
            const QLineF edge(e0, e1);

            QPointF ip;
            QLineF::IntersectionType t = seg.intersects(edge, &ip);
            if (t == QLineF::BoundedIntersection) {
                return true;
            }
        }
    }
    return false;
}

// ------------------ Geometry utils ------------------

double ChinesePostmanParallel::dot(const QPointF& a, const QPointF& b) {
    return a.x() * b.x() + a.y() * b.y();
}

double ChinesePostmanParallel::cross(const QPointF& a, const QPointF& b) {
    return a.x() * b.y() - a.y() * b.x();
}

QPointF ChinesePostmanParallel::sub(const QPointF& a, const QPointF& b) {
    return QPointF(a.x() - b.x(), a.y() - b.y());
}

double ChinesePostmanParallel::norm(const QPointF& a) {
    return std::hypot(a.x(), a.y());
}

double ChinesePostmanParallel::dist(const QPointF& a, const QPointF& b) {
    return norm(sub(a, b));
}

bool ChinesePostmanParallel::almostEqual(double a, double b, double eps) {
    return std::abs(a - b) <= eps;
}

bool ChinesePostmanParallel::pointOnSegment(const QPointF& p, const QPointF& a, const QPointF& b, double eps, double* tProj) {
    QPointF ab = sub(b, a);
    QPointF ap = sub(p, a);
    double ab2 = dot(ab, ab);

    if (ab2 <= eps * eps) {
        if (dist(p, a) <= eps) {
            if (tProj) *tProj = 0.0;
            return true;
        }
        if (dist(p, b) <= eps) {
            if (tProj) *tProj = 1.0;
            return true;
        }
        return false;
    }

    double t = dot(ap, ab) / ab2;
    if (t < -eps || t > 1.0 + eps) return false;

    QPointF proj(a.x() + ab.x() * t, a.y() + ab.y() * t);
    if (dist(proj, p) <= eps) {
        if (tProj) *tProj = std::clamp(t, 0.0, 1.0);
        return true;
    }
    return false;
}

// Winding number test; returns +1 for inside, 0 for outside; boundary treated as outside (returns 0).
int ChinesePostmanParallel::pointInPolygonWinding(const QVector<QPointF>& poly, const QPointF& q, double eps) {
    int wn = 0;
    int n = poly.size();
    for (int i = 0; i < n; ++i) {
        QPointF a = poly[i];
        QPointF b = poly[(i + 1) % n];

        // Boundary check
        if (pointOnSegment(q, a, b, eps)) return 0; // boundary treated as outside

        if (a.y() <= q.y()) {
            if (b.y() > q.y()) {
                double isLeft = cross(sub(b, a), sub(q, a));
                if (isLeft > 0) ++wn;
            }
        } else {
            if (b.y() <= q.y()) {
                double isLeft = cross(sub(b, a), sub(q, a));
                if (isLeft < 0) --wn;
            }
        }
    }
    return (wn == 0) ? 0 : 1;
}

// Returns true if segments intersect; t,u are parameters in [0,1]; kind: 1 proper, 2 colinear overlap, 3 endpoint touch
bool ChinesePostmanParallel::segSegIntersectParam(const QPointF& a, const QPointF& b,
                                                  const QPointF& c, const QPointF& d,
                                                  double& t, double& u, int& kind, double eps) {
    QPointF r = sub(b, a);
    QPointF s = sub(d, c);
    double rxs = cross(r, s);
    QPointF cma = sub(c, a);
    double qpxr = cross(cma, r);

    if (std::abs(rxs) <= eps && std::abs(qpxr) <= eps) {
        // Colinear
        double r2 = dot(r, r);
        if (r2 <= eps * eps) {
            kind = 0;
            return false;
        }
        double t0 = dot(sub(c, a), r) / r2;
        double t1 = dot(sub(d, a), r) / r2;
        if (t0 > t1) std::swap(t0, t1);
        double tStart = std::max(0.0, t0);
        double tEnd = std::min(1.0, t1);
        if (tStart <= tEnd + eps) {
            t = tStart;
            u = 0.0;
            kind = 2;
            return true;
        }
        kind = 0;
        return false;
    }

    if (std::abs(rxs) <= eps && std::abs(qpxr) > eps) {
        kind = 0;
        return false; // parallel non-intersecting
    }

    double tnum = cross(cma, s);
    double unum = cross(cma, r);
    t = tnum / rxs;
    u = unum / rxs;

    if (t >= -eps && t <= 1.0 + eps && u >= -eps && u <= 1.0 + eps) {
        kind = (t > eps && t < 1.0 - eps && u > eps && u < 1.0 - eps) ? 1 : 3;
        return true;
    }
    kind = 0;
    return false;
}

// ------------------ Boundary helpers ------------------

bool ChinesePostmanParallel::locateOnPolygon(int polyIdx, const QPointF& p,
                                             double& sPerimOut, int& edgeIndexOut, double& tOnEdgeOut) const {
    const FencePolygon& poly = polys[polyIdx];
    int n = poly.verts.size();
    double sAccum = 0.0;

    for (int i = 0; i < n; ++i) {
        QPointF a = poly.verts[i];
        QPointF b = poly.verts[(i + 1) % n];
        double t;
        if (pointOnSegment(p, a, b, epsilon, &t)) {
            double segLen = dist(a, b);
            sPerimOut = sAccum + t * segLen;
            edgeIndexOut = i;
            tOnEdgeOut = t;
            return true;
        }
        sAccum += dist(a, b);
    }
    return false;
}

double ChinesePostmanParallel::boundaryArcLength(int polyIdx, double sA, double sB) const {
    const FencePolygon& poly = polys[polyIdx];
    double P = poly.perimeter;
    double d = std::abs(sA - sB);
    return std::min(d, P - d);
}

void ChinesePostmanParallel::boundaryArcPoints(int polyIdx, double sA, double sB, bool forward,
                                               QVector<QPointF>& out) const {
    const FencePolygon& poly = polys[polyIdx];
    int n = poly.verts.size();
    double P = poly.perimeter;

    auto advanceIdx = [&](int idx) -> int { return (idx + 1) % n; };
    auto retreatIdx = [&](int idx) -> int { return (idx - 1 + n) % n; };

    // Helper to find the vertex index just after s along perimeter (forward), and t on edge for exact point
    auto locate = [&](double s, int& edgeIdx, double& tOnEdge, QPointF& point) {
        double acc = 0.0;
        for (int i = 0; i < n; ++i) {
            QPointF a = poly.verts[i];
            QPointF b = poly.verts[(i + 1) % n];
            double L = dist(a, b);
            if (acc + L >= s - 1e-12) {
                edgeIdx = i;
                double d = s - acc;
                double t = (L <= 0) ? 0.0 : std::clamp(d / (L + 1e-30), 0.0, 1.0);
                tOnEdge = t;
                point = QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t);
                return;
            }
            acc += L;
        }
        // wrap to last vertex
        edgeIdx = n - 1;
        tOnEdge = 1.0;
        point = poly.verts[0];
    };

    int eA; double tA; QPointF pA;
    int eB; double tB; QPointF pB;
    locate(sA, eA, tA, pA);
    locate(sB, eB, tB, pB);

    out.clear();
    out.push_back(pA);

    if (forward) {
        int i = eA;
        double t = tA;
        QPointF cur = pA;
        while (true) {
            if (i == eB && tB >= t) {
                // same edge, forward to B
                out.push_back(pB);
                break;
            }
            // move to vertex i+1
            QPointF nextV = poly.verts[(i + 1) % n];
            if (dist(cur, nextV) > 0) out.push_back(nextV);
            i = (i + 1) % n;
            t = 0.0;
            cur = nextV;
        }
    } else {
        // backward direction
        int i = eA;
        double t = tA;
        QPointF cur = pA;
        while (true) {
            if (i == eB && tB <= t) {
                out.push_back(pB);
                break;
            }
            // move to vertex i
            QPointF prevV = poly.verts[i];
            if (dist(cur, prevV) > 0) out.push_back(prevV);
            i = (i - 1 + n) % n;
            t = 1.0;
            cur = prevV;
        }
    }
}

QRectF ChinesePostmanParallel::polygonBBox(const QVector<QPointF>& poly) const {
    double minx = poly[0].x(), maxx = poly[0].x();
    double miny = poly[0].y(), maxy = poly[0].y();
    for (const auto& p : poly) {
        minx = std::min(minx, p.x());
        maxx = std::max(maxx, p.x());
        miny = std::min(miny, p.y());
        maxy = std::max(maxy, p.y());
    }
    return QRectF(QPointF(minx, miny), QPointF(maxx, maxy));
}

// ------------------ Ordering & DP ------------------

double ChinesePostmanParallel::hopCostEndpoint(int si, int entrySideFrom, int sj, int entrySideTo) const {
    int ei = epIndex(si, 1 - entrySideFrom); // exit of i
    int ej = epIndex(sj, entrySideTo);       // entry of j
    return epCostAt(ei, ej);
}

QVector<int> ChinesePostmanParallel::initialOrderGreedy() {
    int N = segs.size();
    QVector<int> order;
    order.reserve(N);
    QVector<bool> used(N, false);

    int cur = 0;
    order.push_back(cur);
    used[cur] = true;

    for (int k = 1; k < N; ++k) {
        double best = INF;
        int bestj = -1;
        for (int j = 0; j < N; ++j) if (!used[j]) {
            // approximate minimal hop from cur to j across entry/exit sides
            double m = INF;
            for (int ei = 0; ei < 2; ++ei) {
                for (int ej = 0; ej < 2; ++ej) {
                    m = std::min(m, hopCostEndpoint(cur, ei, j, ej));
                }
            }
            if (m < best) {
                best = m;
                bestj = j;
            }
        }
        if (bestj < 0) return {};
        cur = bestj;
        order.push_back(cur);
        used[cur] = true;
    }
    return order;
}

QVector<int> ChinesePostmanParallel::initialOrderSweep() {
    // Sort by projection onto axisV (perpendicular direction)
    int N = segs.size();
    struct Item { int idx; double y; };

    QVector<Item> v;
    v.reserve(N);
    for (int i = 0; i < N; ++i) {
        QPointF mid(0.5 * (segs[i].p1().x() + segs[i].p2().x()),
                    0.5 * (segs[i].p1().y() + segs[i].p2().y()));
        double y = dot(mid, axisV);
        v.push_back({ i, y });
    }

    std::sort(v.begin(), v.end(), [](const Item& a, const Item& b) { return a.y < b.y; });

    QVector<int> ord;
    ord.reserve(N);
    for (auto& it : v) ord.push_back(it.idx);
    return ord;
}

// Ring DP over a fixed cyclic order; returns total cost = sum(len_i) + min hop sum; INF on infeasible.
double ChinesePostmanParallel::orientationRingDP(const QVector<int>& order, QVector<int>* entrySideOut,
                                                 QVector<int>* parentSideOut, int* bestStartSideOut) const {
    int N = order.size();
    QVector<double> dpPrev(2, INF), dpCur(2, INF);
    QVector<QVector<int>> parent(N, QVector<int>(2, -1));
    double totalSegLen = 0.0;
    for (int k = 0; k < N; ++k) totalSegLen += segLen[order[k]];

    double bestTotal = INF;
    int bestStartSide = 0;
    QVector<int> bestParentSide0(N, -1);

    for (int startSide = 0; startSide < 2; ++startSide) {
        std::fill(dpPrev.begin(), dpPrev.end(), INF);
        dpPrev[startSide] = 0.0; // starting entry side fixed
        parent[0][0] = parent[0][1] = -1; // not used for k=0

        // forward transitions
        for (int k = 0; k < N - 1; ++k) {
            int i = order[k];
            int j = order[k + 1];
            dpCur[0] = dpCur[1] = INF;

            for (int ei = 0; ei < 2; ++ei) {
                double dpi = dpPrev[ei];
                if (!(dpi < INF / 2)) continue;
                for (int ej = 0; ej < 2; ++ej) {
                    double h = hopCostEndpoint(i, ei, j, ej);
                    if (!(h < INF / 2)) continue;
                    double val = dpi + h;
                    if (val < dpCur[ej]) {
                        dpCur[ej] = val;
                        parent[k + 1][ej] = ei;
                    }
                }
            }
            dpPrev = dpCur;
        }

        // closing hop back to start
        for (int eLast = 0; eLast < 2; ++eLast) {
            double costOpen = dpPrev[eLast];
            if (!(costOpen < INF / 2)) continue;
            double hClose = hopCostEndpoint(order[N - 1], eLast, order[0], startSide);
            if (!(hClose < INF / 2)) continue;

            double total = totalSegLen + (costOpen + hClose);
            if (total < bestTotal) {
                bestTotal = total;
                bestStartSide = startSide;
                bestParentSide0 = QVector<int>(N, -1);
                for (int k = 0; k < N; ++k) bestParentSide0[k] = parent[k][0]; // store column 0 for reuse
                // We'll recompute the exact parent chain below
            }
        }
    }

    if (!(bestTotal < INF / 2)) {
        if (entrySideOut) entrySideOut->clear();
        if (parentSideOut) parentSideOut->clear();
        if (bestStartSideOut) *bestStartSideOut = 0;
        return INF;
    }

    // Re-run once with the best start to capture parents accurately
    int startSide = bestStartSide;
    QVector<QVector<int>> parent2(N, QVector<int>(2, -1));
    std::fill(dpPrev.begin(), dpPrev.end(), INF);
    dpPrev[startSide] = 0.0;

    for (int k = 0; k < N - 1; ++k) {
        int i = order[k];
        int j = order[k + 1];
        dpCur[0] = dpCur[1] = INF;

        for (int ei = 0; ei < 2; ++ei) {
            double dpi = dpPrev[ei];
            if (!(dpi < INF / 2)) continue;
            for (int ej = 0; ej < 2; ++ej) {
                double h = hopCostEndpoint(i, ei, j, ej);
                if (!(h < INF / 2)) continue;
                double val = dpi + h;
                if (val < dpCur[ej]) {
                    dpCur[ej] = val;
                    parent2[k + 1][ej] = ei;
                }
            }
        }
        dpPrev = dpCur;
    }

    // choose last side that closes best
    int bestLastSide = -1;
    double bestVal = INF;
    for (int eLast = 0; eLast < 2; ++eLast) {
        double costOpen = dpPrev[eLast];
        if (!(costOpen < INF / 2)) continue;
        double hClose = hopCostEndpoint(order[N - 1], eLast, order[0], startSide);
        if (!(hClose < INF / 2)) continue;
        double val = costOpen + hClose;
        if (val < bestVal) {
            bestVal = val;
            bestLastSide = eLast;
        }
    }
    if (bestLastSide < 0) return INF;

    // Backtrack entry sides
    QVector<int> entrySide(N, 0);
    entrySide[0] = startSide;
    entrySide[N - 1] = bestLastSide;
    for (int k = N - 1; k >= 1; --k) {
        int e_k = entrySide[k];
        int e_prev = parent2[k][e_k];
        if (e_prev < 0) e_prev = 0;
        entrySide[k - 1] = e_prev;
    }

    if (entrySideOut) *entrySideOut = entrySide;
    if (parentSideOut) *parentSideOut = QVector<int>(); // not used
    if (bestStartSideOut) *bestStartSideOut = startSide;

    return totalSegLen + bestVal;
}

bool ChinesePostmanParallel::improveOrder2Opt(QVector<int>& order, int roundsLimit) {
    if (order.size() < 4) return false;

    QRandomGenerator rng = hasSeed ? QRandomGenerator(rngSeed) : *QRandomGenerator::global();
    bool improved = false;

    for (int round = 0; round < roundsLimit; ++round) {
        // Sample a few pairs
        int N = order.size();
        int i = rng.bounded(0, N - 3);
        int j = rng.bounded(i + 2, N - 1);

        // Create candidate by reversing segment [i+1..j]
        QVector<int> cand = order;
        std::reverse(cand.begin() + i + 1, cand.begin() + j + 1);

        QVector<int> entrySide;
        QVector<int> parent;
        int startSide = 0;
        double cCost = orientationRingDP(cand, &entrySide, &parent, &startSide);
        if (!(cCost < INF / 2)) continue;

        QVector<int> e0;
        QVector<int> p0;
        int s0 = 0;
        double oCost = orientationRingDP(order, &e0, &p0, &s0);
        if (!(oCost < INF / 2)) {
            order = cand;
            improved = true;
            continue;
        }
        if (cCost + 1e-9 < oCost) {
            order = cand;
            improved = true;
        }
    }
    return improved;
}

// ------------------ Route reconstruction ------------------

QList<QPointF> ChinesePostmanParallel::buildRoute(const QVector<int>& order,
                                                  const QVector<int>& entrySide,
                                                  int startSide) const {
    QList<QPointF> path;
    int N = order.size();
    if (N == 0) return path;

    auto pushNoDup = [&](const QPointF& p) {
        if (!path.isEmpty()) {
            if (dist(path.back(), p) <= epsilon) return;
        }
        path.push_back(p);
    };

    // Helper to add hop between two endpoints
    auto addHop = [&](int ei, int ej) {
        const EndpointInfo& A = endpoints[ei];
        const EndpointInfo& B = endpoints[ej];
        double best = epCostAt(ei, ej);
        if (!(best < INF / 2)) return false;

        // Compute both candidates to reconstruct polyline
        bool straightOK = isStraightHopAllowed(A.p, B.p);
        double straightCost = straightOK ? dist(A.p, B.p) : INF;

        double arcCost = INF;
        if (A.polyIdx >= 0 && A.polyIdx == B.polyIdx) {
            arcCost = boundaryArcLength(A.polyIdx, A.sPerim, B.sPerim);
        }

        if (straightCost <= arcCost + 1e-9) {
            // Use straight
            pushNoDup(B.p);
            return true;
        } else {
            // Use boundary; decide direction (forward along increasing s or backward)
            const FencePolygon& poly = polys[A.polyIdx];
            double d = std::abs(A.sPerim - B.sPerim);
            bool forward = ((A.sPerim <= B.sPerim) ? (d <= poly.perimeter - d)
                                                   : ((poly.perimeter - d) < d));
            QVector<QPointF> arcPts;
            boundaryArcPoints(A.polyIdx, A.sPerim, B.sPerim, forward, arcPts);
            for (int k = 1; k < arcPts.size(); ++k) { // skip first (already at A)
                pushNoDup(arcPts[k]);
            }
            return true;
        }
    };

    // Start at first segment entry endpoint
    int s0 = order[0];
    int e0 = entrySide[0]; // 0 or 1
    int ep0 = epIndex(s0, e0);
    pushNoDup(endpoints[ep0].p);

    // Traverse first segment to its other endpoint
    int exit0 = epIndex(s0, 1 - e0);
    pushNoDup(endpoints[exit0].p);

    // For each transition
    for (int k = 0; k < N - 1; ++k) {
        int si = order[k];
        int sj = order[k + 1];
        int ei = epIndex(si, 1 - entrySide[k]);    // exit of si
        int ej = epIndex(sj, entrySide[k + 1]);    // entry of sj
        if (!addHop(ei, ej)) return {};

        // traverse segment sj
        int exitj = epIndex(sj, 1 - entrySide[k + 1]);
        pushNoDup(endpoints[exitj].p);
    }

    // Close the tour: last exit -> start entry
    int slast = order[N - 1];
    int eLastExit = epIndex(slast, 1 - entrySide[N - 1]);
    if (!addHop(eLastExit, ep0)) return {};

    // The list is closed at the start point; we can optionally push the start point again to emphasize closure
    if (dist(path.front(), path.back()) > epsilon) pushNoDup(path.front());

    return path;
}

// ------------------ Helpers ------------------

QRectF rectFromTwoPoints(const QPointF& a, const QPointF& b) {
    return QRectF(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                  QPointF(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

// ------------------ END ------------------

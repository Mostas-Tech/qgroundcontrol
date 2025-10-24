// tsp_route.h
#pragma once
#include <QList>
#include <QPointF>
#include <QtMath>
#include <algorithm>

namespace tsp {

// Euclidean distance
inline double dist(const QPointF& a, const QPointF& b) {
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return std::sqrt(dx*dx + dy*dy);
}

// Total length of a closed tour (order contains all indices once; tour is closed a->...->a)
inline double tourLength(const QList<QPointF>& pts, const QList<int>& order) {
    if (order.size() < 2) return 0.0;
    double L = 0.0;
    for (int i = 0; i < order.size() - 1; ++i)
        L += dist(pts[order[i]], pts[order[i+1]]);
    // close loop
    L += dist(pts[order.back()], pts[order.front()]);
    return L;
}

// Build initial tour via nearest neighbor starting at startIdx
inline QList<int> nearestNeighbor(const QList<QPointF>& pts, int startIdx = 0) {
    const int n = pts.size();
    QList<int> order;
    order.reserve(n);
    QVector<bool> used(n, false);

    int current = startIdx;
    order.push_back(current);
    used[current] = true;

    for (int k = 1; k < n; ++k) {
        int best = -1;
        double bestD = std::numeric_limits<double>::infinity();
        for (int i = 0; i < n; ++i) {
            if (used[i]) continue;
            double d = dist(pts[current], pts[i]);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        current = best;
        used[current] = true;
        order.push_back(current);
    }
    return order;
}

// Two-edge swap improvement
inline bool twoOptOnce(const QList<QPointF>& pts, QList<int>& order) {
    // We keep tour closed implicitly (order[0] links to order.back()).
    const int n = order.size();
    bool improved = false;

    auto at = [&](int idx)->int { // wrap helper
        if (idx >= n) idx -= n;
        return idx;
    };

    for (int i = 0; i < n - 1; ++i) {
        int i1 = at(i + 1);
        const QPointF& A = pts[order[i]];
        const QPointF& B = pts[order[i1]];

        for (int k = i + 2; k < n; ++k) {
            int k1 = at(k + 1);
            // Do not break the start–end edge by reversing whole loop segment
            if (k1 == i) continue;

            const QPointF& C = pts[order[k]];
            const QPointF& D = pts[order[k1]];

            double oldLen = dist(A, B) + dist(C, D);
            double newLen = dist(A, C) + dist(B, D);

            if (newLen + 1e-12 < oldLen) {
                // reverse the segment (i1..k)
                std::reverse(order.begin() + i1, order.begin() + k + 1);
                improved = true;
                return improved; // do a single successful swap per pass
            }
        }
    }
    return improved;
}

// Run 2-opt until no improvement or iteration cap
inline void twoOptImprove(const QList<QPointF>& pts, QList<int>& order, int maxPasses = 200) {
    for (int pass = 0; pass < maxPasses; ++pass) {
        if (!twoOptOnce(pts, order)) break;
    }
}

// Main entry: returns a CLOSED route as points with the start repeated at the end.
inline QList<QPointF> computeRoute(const QList<QPointF>& polygonCenters, int startIndex = 0) {
    if (polygonCenters.isEmpty()) return {};
    if (polygonCenters.size() == 1) return { polygonCenters.first(), polygonCenters.first() };

    // 1) Build an initial tour
    QList<int> order = nearestNeighbor(polygonCenters, startIndex);

    // 2) Improve with 2-opt
    twoOptImprove(polygonCenters, order, 300);

    // 3) Materialize closed route (repeat start at end)
    QList<QPointF> route;
    route.reserve(order.size() + 1);
    for (int idx : order) route.push_back(polygonCenters[idx]);
    route.push_back(route.front()); // close the loop
    return route;
}

} // namespace tsp

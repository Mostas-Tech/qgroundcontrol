#pragma once

#include <QList>
#include <QVector>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QHash>
#include <QString>

// A heuristic Chinese-Postman-style tour constructor over disjoint segments,
// optionally respecting "fence" polygons that cannot be crossed.
// See the .cpp for implementation details.
class ChinesePostmanParallel {
public:
    // --- Types ---
    struct FencePolygon {
        QVector<QPointF> verts;   // ordered loop (closed implicitly)
        QVector<double>  prefix;  // cumulative edge lengths; size = verts.size()+1
        double           perimeter = 0.0;
        QRectF           bbox;
    };

    struct EndpointInfo {
        QPointF p;        // endpoint position
        int     segIdx;   // which segment this endpoint belongs to
        int     endSide;  // 0 = left(A), 1 = right(B) in our L/R labeling
        int     polyIdx;  // -1 if not on any polygon boundary, else index into polys
        double  sPerim;   // arclength along polygon perimeter (valid if polyIdx>=0)
    };

public:
    // --- Construction ---
    ChinesePostmanParallel(const QList<QLineF>& segments, const QList<QLineF>& fences);

    // Compute a closed tour polyline (list of vertices). Empty list on failure.
    // Check wasLastFeasible()/lastError() for status.
    QList<QPointF> solve();

    // --- Status / configuration ---
    bool wasLastFeasible() const { return lastFeasible; }
    const QString& lastError() const { return errorMsg; }

    // Tuning parameters
    void setEpsilon(double eps) { epsilon = eps; }
    void setMaxImproveRounds(int rounds) { maxImproveRounds = rounds; }
    void setRandomSeed(quint32 seed) { hasSeed = true; rngSeed = seed; }
    void clearRandomSeed() { hasSeed = false; }

private:
    // --------- Top-level steps ---------
    bool prepare();
    bool buildPolygons();
    bool stitchPolygons(QList<QLineF> lines, QVector<FencePolygon>& out);
    void classifySegmentsLR();
    void precomputeBoundaryLocationsForEndpoints();
    void buildEndpointCostMatrix();

    // --------- Geometry / predicates ---------
    static double dot(const QPointF& a, const QPointF& b);
    static double cross(const QPointF& a, const QPointF& b);
    static QPointF sub(const QPointF& a, const QPointF& b);
    static double norm(const QPointF& a);
    static double dist(const QPointF& a, const QPointF& b);
    static bool   almostEqual(double a, double b, double eps);

    static bool pointOnSegment(const QPointF& p, const QPointF& a, const QPointF& b,
                               double eps, double* tProj = nullptr);

    // Winding-number based point-in-polygon (boundary counts as outside => 0)
    static int pointInPolygonWinding(const QVector<QPointF>& poly, const QPointF& q, double eps);

    // Segment/segment intersection (with param outputs)
    static bool segSegIntersectParam(const QPointF& a, const QPointF& b,
                                     const QPointF& c, const QPointF& d,
                                     double& t, double& u, int& kind, double eps);

    // Polygon boundary helpers
    bool   locateOnPolygon(int polyIdx, const QPointF& p,
                           double& sPerimOut, int& edgeIndexOut, double& tOnEdgeOut) const;

    double boundaryArcLength(int polyIdx, double sA, double sB) const;

    void   boundaryArcPoints(int polyIdx, double sA, double sB, bool forward,
                             QVector<QPointF>& out) const;

    QRectF polygonBBox(const QVector<QPointF>& poly) const;

    // Fence crossing checks
    bool isStraightHopAllowed(const QPointF& a, const QPointF& b) const;
    bool segmentCrossesInteriorAnyPoly(const QPointF& a, const QPointF& b) const;

    // --------- Ordering & DP ---------
    double hopCostEndpoint(int si, int entrySideFrom, int sj, int entrySideTo) const;

    QVector<int> initialOrderGreedy();
    QVector<int> initialOrderSweep();

    // Ring DP over fixed cyclic order. Returns total cost or INF on infeasible.
    double orientationRingDP(const QVector<int>& order,
                             QVector<int>* entrySideOut,
                             QVector<int>* parentSideOut,
                             int* bestStartSideOut) const;

    bool improveOrder2Opt(QVector<int>& order, int roundsLimit);

    // Route reconstruction
    QList<QPointF> buildRoute(const QVector<int>& order,
                              const QVector<int>& entrySide,
                              int startSide) const;

    // --------- Small helpers ---------
    static QRectF rectFromTwoPoints(const QPointF& a, const QPointF& b);
    static double lerp(double a, double b, double t);

    // Endpoint indexing (2 endpoints per segment)
    static int epIndex(int segIdx, int side) { return segIdx * 2 + (side & 1); }

    // Access into dense endpoint cost matrix
    double epCostAt(int i, int j) const { return epCost[i * endpoints.size() + j]; }
    void   setEpCostAt(int i, int j, double v) { epCost[i * endpoints.size() + j] = v; }

private:
    // --------- Inputs ---------
    QList<QLineF> segs;      // segments to visit
    QList<QLineF> fencesIn;  // fence edges (intended to stitch into polygons)

    // --------- Derived / working data ---------
    // Segment L/R labeling relative to axisU
    QVector<QPointF> segA, segB;  // left (A) and right (B) endpoints
    QVector<double>  segLen;

    // Fence polygons
    QVector<FencePolygon> polys;

    // Endpoint list (2 per segment)
    QVector<EndpointInfo> endpoints;

    // Endpoint-to-endpoint hop cost matrix (size MxM, flattened row-major)
    QVector<double> epCost;

    // Axis for projecting/ordering
    QPointF axisU {1, 0};
    QPointF axisV {0, 1};

    // --------- Parameters / state ---------
    double  epsilon = 1e-6;
    int     maxImproveRounds = 200;

    bool    hasSeed = false;
    quint32 rngSeed = 0;

    bool    lastFeasible = false;
    QString errorMsg;
};

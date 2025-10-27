#pragma once

#include <QList>
#include <QVector>
#include <QPointF>
#include <QPolygonF>
#include <QLineF>
#include <QRectF>
#include <QtGlobal>
#include <limits>
#include <cmath>
#include <algorithm>

class ChinesePostmanParallel {
public:
    // ---- public API (unchanged) ----
    ChinesePostmanParallel(const QList<QLineF>& segments,
                           const QList<QPolygonF>& fencePolys);

    QList<QPointF> solve();

    QString error() const { return errorMsg; }
    bool feasible() const { return lastFeasible; }

public: // tuning knobs
    int deterministicPasses = 2;      // fewer passes for speed
    double epsilon = 1e-9;            // geometric epsilon
    int twoOptBand = 64;              // band limit for 2-opt (speed)

private:
    // --------- data ----------
    QList<QLineF> segs;
    QVector<QPolygonF> polys;

    // axis
    QPointF axisU{1,0}, axisV{0,1};

    // segment meta
    QVector<QPointF> segA, segB;  // left-to-right endpoints
    QVector<double> segLen;

    struct Endpoint {
        QPointF p;
        int seg;     // segment id
        int side;    // 0 = A, 1 = B
        int dummy1;
        double dummy2;
    };
    QVector<Endpoint> endpoints;

    // costs (flat m*m)
    QVector<double> epCost;

    // status
    QString errorMsg;
    bool lastFeasible = false;

private:
    // -------- small math helpers --------
    static QPointF makeUnit(const QPointF& v);
    static double  dot(const QPointF& a, const QPointF& b);
    static QPointF sub(const QPointF& a, const QPointF& b);
    static double  norm(const QPointF& a);
    static double  dist(const QPointF& a, const QPointF& b);
    static bool    finitePoint(const QPointF& p);

    // -------- pipeline --------
    bool prepare();
    void classifySegmentsLR();
    void buildEndpointCostMatrix();

    // feasibility
    bool isStraightHopAllowed(const QPointF& a, const QPointF& b) const;

    // ordering & DP
    QVector<int> initialOrderGreedy();
    QVector<int> initialOrderSweep();

    double orientationRingDP(const QVector<int>& order,
                             QVector<int>* entrySideOut,
                             QVector<int>* parentSideOut,
                             int* bestStartSideOut) const;

    bool improveOrder2Opt(QVector<int>& order, int maxPasses);

    // helpers
    inline qsizetype M() const { return endpoints.size(); }
    int epIndex(int s, int side) const;
    double epCostAt(int ei, int ej) const;
    void setEpCostAt(int ei, int ej, double v);
    double hopCostEndpoint(int si, int entrySideFrom, int sj, int entrySideTo) const;

    // route build
    QList<QPointF> buildRoute(const QVector<int>& order,
                              const QVector<int>& entrySide, int startSide) const;

    // -------- fast geometry & acceleration --------
    struct Edge {
        double x1, y1, x2, y2;
        QRectF bbox;
        int polyId;
    };
    QVector<Edge> edges;           // all polygon edges
    QRectF worldBB;                // bbox of all polygons

    // uniform grid
    struct Grid {
        int nx = 0, ny = 0;
        double x0 = 0, y0 = 0, dx = 1, dy = 1;
        QVector<QVector<int>> cells; // indices into edges
        bool valid() const { return nx > 0 && ny > 0 && !cells.isEmpty(); }
    } grid;

    void preprocessPolygons();
    void buildGrid(int targetCells = 16384); // ~128x128 default
    void edgesInAABB(const QRectF& bb, QVector<int>& out) const;

    // geometric tests
    static bool bboxIntersect(const QRectF& a, const QRectF& b);
    static bool segSegIntersect(double x1,double y1,double x2,double y2,
                                double x3,double y3,double x4,double y4);
    bool segmentCrossesInteriorAnyPolyFast(const QPointF& a, const QPointF& b) const;
    bool pointInPolyWindingFast(const QPointF& p, int polyId) const;

    // cheap pruning
    double distanceGate = std::numeric_limits<double>::infinity(); // set from median seg length
    void computeDistanceGate();
};

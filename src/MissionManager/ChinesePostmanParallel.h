#pragma once

#include <QList>
#include <QVector>
#include <QLineF>
#include <QPolygonF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QtGlobal>

// A lightweight solver that orders and orients line segments into a closed tour,
// allowing straight hops between endpoints only if the hop doesn't cross any fence polygon.
// No "boundary riding" – just straight-line feasibility checks.
class ChinesePostmanParallel {
public:
    ChinesePostmanParallel(const QList<QLineF>& segments,
                           const QList<QPolygonF>& fencePolys);

    // Solve the closed tour. On success, returns the polyline path (including hops).
    // Call error() if it returns empty to distinguish "empty input" vs "infeasible".
    QList<QPointF> solve();

    // Status
    bool feasible() const { return lastFeasible; }
    QString error() const { return errorMsg; }

    // Tuning
    void setEpsilon(double e) { epsilon = e; }
    void setDeterministic2OptPasses(int passes) { deterministicPasses = qMax(0, passes); }

private:
    struct EndpointInfo {
        QPointF p{};
        int segIdx{-1};
        int side{0}; // 0: left(A), 1: right(B)
        // legacy/unused fields kept for compatibility
        int    polyIdx{-1};
        double sPerim{0.0};
    };

    // ---- Data
    QList<QLineF>       segs;
    QVector<QPolygonF>  polys;

    QVector<double>     segLen;
    QVector<QPointF>    segA, segB;
    QVector<EndpointInfo> endpoints; // size = 2*N
    QVector<double>     epCost;      // size = (2N)*(2N), flat row-major

    QPointF axisU{1,0}, axisV{0,1};

    // State & params
    bool    lastFeasible{false};
    QString errorMsg;
    double  epsilon{1e-9};
    int     deterministicPasses{2};

private:
    // ---- Pipeline pieces
    bool prepare();
    void classifySegmentsLR();
    void buildEndpointCostMatrix();

    bool isStraightHopAllowed(const QPointF& a, const QPointF& b) const;
    bool segmentCrossesInteriorAnyPoly(const QPointF& a, const QPointF& b) const;

    QVector<int> initialOrderGreedy();
    QVector<int> initialOrderSweep();

    double orientationRingDP(const QVector<int>& order,
                             QVector<int>* entrySideOut,
                             QVector<int>* parentSideOut,
                             int* bestStartSideOut) const;

    bool improveOrder2Opt(QVector<int>& order, int maxPasses);

    QList<QPointF> buildRoute(const QVector<int>& order,
                              const QVector<int>& entrySide, int startSide) const;

    // ---- Helpers
    qsizetype M() const { return endpoints.size(); }
    int    epIndex(int s, int side) const;
    double epCostAt(int ei, int ej) const;
    void   setEpCostAt(int ei, int ej, double v);
    double hopCostEndpoint(int si, int entrySideFrom, int sj, int entrySideTo) const;

    // Small math helpers (implemented in .cpp)
    static QPointF makeUnit(const QPointF& v);
    static double  dot(const QPointF& a, const QPointF& b);
    static QPointF sub(const QPointF& a, const QPointF& b);
    static double  norm(const QPointF& a);
    static double  dist(const QPointF& a, const QPointF& b);
    static bool    finitePoint(const QPointF& p);
};


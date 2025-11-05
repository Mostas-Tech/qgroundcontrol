#include "AgriculturalStyleComplexItem.h"
#include <Decomposition.h>
#include <QSettings>
#include <QtCore/QJsonArray>
#include <QtMath>
#include <algorithm>
#include <limits>
#include "GeoFenceController.h"
#include "JsonHelper.h"
#include "MissionController.h"
#include "PlanMasterController.h"
#include "QGC.h"
#include "QGCApplication.h"
#include "QGCFenceCircle.h"
#include "QGCFencePolygon.h"
#include "QGCGeo.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"
#include "tsp_route.h"
#include "ChinesePostmanParallel.h"

QGC_LOGGING_CATEGORY(AgriculturalStyleComplexItemLog, "AgriStyleComplexItemLog");

QPointF geoToNedXY(const QGeoCoordinate& geo, const QGeoCoordinate& ref) {
    double x = 0, y = 0, z = 0;
    // QGC uses (north, east, down). Map to (x=east, y=north)
    QGCGeo::convertGeoToNed(geo, ref, y, x, z); // <-- NO & on args
    return QPointF(x, y);
}

QGeoCoordinate nedXYToGeo(const QPointF& nedXY, const QGeoCoordinate& ref) {
    QGeoCoordinate out;
    QGCGeo::convertNedToGeo(nedXY.y(), nedXY.x(), 50, ref, out); // <-- out by ref
    return out;
}

QList<QLineF> clipLinesWithPolygon(const QList<QLineF>& lines, const QPolygonF& poly) {
    QList<QLineF> out;
    if (poly.size() < 3) return out;

    const qreal eps = 1e-9;

    // local lambdas (still one function)
    auto nearlyEqual = [&](qreal a, qreal b){ return qAbs(a - b) <= eps; };
    auto paramAlong = [&](const QPointF& A, const QPointF& B, const QPointF& P){
        const qreal dx = B.x() - A.x(), dy = B.y() - A.y();
        const qreal len2 = dx*dx + dy*dy;
        if (len2 <= eps) return 0.0;
        return ((P.x() - A.x())*dx + (P.y() - A.y())*dy) / len2;
    };
    auto pushUnique = [&](QVector<qreal>& v, qreal t){
        for (qreal u : v) if (nearlyEqual(u, t)) return;
        v.push_back(t);
    };

    const int n = poly.size();
    for (const QLineF& seg : lines) {
        if (seg.length() <= eps) continue;

        // Collect parametric cut points along seg
        QVector<qreal> cuts;
        cuts.reserve(8);
        cuts << 0.0 << 1.0;

        for (int i = 0; i < n; ++i) {
            const QPointF& a = poly[i];
            const QPointF& b = poly[(i + 1) % n];
            QPointF ip;
            QLineF edge(a, b);
            auto t = seg.intersects(edge, &ip);
            if (t == QLineF::BoundedIntersection) {
                qreal s = paramAlong(seg.p1(), seg.p2(), ip);
                // clamp tiny overshoots
                if (s < 0.0 && s > -eps) s = 0.0;
                if (s > 1.0 && s < 1.0 + eps) s = 1.0;
                if (s >= -eps && s <= 1.0 + eps) {
                    pushUnique(cuts, qBound(0.0, s, 1.0));
                }
            }
        }

        std::sort(cuts.begin(), cuts.end());
        // unique near-equal neighbors
        QVector<qreal> uniq;
        uniq.reserve(cuts.size());
        for (qreal t : cuts) {
            if (uniq.isEmpty() || !nearlyEqual(uniq.back(), t)) uniq.push_back(t);
        }

        // Build subsegments; keep those whose midpoint is inside polygon
        for (int i = 0; i + 1 < uniq.size(); ++i) {
            const qreal ta = uniq[i], tb = uniq[i + 1];
            if (tb - ta <= eps) continue;
            const QPointF A = seg.pointAt(ta);
            const QPointF B = seg.pointAt(tb);
            const QPointF M((A.x() + B.x()) * 0.5, (A.y() + B.y()) * 0.5);
            if (poly.containsPoint(M, Qt::OddEvenFill)) {
                out.append(QLineF(A, B));
            }
        }
    }
    return out;
}

static double polygonAreaAbs(const QPolygonF& poly) {
    if (poly.size() < 3) return 0.0;
    double a = 0.0;
    for (int i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        a += (poly[j].x() * poly[i].y()) - (poly[i].x() * poly[j].y());
    }
    return qAbs(a) * 0.5;
}

auto toGeoSafe = [&](const QPointF& p,const QGeoCoordinate& ref) {
    if (!qIsFinite(p.x()) || !qIsFinite(p.y())) {
        qWarning() << "Non-finite NED point to convert:" << p;
        return QGeoCoordinate(); // invalid
    }
    return nedXYToGeo(p, ref);
};

QPolygonF nedSurveyArea_offsett(const QPolygonF& nedSurveyArea, double offset) {
    if (nedSurveyArea.size() < 3 || qFuzzyIsNull(offset)) {
        return nedSurveyArea;
    }

    // Build the base path
    QPainterPath path;
    path.moveTo(nedSurveyArea.first());
    for (int i = 1; i < nedSurveyArea.size(); ++i) path.lineTo(nedSurveyArea[i]);
    path.closeSubpath();

    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * qAbs(offset));
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);

    QPainterPath stroke = stroker.createStroke(path);
    QPainterPath result;

    if (offset > 0) {
        // Shrink (inset)
        result = path.subtracted(stroke).simplified();
    } else {
        // Expand (outset)
        result = path.united(stroke).simplified();
    }

    // Pick the largest region (in case multiple polygons appear)
    const auto polys = result.toFillPolygons();
    if (polys.isEmpty()) return QPolygonF();

    double bestArea = -1.0;
    QPolygonF best;
    for (const QPolygonF& p : polys) {
        double a = polygonAreaAbs(p);
        if (a > bestArea) {
            bestArea = a;
            best = p;
        }
    }

    if (!best.isEmpty() && best.first() != best.last()) best << best.first();
    return best;
}

static QList<QLineF> generateTransectsNED(const QRectF& bboxNED, double spacing_m, double angleDeg, int startCorner) {
    QList<QLineF> transects;

    // Sanity check
    if (spacing_m <= 0.0) return transects;
    if (startCorner < 0 || startCorner > 3) startCorner = 0;

    // Define bbox corners
    QVector<QPointF> corners = {
        bboxNED.topLeft(),    // 0
        bboxNED.topRight(),   // 1
        bboxNED.bottomRight(),// 2
        bboxNED.bottomLeft()  // 3
    };

    QPointF startpoint = corners[startCorner];

    // Compute direction and normal vectors
    double angleRad = qDegreesToRadians(angleDeg);
    QPointF dir(qCos(angleRad), qSin(angleRad));      // direction along transect
    QPointF normal(-dir.y(), dir.x());                // perpendicular direction (for spacing)

    // Determine offset range (projections of bbox corners along normal)
    double minOffset = std::numeric_limits<double>::max();
    double maxOffset = -std::numeric_limits<double>::max();
    for (const QPointF& c : corners) {
        double proj = QPointF::dotProduct(c - startpoint, normal);
        minOffset = qMin(minOffset, proj);
        maxOffset = qMax(maxOffset, proj);
    }

    // Generate transects: spacing/2 + line + spacing + line + ... + spacing/2
    for (double offset = minOffset + spacing_m / 2.0; offset <= maxOffset - spacing_m / 2.0; offset += spacing_m) {
        QPointF offsetPoint = startpoint + normal * offset;

        // Long line along direction
        QLineF line(offsetPoint - dir * 1e6, offsetPoint + dir * 1e6);

        // Clip line to bbox (intersection with edges)
        QList<QPointF> intersections;
        QVector<QLineF> edges = {
            QLineF(bboxNED.topLeft(), bboxNED.topRight()),
            QLineF(bboxNED.topRight(), bboxNED.bottomRight()),
            QLineF(bboxNED.bottomRight(), bboxNED.bottomLeft()),
            QLineF(bboxNED.bottomLeft(), bboxNED.topLeft())
        };
        for (const QLineF& edge : edges) {
            QPointF intersect;
            if (line.intersects(edge, &intersect) == QLineF::BoundedIntersection)
                intersections.append(intersect);
        }

        if (intersections.size() >= 2)
            transects.append(QLineF(intersections[0], intersections[1]));
    }
    return transects;
}

static inline QPointF lerp(const QPointF& a, const QPointF& b, double t) {
    return QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t);
}

static inline double paramAlong(const QLineF& line, const QPointF& p) {
    // Project p onto the line segment parameter t in [0,1]
    const QPointF a = line.p1();
    const QPointF b = line.p2();
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double len2 = dx*dx + dy*dy;
    if (len2 == 0.0) return 0.0;
    return ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2;
}

static inline bool nearlyEqual(double x, double y, double eps = 1e-9) {
    return qAbs(x - y) <= eps;
}

static void pushUnique(QVector<double>& v, double t, double eps = 1e-9) {
    for (double u : v) {
        if (nearlyEqual(u, t, eps)) return;
    }
    v.push_back(t);
}

QList<QLineF> subtractFence(const QList<QLineF>& lines, const QPolygonF& fence) {
    QList<QLineF> result;
    if (fence.size() < 3) {
        // Nothing to subtract if fence isn’t a valid polygon
        return lines;
    }

    // Precompute polygon edges
    const int n = fence.size();
    QVector<QLineF> edges;
    edges.reserve(n);
    for (int i = 0; i < n; ++i) {
        edges.push_back(QLineF(fence[i], fence[(i + 1) % n]));
    }

    constexpr double EPS = 1e-9;

    for (const QLineF& seg : lines) {
        // Collect cut parameters along the segment: always include [0,1] ends
        QVector<double> ts;
        ts.reserve(edges.size() + 2);
        ts.push_back(0.0);
        ts.push_back(1.0);

        // Find intersections with polygon edges
        for (const QLineF& e : edges) {
            QPointF ip;
            QLineF::IntersectionType it = seg.intersects(e, &ip);
            if (it == QLineF::BoundedIntersection) {
                // Intersection point lies on both segments
                double t = paramAlong(seg, ip);
                // Keep only if within [0,1]
                if (t > -EPS && t < 1.0 + EPS) {
                    // Clamp to [0,1] to avoid tiny numeric bleed
                    t = std::clamp(t, 0.0, 1.0);
                    pushUnique(ts, t, 1e-8);
                }
            } else if (it == QLineF::UnboundedIntersection) {
                // Collinear or infinite-line intersection.
                // If collinear and overlapping, we conservatively insert the
                // projections of the edge endpoints that fall on the segment.
                // This splits the segment so midpoint tests can discard inside parts.
                if (qFuzzyIsNull(seg.angleTo(e)) || qFuzzyIsNull(e.angleTo(seg))) {
                    double t1 = paramAlong(seg, e.p1());
                    double t2 = paramAlong(seg, e.p2());
                    // Only insert t’s that are near the segment bounds
                    if (t1 > -EPS && t1 < 1.0 + EPS) pushUnique(ts, std::clamp(t1, 0.0, 1.0), 1e-8);
                    if (t2 > -EPS && t2 < 1.0 + EPS) pushUnique(ts, std::clamp(t2, 0.0, 1.0), 1e-8);
                }
            }
        }

        // Sort and de-dup
        std::sort(ts.begin(), ts.end(), [](double a, double b){ return a < b; });

        // Rebuild with strict uniqueness after sort to be safe
        QVector<double> cuts;
        cuts.reserve(ts.size());
        for (double t : ts) {
            if (cuts.isEmpty() || !nearlyEqual(cuts.back(), t, 1e-8))
                cuts.push_back(t);
        }

        // Build candidate sub-segments between consecutive t’s
        for (int i = 0; i + 1 < cuts.size(); ++i) {
            double t0 = cuts[i];
            double t1 = cuts[i + 1];
            // Ignore degenerate intervals
            if (t1 - t0 <= 1e-9) continue;

            // Midpoint of this piece
            double tm = (t0 + t1) * 0.5;
            QPointF mid = lerp(seg.p1(), seg.p2(), tm);

            // Keep only if midpoint is OUTSIDE the polygon
            // Use OddEven fill; treat boundary as "outside" by checking a tiny offset if needed
            Qt::FillRule rule = Qt::OddEvenFill;
            bool inside = fence.containsPoint(mid, rule);
            if (!inside) {
                QPointF a = lerp(seg.p1(), seg.p2(), t0);
                QPointF b = lerp(seg.p1(), seg.p2(), t1);
                // Avoid creating vanishingly small segments
                if (QLineF(a, b).length() > 1e-9)
                    result.push_back(QLineF(a, b));
            }
        }
    }
    return result;
}

// Distance from point P to segment AB
static double pointToSegmentDist(const QPointF& A, const QPointF& B, const QPointF& P) {
    const double vx = B.x() - A.x(), vy = B.y() - A.y();
    const double wx = P.x() - A.x(), wy = P.y() - A.y();
    const double vv = vx*vx + vy*vy;
    double t = vv > 0.0 ? (wx*vx + wy*vy) / vv : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double dx = (A.x() + t*vx) - P.x();
    const double dy = (A.y() + t*vy) - P.y();
    return std::hypot(dx, dy);
}

// Inclusive "inside": true if inside by fill rule OR lying on an edge (<= eps)
static bool pointInsidePolygonInclusive(const QPolygonF& poly, const QPointF& p, const double edgeEps) {
    if (poly.containsPoint(p, Qt::OddEvenFill)) return true;

    // Boundary check
    const int n = poly.size();
    for (int i = 0; i < n; ++i) {
        const QPointF A = poly[i];
        const QPointF B = poly[(i + 1) % n];
        if (pointToSegmentDist(A, B, p) <= edgeEps) return true;
    }
    return false;
}

// Clip segments to a (hole-free) polygon, keeping boundary-overlapping parts.
// - linesIn: input segments (unchanged)
// - poly: clipping polygon (NED meters; may be concave; no holes)
// Returns: list of clipped segments fully inside or on the polygon.
static QList<QLineF> clipLinesToPolygonInclusive(const QList<QLineF>& linesIn, const QPolygonF& poly) {
    QList<QLineF> out;
    if (poly.size() < 3 || linesIn.isEmpty()) return out;

    const int n = poly.size();
    QVector<QLineF> edges;
    edges.reserve(n);
    for (int i = 0; i < n; ++i) edges.push_back(QLineF(poly[i], poly[(i + 1) % n]));

    // Tolerances
    const double EPS_T = 1e-9;   // parameter epsilon on [0,1]
    const double EPS_LEN = 1e-8; // drop degenerate pieces
    const double EPS_PAR = 1e-12;// parallelism test (slope/cross)
    const double EDGE_EPS = 1e-6;// boundary inclusion tolerance (meters)

    auto dot = [](const QPointF& a, const QPointF& b){ return a.x()*b.x() + a.y()*b.y(); };
    auto cross = [](const QPointF& a, const QPointF& b){ return a.x()*b.y() - a.y()*b.x(); };

    for (const QLineF& L : linesIn) {
        const QPointF A = L.p1();
        const QPointF B = L.p2();
        const QPointF dL(B.x() - A.x(), B.y() - A.y());

        // Collect all t in [0,1] where L meets polygon boundary OR endpoints inside.
        QVector<double> ts;
        ts.reserve(8);
        // Always consider endpoints; we’ll filter intervals by midpoint inclusion.
        ts.push_back(0.0);
        ts.push_back(1.0);

        for (const QLineF& E : edges) {
            const QPointF C = E.p1();
            const QPointF D = E.p2();
            const QPointF dE(D.x() - C.x(), D.y() - C.y());

            // Check for parallel / colinear overlap first
            const double cr = cross(dL, dE);
            if (std::fabs(cr) <= EPS_PAR) {
                // Parallel: check if colinear (C is on L)
                if (pointToSegmentDist(A, B, C) <= EDGE_EPS || pointToSegmentDist(A, B, D) <= EDGE_EPS) {
                    // Project edge endpoints onto L to get t's
                    const double ll = dot(dL, dL);
                    if (ll > 0.0) {
                        auto projT = [&](const QPointF& P){
                            return std::clamp(dot(QPointF(P.x() - A.x(), P.y() - A.y()), dL) / ll, 0.0, 1.0);
                        };
                        double tC = projT(C);
                        double tD = projT(D);
                        if (tD < tC) std::swap(tC, tD);
                        ts.push_back(tC);
                        ts.push_back(tD);
                    }
                }
                // If parallel but not colinear -> no intersection
                continue;
            }

            // Non-parallel: compute intersection
            QPointF ip;
            if (QLineF(A, B).intersects(QLineF(C, D), &ip) == QLineF::BoundedIntersection) {
                // Recover parameter t on L for ip
                double t;
                if (std::fabs(dL.x()) >= std::fabs(dL.y()))
                    t = (std::fabs(dL.x()) > 0.0) ? (ip.x() - A.x()) / dL.x() : 0.0;
                else
                    t = (std::fabs(dL.y()) > 0.0) ? (ip.y() - A.y()) / dL.y() : 0.0;

                if (t >= -EPS_T && t <= 1.0 + EPS_T)
                    ts.push_back(std::clamp(t, 0.0, 1.0));
            }
        }

        if (ts.size() < 2) continue;

        // Sort & deduplicate t's (avoid double hits at vertices/corners)
        std::sort(ts.begin(), ts.end());
        QVector<double> uniq;
        uniq.reserve(ts.size());
        for (double t : ts) {
            if (uniq.isEmpty() || std::fabs(t - uniq.back()) > 1e-7)
                uniq.push_back(t);
        }

        // Build interior pieces: for each consecutive [t_i, t_{i+1}], test midpoint.
        for (int i = 0; i + 1 < uniq.size(); ++i) {
            const double t0 = uniq[i];
            const double t1 = uniq[i + 1];
            if (t1 - t0 <= EPS_T) continue;

            const QPointF P0 = L.pointAt(t0);
            const QPointF P1 = L.pointAt(t1);
            const QPointF Pm = L.pointAt(0.5 * (t0 + t1));

            // Inclusive test: inside OR on boundary -> keep
            if (!pointInsidePolygonInclusive(poly, Pm, EDGE_EPS)) continue;
            if (QLineF(P0, P1).length() > EPS_LEN)
                out.push_back(QLineF(P0, P1));
        }
    }

    return out;
}

static QPolygonF _toPolyF(const QGCMapPolygon& poly) {
    QPolygonF p;
    const QVariantList& pts = poly.path();
    p.reserve(pts.size());
    for (const QVariant& v : pts) {
        const QGeoCoordinate c = v.value<QGeoCoordinate>();
        p << QPointF(c.longitude(), c.latitude()); // NOTE: visual math only; mission uses QGeoCoordinate
    }
    return p;
}

AgriculturalStyleComplexItem::AgriculturalStyleComplexItem(PlanMasterController* masterController, bool flyView)
    : ComplexMissionItem(masterController, flyView),
      _metaDataMap(FactMetaData::createMapFromJsonFile(QStringLiteral(":/json/Agriculture.test.json"), this)),
      _lineSpacingFact(QString("Agricultural") /*componentId*/, _metaDataMap[lineSpacingName], this),
      _gridAngleFact(QString("Agricultural"), _metaDataMap[gridAngleName], this),
      _entryLocationFact(QString("Agricultural"), _metaDataMap[entryLocationName], this),
      _speedModeFact(QString("Agricultural"), _metaDataMap[speedModeName], this),
      _fixedSpeedFact(QString("Agricultural"), _metaDataMap[fixedSpeedName], this),
      _turnAroundDistanceFact(QString("Agricultural"), _metaDataMap[turnAroundDistanceName], this),
      _terrainAdjustToleranceFact(QString("Agricultural"), _metaDataMap[terrainAdjustToleranceName], this),
      _terrainAdjustMaxClimbRateFact(QString("Agricultural"), _metaDataMap[terrainAdjustMaxClimbRateName], this),
      _terrainAdjustMaxDescentRateFact(QString("Agricultural"), _metaDataMap[terrainAdjustMaxDescentRateName], this)
{
    GeoFenceController* gfc = _masterController->geoFenceController();
    _geoFenceCircles = gfc ? gfc->circles() : nullptr;
    _geoFencePolygons = gfc ? gfc->polygons() : nullptr;

    // Polygon change triggers recalc
    connect(&_surveyAreaPolygon, &QGCMapPolygon::pathChanged, this, &AgriculturalStyleComplexItem::_polyChanged);
    connect(&_surveyAreaPolygon, &QGCMapPolygon::isValidChanged, this, &AgriculturalStyleComplexItem::readyForSaveStateChanged);

    // Fact changes that affect geometry
    connect(&_lineSpacingFact, &Fact::valueChanged, this, &AgriculturalStyleComplexItem::_rebuildTransects);
    connect(&_gridAngleFact, &Fact::valueChanged, this, &AgriculturalStyleComplexItem::_rebuildTransects);
    connect(&_entryLocationFact, &Fact::valueChanged, this, &AgriculturalStyleComplexItem::_rebuildTransects);
    connect(&_turnAroundDistanceFact, &Fact::valueChanged, this, &AgriculturalStyleComplexItem::_rebuildTransects);
    connect(&_surveyAreaPolygon, &QGCMapPolygon::pathChanged, this, &AgriculturalStyleComplexItem::_rebuildTransects);
    connect(&_surveyAreaPolygon, &QGCMapPolygon::pathChanged, this, &AgriculturalStyleComplexItem::_polyChanged);

    setDirty(false);
}

QLineF AgriculturalStyleComplexItem::_generateMidLine(const QGeoCoordinate& center, double angleDeg) const {
    // Create a long center line passing through 'center' at 'angleDeg' (in degrees).
    // Works in lon/lat space (QPointF: x=lon, y=lat). This is a simple approximation and
    // should be replaced with a proper geo-projection for high accuracy.
    const double rad = qDegreesToRadians(angleDeg);
    const double cs = qCos(rad);
    const double sn = qSin(rad);

    // extent in degrees (~0.1 deg ≈ 11 km in latitude). Tunable as needed.
    const double extentDeg = 0.1;

    QPointF dir(cs, sn);
    QPointF p1(center.longitude() - dir.x() * extentDeg, center.latitude() - dir.y() * extentDeg);
    QPointF p2(center.longitude() + dir.x() * extentDeg, center.latitude() + dir.y() * extentDeg);
    return QLineF(p1, p2);
}

void AgriculturalStyleComplexItem::setSequenceNumber(int sequenceNumber) {
    if (_sequenceNumber != sequenceNumber) {
        _sequenceNumber = sequenceNumber;
        emit sequenceNumberChanged(sequenceNumber);
        emit lastSequenceNumberChanged(lastSequenceNumber());
    }
}

void AgriculturalStyleComplexItem::setDirty(bool dirty) {
    if (!dirty) {
        _surveyAreaPolygon.setDirty(false);
    }
    if (_dirty != dirty) {
        _dirty = dirty;
        emit dirtyChanged(_dirty);
    }
}

// Build a simple WP at coordinate with altitude from vehicle default (no terrain mode yet)
void AgriculturalStyleComplexItem::_appendWaypoint(QList<MissionItem*>& items, QObject* missionItemParent, int& seqNum, MAV_FRAME mavFrame, float holdTime, const QGeoCoordinate& coordinate) {
    double altitude = 5.0;
    MissionItem* item = new MissionItem(seqNum++,
                                        MAV_CMD_NAV_WAYPOINT,
                                        mavFrame,
                                        holdTime,
                                        0.0, // acceptance radius
                                        0.0, // pass through
                                        std::numeric_limits<double>::quiet_NaN(), // yaw
                                        coordinate.latitude(),
                                        coordinate.longitude(),
                                        altitude,
                                        true,  // autoContinue
                                        false, // isCurrentItem
                                        missionItemParent);
    items.append(item);
}

// Generate parallel centerlines (world space in lon/lat for now); caller clips
QList<QLineF> AgriculturalStyleComplexItem::_generateParallelLines(const QPolygonF& area, double spacingMeters, double angleDeg) const {
    QList<QLineF> lines;
    if (area.size() < 3 || spacingMeters <= 0) {
        return lines;
    }

    // Very lightweight approach: compute area bbox in local frame rotated by angle
    // NOTE: For production we should use proper meters projection. This is a first pass.
    const double rad = qDegreesToRadians(angleDeg);
    const double cs = qCos(rad), sn = qSin(rad);
    auto rot = [&](const QPointF& p) { return QPointF(cs * p.x() - sn * p.y(), sn * p.x() + cs * p.y()); };
    auto irot = [&](const QPointF& p) { return QPointF(cs * p.x() + sn * p.y(), -sn * p.x() + cs * p.y()); };

    QPolygonF r;
    r.reserve(area.size());
    for (const QPointF& p : area) r << rot(p);
    QRectF rb = r.boundingRect();

    // Create parallel lines across bbox, step by spacing
    // We fake meters by treating lon/lat as planar small deltas (OK for small fields). TODO: replace with Geo
    // projection.
    const double step = spacingMeters * 1.0e-5; // rough scale placeholder; to be replaced with proper geo scale
    for (double y = rb.top(); y <= rb.bottom(); y += step) {
        QPointF a = irot(QPointF(rb.left(), y));
        QPointF b = irot(QPointF(rb.right(), y));
        lines << QLineF(a, b);
    }

    return lines;
}

// Clip line list to polygon -> produce legs as sequences of entry->exit
// CoordInfo_t (no zigzag ordering here)
QList<QList<AgriculturalStyleComplexItem::CoordInfo_t>> AgriculturalStyleComplexItem::_clipLinesToPolygon(
    const QList<QLineF>& lines, const QPolygonF& area) const
{
    QList<QList<CoordInfo_t>> legs;
    if (area.size() < 3) return legs;

    for (const QLineF& l : lines) {
        QList<QPointF> intersections;
        for (int i = 0; i < area.size(); ++i) {
            QLineF edge(area[i], area[(i + 1) % area.size()]);
            QPointF ip;
            if (l.intersects(edge, &ip) == QLineF::BoundedIntersection) {
                intersections << ip;
            }
        }

        if (intersections.size() < 2) continue;

        std::sort(intersections.begin(), intersections.end(), [&](const QPointF& a, const QPointF& b) {
            return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
        });

        // pair them
        for (int i = 0; i + 1 < intersections.size(); i += 2) {
            QGeoCoordinate entry(intersections[i].y(), intersections[i].x());
            QGeoCoordinate exit(intersections[i + 1].y(), intersections[i + 1].x());

            // --- Fence rejection (exclusion polygons only) ---
            // Convert candidate leg to a QLineF in lon/lat space
            QLineF legLine(QPointF(entry.longitude(), entry.latitude()),
                           QPointF(exit.longitude(), exit.latitude()));

            auto segmentBlockedByExclusion = [&](const QList<QGCFencePolygon*>& fences) -> bool {
                for (const auto* fence : fences) {
                    if (!fence) continue;
                    if (fence->inclusion()) {
                        // We only avoid *exclusion* zones
                        continue;
                    }
                    // Build polygon in lon/lat
                    QPolygonF fencePoly;
                    const QVariantList& path = fence->path();
                    fencePoly.reserve(path.size());
                    for (const QVariant& v : path) {
                        const QGeoCoordinate c = v.value<QGeoCoordinate>();
                        fencePoly << QPointF(c.longitude(), c.latitude());
                    }
                    // Quick contain test (midpoint or endpoints inside)
                    const QPointF mid((legLine.p1().x() + legLine.p2().x()) * 0.5,
                                      (legLine.p1().y() + legLine.p2().y()) * 0.5);
                    if (fencePoly.containsPoint(mid, Qt::OddEvenFill) ||
                        fencePoly.containsPoint(legLine.p1(), Qt::OddEvenFill) ||
                        fencePoly.containsPoint(legLine.p2(), Qt::OddEvenFill)) {
                        return true; // blocked
                    }
                    // Edge intersection test
                    for (int k = 0; k < fencePoly.size(); ++k) {
                        QLineF e(fencePoly[k], fencePoly[(k + 1) % fencePoly.size()]);
                        QPointF ip;
                        if (legLine.intersects(e, &ip) == QLineF::BoundedIntersection) {
                            return true; // blocked
                        }
                    }
                }
                return false;
            };

            // OK to keep: create the usual CoordInfo_t pair
            QList<CoordInfo_t> seg;
            CoordInfo_t c1, c2;
            c1.coord = entry;
            c2.coord = exit;
            c1.coordType = CoordTypeSurveyEntry;
            c2.coordType = CoordTypeSurveyExit;
            seg << c1 << c2;
            legs << seg;
        }
    }

    return legs;
}

QList<QList<AgriculturalStyleComplexItem::CoordInfo_t>> AgriculturalStyleComplexItem::_orderLegsEntryFirst(
    const QList<QList<CoordInfo_t>>& legs) const
{
    QList<QList<CoordInfo_t>> out;
    if (legs.isEmpty()) return out;

    const double angleDeg = _gridAngleFact.rawValue().toDouble();
    const double rad = qDegreesToRadians(angleDeg);
    const double cs = qCos(rad), sn = qSin(rad);
    auto rot = [&](const QPointF& p) { return QPointF(cs * p.x() - sn * p.y(), sn * p.x() + cs * p.y()); };

    struct Wrap {
        QList<CoordInfo_t> seg;
        double yMid;
        double x0, x1;
    };

    QList<Wrap> tmp;
    tmp.reserve(legs.size());
    for (const auto& seg : legs) {
        if (seg.size() < 2) continue;
        const QPointF p0(seg.first().coord.longitude(), seg.first().coord.latitude());
        const QPointF p1(seg.last().coord.longitude(), seg.last().coord.latitude());
        const QPointF r0 = rot(p0);
        const QPointF r1 = rot(p1);
        tmp << Wrap{seg, 0.5 * (r0.y() + r1.y()), r0.x(), r1.x()};
    }
    if (tmp.isEmpty()) return out;

    const int entry = _entryLocationFact.rawValue().toInt();
    const bool startTop = (entry == EntryLocationTopLeft || entry == EntryLocationTopRight);
    const bool startLeft = (entry == EntryLocationTopLeft || entry == EntryLocationBottomLeft);

    // Top→bottom or bottom→top
    std::sort(tmp.begin(), tmp.end(), [&](const Wrap& a, const Wrap& b) {
        return startTop ? a.yMid > b.yMid : a.yMid < b.yMid;
    });

    bool wantLeftToRight = startLeft;
    for (auto& w : tmp) {
        auto seg = w.seg;
        const bool segIsLeftToRight = (w.x0 <= w.x1);
        if (segIsLeftToRight != wantLeftToRight) std::reverse(seg.begin(), seg.end());
        out << seg;
        wantLeftToRight = !wantLeftToRight;
    }

    return out;
}

void AgriculturalStyleComplexItem::updatetransect(void) {
    _rebuildTransects();
    setDirty(true);
}

void AgriculturalStyleComplexItem::rotateEntryPoint(void) {
    // Assuming you have:
    // enum EntryLocation { EntryLocationTopLeft = 0, EntryLocationTopRight,
    //                      // EntryLocationBottomRight, EntryLocationBottomLeft };
    // And: int _entryLocation; (or Fact _entryLocationFact)
    int entry = _entryLocationFact.rawValue().toInt(); // read current
    entry = (entry + 1) % 4; // rotate clockwise 0→1→2→3→0
    _entryLocationFact.setRawValue(entry); // update Fact

    // Rebuild transects with new entry point
    _rebuildTransects();

    // Mark as dirty so QGC knows to save it
    setDirty(true);
}

void AgriculturalStyleComplexItem::_recalcComplexDistance() {
    double dist = 0.0;
    for (const auto& leg : _transects) {
        for (int i = 1; i < leg.size(); ++i) {
            dist += leg[i - 1].coord.distanceTo(leg[i].coord);
        }
    }
    if (!qFuzzyCompare(dist, _complexDistance)) {
        _complexDistance = dist;
        emit complexDistanceChanged();
    }
}

QPolygonF AgriculturalStyleComplexItem::_surveyAreaToNed(const QGeoCoordinate& ref) const {
    QPolygonF ned;
    const QList<QGeoCoordinate> pts = _surveyAreaPolygon.coordinateList();
    ned.reserve(pts.size());
    for (const auto& c : pts) {
        ned << geoToNedXY(c, ref); // meters: x=East, y=North
    }
    return ned;
}

QPolygonF AgriculturalStyleComplexItem::fencePolygonToNed(const QGCFencePolygon* fence, const QGeoCoordinate& ref) {
    QPolygonF ned;
    if (!fence) return ned;
    const QVariantList& path = fence->path();
    ned.reserve(path.size());
    for (const QVariant& v : path) {
        const QGeoCoordinate c = v.value<QGeoCoordinate>();
        ned << geoToNedXY(c, ref); // meters
    }
    return ned;
}

void AgriculturalStyleComplexItem::_rebuildTransects() {
    if (_ignoreRecalc) return;

    _transects.clear();
    _visualTransectPoints.clear();
    _visualFieldTransectPairs.clear();

    if (!_surveyAreaPolygon.isValid() || _surveyAreaPolygon.count() < 3) {
        emit visualTransectPointsChanged();
        emit readyForSaveStateChanged();
        return;
    }

    QGeoCoordinate plannedHome = _masterController->missionController()->takeoffCoordinate();

    if (_geoFenceCircles->isEmpty() && _geoFencePolygons->isEmpty()) {
        // No fences -> simple transect generation (same basic approach as TransectStyleComplexItem)

        const QPolygonF area = _toPolyF(_surveyAreaPolygon);
        const double spacingMeters = _lineSpacingFact.rawValue().toDouble();
        const double angleDeg = _gridAngleFact.rawValue().toDouble();

        // 2) Generate parallel lines across bounding box
        QList<QLineF> lines = _generateParallelLines(area, spacingMeters, angleDeg);

        // 3) Clip lines to polygon to produce entry/exit legs
        QList<QList<CoordInfo_t>> legs = _clipLinesToPolygon(lines, area);

        // 4) Order legs according to entry location setting and create transects
        _transects = _orderLegsEntryFirst(legs);

        // --- apply turn-around distance extension only in the default (no-fence) branch ---
        const double turnMeters = _turnAroundDistanceFact.rawValue().toDouble();
        if (turnMeters > 0.0) {
            // Use same rough deg<->meter scale used by line generation
            const double degExt = turnMeters * 1.0e-5;
            for (auto& seg : _transects) {
                if (seg.size() < 2) continue;
                QGeoCoordinate e0 = seg.first().coord;
                QGeoCoordinate e1 = seg.last().coord;
                QPointF p0(e0.longitude(), e0.latitude());
                QPointF p1(e1.longitude(), e1.latitude());
                QPointF dir = p1 - p0;
                double len = qSqrt(dir.x() * dir.x() + dir.y() * dir.y());
                if (len <= 0.0) continue;
                QPointF ext(dir.x() / len * degExt, dir.y() / len * degExt);
                QGeoCoordinate newEntry(e0.latitude() - ext.y(), e0.longitude() - ext.x());
                QGeoCoordinate newExit(e1.latitude() + ext.y(), e1.longitude() + ext.x());
                seg.first().coord = newEntry;
                seg.last().coord = newExit;
            }
        }

        // 5) Build visual transect points and update entry/exit coords
        _visualTransectPoints.clear();
        for (const QList<CoordInfo_t>& transect : _transects) {
            for (const CoordInfo_t& ci : transect) {
                _visualTransectPoints.append(QVariant::fromValue(ci.coord));
            }
        }
        if (_visualTransectPoints.count()) {
            _coordinate = _visualTransectPoints.first().value<QGeoCoordinate>();
            _exitCoordinate = _visualTransectPoints.last().value<QGeoCoordinate>();
        } else {
            _coordinate = QGeoCoordinate();
            _exitCoordinate = QGeoCoordinate();
        }
        emit coordinateChanged(_coordinate);
        emit exitCoordinateChanged(_exitCoordinate);
        emit visualTransectPointsChanged();
        emit fieldPolygonsMapChanged();
        _recalcComplexDistance();
        emit specifiesCoordinateChanged();
        emit lastSequenceNumberChanged(lastSequenceNumber());
        emit readyForSaveStateChanged();
    } else {
        // Fences present -> obstacle-aware transect generation

        // Generate a Line with using angle and mid point of polygon
        _fieldPolygonsMap.clear();
        QGeoCoordinate midPoint = _surveyAreaPolygon.center();

        // Use midPoint and convert geo to local NED
        _refForNed = midPoint;

        const double angleDeg = _gridAngleFact.rawValue().toDouble();
        const double spacingMeters = _lineSpacingFact.rawValue().toDouble();
        QLineF midLine = _generateMidLine(midPoint, angleDeg);

        const double rad = qDegreesToRadians(angleDeg);
        const QPointF dirNED(qCos(rad), qSin(rad));

        QPolygonF nedSurveyArea = _surveyAreaToNed(_refForNed);
        QRectF bbox = nedSurveyArea.boundingRect();

        nedSurveyArea = nedSurveyArea_offsett(nedSurveyArea,2);

        QPainterPath surveyPainterPath;
        surveyPainterPath.addPolygon(nedSurveyArea);

        QList<QLineF> lines = generateTransectsNED(bbox,spacingMeters,angleDeg,_startDirectionFact.rawValue().toInt());
        lines = clipLinesWithPolygon(lines,nedSurveyArea);

        QList<QPolygonF> fences;
        // Generate a data structure that contains NED circles with center and radius
        QList<geom::Fence> nedFences;

        for (int i = 0; i < _geoFenceCircles->count(); ++i) {
            auto* fenceCircle = _geoFenceCircles->value<QGCFenceCircle*>(i);
            if (!fenceCircle) continue;

            const QGeoCoordinate center = fenceCircle->center();
            const QPointF nedCenter = geoToNedXY(center, _refForNed);
            const double radiusMeters = fenceCircle->radius()->rawValue().toDouble();

            geom::Fence fence(geom::Circle(nedCenter, radiusMeters));
            if (fence.isValid()) {
                nedFences.append(fence);

                QPainterPath circlePath;
                circlePath.addEllipse(nedCenter, radiusMeters, radiusMeters);
                surveyPainterPath = surveyPainterPath.subtracted(circlePath).simplified();
            }
        }

        for (int i = 0; i < _geoFencePolygons->count(); ++i) {
            auto* fp = _geoFencePolygons->value<QGCFencePolygon*>(i);
            if (!fp) continue;

            QPolygonF nedPoly = fencePolygonToNed(fp, _refForNed);
            QPolygonF nedPolybig = nedSurveyArea_offsett(nedPoly,-2);

            std::vector<QPointF> verts;
            verts.reserve(nedPoly.size());
            for (const QPointF& p : nedPoly) verts.push_back(p);
            geom::Fence f{geom::Polygon(std::move(verts))};
            if (f.isValid()) {
                nedFences.append(f);

                QPainterPath polyPath;
                polyPath.addPolygon(nedPolybig);
                surveyPainterPath = surveyPainterPath.subtracted(polyPath).simplified();

                lines = subtractFence(lines,nedPolybig);
                fences.append(nedPoly);
            }
        }

        // calculate tangent lines
        // --- Build tangent lines in NED for every fence ---
        QPointF linePointNED(0.0, 0.0); // because _refForNed = midPoint, the midPoint is origin in NED

        QList<QLineF> fencelines;
        for (const QPolygonF &fence : fences) {
            int n = fence.size();
            for (int i = 0; i < n; ++i) {
                QPointF p1 = fence[i];
                QPointF p2 = fence[(i + 1) % n]; // connects last point to first (closed polygon)
                fencelines.append(QLineF(p1, p2));
            }
        }

        ChinesePostmanParallel solver(lines, fencelines);
        QList<QPointF> path = solver.solve();

        if (path.size() < 2) {
            qWarning() << "Route too short:" << path.size();
            _visualTransectPoints.clear();
            _transects.clear();
            emit visualTransectPointsChanged();
            emit readyForSaveStateChanged();
            setDirty(true);
            return;
        }

        qDebug() << "Resulting route (" << path.size() << " points ):";
        double total = 0.0;
        for (int i = 0; i < path.size(); ++i) {
            if (i > 0) total += QLineF(path[i-1], path[i]).length();
        }
        qDebug() << "Approx. total route length:" << total;

        _visualTransectPoints.clear();
        _transects.clear();

        qDebug() << "clean" << total;

        // 1) include ALL points
        for (int i = 0; i < path.size()-1; ++i) {
            _visualTransectPoints.append(QVariant::fromValue(toGeoSafe(path[i],_refForNed)));
        }
        qDebug() << "visual points added" << total;

        // 2) build legs so the UI knows where entry/exit are
        for (int i = 0; i + 1 < path.size()-1; i += 2) {
            QList<CoordInfo_t> leg;
            CoordInfo_t a, b;
            a.coord = toGeoSafe(path[i],_refForNed);
            b.coord = toGeoSafe(path[i+1],_refForNed);
            a.coordType = CoordTypeSurveyEntry;
            b.coordType = CoordTypeSurveyExit;
            leg << a << b;
            _transects << leg;
        }
        qDebug() << "transects generated" << total;

        _coordinate = _visualTransectPoints.isEmpty() ? QGeoCoordinate() : _visualTransectPoints.first().value<QGeoCoordinate>();
        _exitCoordinate = _visualTransectPoints.isEmpty() ? QGeoCoordinate() : _visualTransectPoints.last().value<QGeoCoordinate>();

        qDebug() << "coordinates update" << total;

        emit coordinateChanged(_coordinate);
        emit exitCoordinateChanged(_exitCoordinate);
        emit visualTransectPointsChanged();
        emit readyForSaveStateChanged();
        emit lastSequenceNumberChanged(lastSequenceNumber());
        emit specifiesCoordinateChanged();
    }

    _flightPathSegments.beginResetModel();
    _flightPathSegments.clear();
    QGeoCoordinate prev;
    for (const QVariant& v : _visualTransectPoints) {
        const QGeoCoordinate c = v.value<QGeoCoordinate>();
        if (prev.isValid() && c.isValid()) {
            _appendFlightPathSegment(FlightPathSegment::SegmentTypeGeneric,
                                     prev, prev.altitude(),
                                     c, c.altitude());
        }
        prev = c;
    }
    _flightPathSegments.endResetModel();

    // 2) Tell controller to recompute the overall mission path
    _masterController->missionController()->recalcTerrainProfile();

    setDirty(true);
    return;
}

void AgriculturalStyleComplexItem::_polyChanged() {
    _rebuildTransects();
}

int AgriculturalStyleComplexItem::lastSequenceNumber(void) const {
    int itemCount = 0;
    for (const QList<CoordInfo_t>& seg : _transects) {
        itemCount += seg.count();
    }
    return _sequenceNumber + qMax(0, itemCount - 1);
}

// Great-circle max distance to either start or end
double AgriculturalStyleComplexItem::greatestDistanceTo(const QGeoCoordinate& other) const {
    double d1 = other.distanceTo(_coordinate);
    double d2 = other.distanceTo(_exitCoordinate);
    return qMax(d1, d2);
}

// When fixed mode, return desired speed; otherwise NaN so vehicle default/script applies
double AgriculturalStyleComplexItem::specifiedFlightSpeed(void) {
    if (_speedModeFact.rawValue().toInt() == SpeedModeFixed) {
        return _fixedSpeedFact.rawValue().toDouble();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

void AgriculturalStyleComplexItem::appendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent) {
    // For base class we only create NAV_WAYPOINTs along legs and pure turnarounds as geometry.
    // Derived classes (Spray/Spreader) will inject SCRIPT_TIME start/stop around productive legs.
    _buildAndAppendMissionItems(items, missionItemParent);
}

void AgriculturalStyleComplexItem::_buildAndAppendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent) {
    int seqNum = _sequenceNumber;

    // Optionally set speed when fixed. We keep it simple: one DO_CHANGE_SPEED at the beginning.
    if (_speedModeFact.rawValue().toInt() == SpeedModeFixed) {
        MissionItem* speed = new MissionItem(seqNum++,
                                             MAV_CMD_DO_CHANGE_SPEED,
                                             MAV_FRAME_MISSION,
                                             1, // airspeed
                                             _fixedSpeedFact.rawValue().toDouble(),
                                             -1, 0, 0, 0, 0,
                                             true, false,
                                             missionItemParent);
        items.append(speed);
    }

    const MAV_FRAME frame = MAV_FRAME_GLOBAL_RELATIVE_ALT;

    for (const QList<CoordInfo_t>& leg : _transects) {
        if (leg.size() < 2) continue;

        // Entry WP
        _appendWaypoint(items, missionItemParent, seqNum, frame, 0 /*hold*/, leg.first().coord);

        // TODO(SPRAY): insert MAV_CMD_SCRIPT_TIME to START spraying here (param mapping TBD)

        // Exit WP
        _appendWaypoint(items, missionItemParent, seqNum, frame, 0 /*hold*/, leg.last().coord);

        // TODO(SPRAY): insert MAV_CMD_SCRIPT_TIME to STOP spraying here (param mapping TBD)
    }
}

double AgriculturalStyleComplexItem::amslEntryAlt(void) const {
    // No terrain/absolute alt yet; unknown -> NaN
    return std::numeric_limits<double>::quiet_NaN();
}

double AgriculturalStyleComplexItem::amslExitAlt(void) const {
    // No terrain/absolute alt yet; unknown -> NaN
    return std::numeric_limits<double>::quiet_NaN();
}

double AgriculturalStyleComplexItem::specifiedGimbalYaw(void) {
    // No gimbal control in agriculture base
    return std::numeric_limits<double>::quiet_NaN();
}

double AgriculturalStyleComplexItem::specifiedGimbalPitch(void) {
    // No gimbal control in agriculture base
    return std::numeric_limits<double>::quiet_NaN();
}

void AgriculturalStyleComplexItem::setCoordinate(const QGeoCoordinate& coordinate) {
    // For editing UX, move polygon center; rebuild
    _surveyAreaPolygon.setCenter(coordinate);
    emit _surveyAreaPolygon.pathChanged();

    _coordinate = coordinate;
    emit coordinateChanged(_coordinate);

    _rebuildTransects();
}

void AgriculturalStyleComplexItem::applyNewAltitude(double newAltitude) {
    // Base doesn’t manage absolute alt; missions use relative alt in WP build.
    // Store on entry/exit so UI doesn’t crash if queried.
    if (_coordinate.isValid()) _coordinate.setAltitude(newAltitude);
    if (_exitCoordinate.isValid()) _exitCoordinate.setAltitude(newAltitude);

    emit coordinateChanged(_coordinate);
    emit exitCoordinateChanged(_exitCoordinate);
}

double AgriculturalStyleComplexItem::minAMSLAltitude(void) const {
    // Unknown without terrain; return NaN (QGC callers handle this)
    return std::numeric_limits<double>::quiet_NaN();
}

double AgriculturalStyleComplexItem::maxAMSLAltitude(void) const {
    // Unknown without terrain; return NaN
    return std::numeric_limits<double>::quiet_NaN();
}

// ---------------------------- Save/Load ----------------------------
QVariantList AgriculturalStyleComplexItem::fieldPolygonsVariant() const {
    QVariantList v;
    v.reserve(_fieldPolygonsMap.size());
    for (auto* p : _fieldPolygonsMap) v << QVariant::fromValue(static_cast<QObject*>(p));
    return v;
}

void AgriculturalStyleComplexItem::save(QJsonArray& planItems) {
    QJsonObject complexObject;
    QJsonObject inner;

    inner[JsonHelper::jsonVersionKey] = 1;
    inner[lineSpacingName] = _lineSpacingFact.rawValue().toDouble();
    inner[gridAngleName] = _gridAngleFact.rawValue().toDouble();
    inner[entryLocationName] = _entryLocationFact.rawValue().toInt();
    inner[speedModeName] = _speedModeFact.rawValue().toInt();
    inner[fixedSpeedName] = _fixedSpeedFact.rawValue().toDouble();
    inner[turnAroundDistanceName] = _turnAroundDistanceFact.rawValue().toDouble();
    // Terrain placeholders saved only when we later enable terrain mode

    // Save visuals (polyline of leg endpoints)
    QJsonValue visualJson;
    JsonHelper::saveGeoCoordinateArray(_visualTransectPoints, false /*writeAltitude*/, visualJson);
    inner[_jsonVisualTransectPointsKey] = visualJson;

    // Save internal mission items snapshot for reload
    QJsonArray itemsArray;
    QObject* parent = new QObject();
    QList<MissionItem*> tmp;
    appendMissionItems(tmp, parent);
    for (const MissionItem* mi : tmp) {
        QJsonObject obj;
        mi->save(obj);
        itemsArray.append(obj);
    }
    parent->deleteLater();
    inner[_jsonItemsKey] = itemsArray;

    complexObject[_jsonKey] = inner;

    QJsonObject saveObject;
    saveObject[QStringLiteral("type")] = QStringLiteral("ComplexItem");
    saveObject[QStringLiteral("complexItem")] = complexObject;

    planItems.append(saveObject);
}

bool AgriculturalStyleComplexItem::load(const QJsonObject& complexObject, int sequenceNumber, QString& errorString) {
    QList<JsonHelper::KeyValidateInfo> keyInfo = {
        {_jsonKey, QJsonValue::Object, true},
    };
    if (!JsonHelper::validateKeys(complexObject, keyInfo, errorString)) {
        return false;
    }

    _sequenceNumber = sequenceNumber;

    const QJsonObject inner = complexObject[_jsonKey].toObject();
    const int version = inner.value(JsonHelper::jsonVersionKey).toInt(1);
    if (version != 1) {
        errorString = tr("AgriculturalStyleComplexItem version %1 not supported").arg(version);
        return false;
    }

    _ignoreRecalc = true;

    _lineSpacingFact.setRawValue(inner.value(lineSpacingName).toDouble(_lineSpacingFact.rawValue().toDouble()));
    _gridAngleFact.setRawValue(inner.value(gridAngleName).toDouble(_gridAngleFact.rawValue().toDouble()));
    _entryLocationFact.setRawValue(inner.value(entryLocationName).toInt(_entryLocationFact.rawValue().toInt()));
    _speedModeFact.setRawValue(inner.value(speedModeName).toInt(_speedModeFact.rawValue().toInt()));
    _fixedSpeedFact.setRawValue(inner.value(fixedSpeedName).toDouble(_fixedSpeedFact.rawValue().toDouble()));
    _turnAroundDistanceFact.setRawValue(
        inner.value(turnAroundDistanceName).toDouble(_turnAroundDistanceFact.rawValue().toDouble()));

    // Load visuals
    if (inner.contains(_jsonVisualTransectPointsKey)) {
        // TODO: parse visuals if needed; not required for function.
        // Keeping it ignored for now to avoid schema/version mismatches.
    }

    _ignoreRecalc = false;
    setDirty(false);
    return true;
}

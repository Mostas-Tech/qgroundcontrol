// TransectPathGenerator.cpp
#include "TransectPathGenerator.h"
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <limits>

QList<TransectPathGenerator::PathSegment> TransectPathGenerator::generatePath(
    const QPolygonF& mainArea,
    const QList<QPolygonF>& fences,
    const QList<QLineF>& transectLines,
    const QPointF& entryPoint) 
{
    QList<PathSegment> path;
    
    // Step 1: Clip each transect line to main area and remove fence intersections
    QList<QList<QLineF>> validSegmentsPerTransect;
    
    for (int i = 0; i < transectLines.size(); ++i) {
        QList<QLineF> segments = clipTransectToArea(transectLines[i], mainArea, fences);
        validSegmentsPerTransect.append(segments);
    }
    
    // Step 2: Determine optimal order and direction for transects
    QList<int> transectOrder = determineOptimalOrder(validSegmentsPerTransect, entryPoint);
    
    // Step 3: Build the complete path with transect and transit segments
    for (int i = 0; i < transectOrder.size(); ++i) {
        int transectIdx = transectOrder[i];
        const QList<QLineF>& segments = validSegmentsPerTransect[transectIdx];
        
        if (segments.isEmpty()) continue;
        
        // Determine direction to minimize transit distance
        bool reverse = false;
        if (!path.isEmpty()) {
            QPointF lastPoint = path.last().end;
            qreal distToStart = QLineF(lastPoint, segments.first().p1()).length();
            qreal distToEnd = QLineF(lastPoint, segments.last().p2()).length();
            reverse = (distToEnd < distToStart);
        }
        
        // Add transit segment to reach this transect
        if (!path.isEmpty()) {
            QPointF startPoint = reverse ? segments.last().p2() : segments.first().p1();
            PathSegment transit;
            transit.start = path.last().end;
            transit.end = startPoint;
            transit.transectIndex = transectIdx;
            transit.isTransit = true;
            path.append(transit);
        }
        
        // Add transect segments
        if (reverse) {
            for (int j = segments.size() - 1; j >= 0; --j) {
                PathSegment seg;
                seg.start = segments[j].p2();
                seg.end = segments[j].p1();
                seg.transectIndex = transectIdx;
                seg.isTransit = false;
                path.append(seg);
            }
        } else {
            for (const QLineF& line : segments) {
                PathSegment seg;
                seg.start = line.p1();
                seg.end = line.p2();
                seg.transectIndex = transectIdx;
                seg.isTransit = false;
                path.append(seg);
            }
        }
    }
    
    return path;
}

QList<QLineF> TransectPathGenerator::clipTransectToArea(
    const QLineF& transect,
    const QPolygonF& mainArea,
    const QList<QPolygonF>& fences)
{
    QList<QLineF> result;
    
    // Find all intersection points with main area boundary
    QList<qreal> intersectionParams;
    intersectionParams.append(0.0);
    intersectionParams.append(1.0);
    
    // Intersect with main area polygon edges
    for (int i = 0; i < mainArea.size(); ++i) {
        QPointF p1 = mainArea[i];
        QPointF p2 = mainArea[(i + 1) % mainArea.size()];
        QLineF edge(p1, p2);
        
        QPointF intersection;
        if (transect.intersects(edge, &intersection) == QLineF::BoundedIntersection) {
            qreal t = getParameterOnLine(transect, intersection);
            if (t >= 0.0 && t <= 1.0) {
                intersectionParams.append(t);
            }
        }
    }
    
    // Intersect with fences
    QList<QPair<qreal, qreal>> fenceRanges;
    for (const QPolygonF& fence : fences) {
        QList<qreal> fenceParams;
        
        for (int i = 0; i < fence.size(); ++i) {
            QPointF p1 = fence[i];
            QPointF p2 = fence[(i + 1) % fence.size()];
            QLineF edge(p1, p2);
            
            QPointF intersection;
            if (transect.intersects(edge, &intersection) == QLineF::BoundedIntersection) {
                qreal t = getParameterOnLine(transect, intersection);
                if (t >= 0.0 && t <= 1.0) {
                    fenceParams.append(t);
                }
            }
        }
        
        // Create ranges for fence intersections (entry/exit pairs)
        if (fenceParams.size() >= 2) {
            std::sort(fenceParams.begin(), fenceParams.end());
            for (int i = 0; i < fenceParams.size() - 1; i += 2) {
                fenceRanges.append(qMakePair(fenceParams[i], fenceParams[i + 1]));
            }
        }
    }
    
    // Sort and remove duplicates
    std::sort(intersectionParams.begin(), intersectionParams.end());
    intersectionParams.erase(
        std::unique(intersectionParams.begin(), intersectionParams.end(),
            [](qreal a, qreal b) { return qAbs(a - b) < 1e-6; }),
        intersectionParams.end()
    );
    
    // Create segments, filtering out those outside main area or inside fences
    QPainterPath mainPath;
    mainPath.addPolygon(mainArea);
    
    for (int i = 0; i < intersectionParams.size() - 1; ++i) {
        qreal t1 = intersectionParams[i];
        qreal t2 = intersectionParams[i + 1];
        qreal tMid = (t1 + t2) / 2.0;
        
        QPointF midPoint = transect.pointAt(tMid);
        
        // Check if segment is inside main area
        if (!mainPath.contains(midPoint)) continue;
        
        // Check if segment intersects any fence
        bool insideFence = false;
        for (const QPolygonF& fence : fences) {
            QPainterPath fencePath;
            fencePath.addPolygon(fence);
            if (fencePath.contains(midPoint)) {
                insideFence = true;
                break;
            }
        }
        
        if (!insideFence) {
            QPointF p1 = transect.pointAt(t1);
            QPointF p2 = transect.pointAt(t2);
            result.append(QLineF(p1, p2));
        }
    }
    
    return result;
}

qreal TransectPathGenerator::getParameterOnLine(const QLineF& line, const QPointF& point) {
    QPointF v = line.p2() - line.p1();
    QPointF w = point - line.p1();
    
    qreal lenSq = v.x() * v.x() + v.y() * v.y();
    if (lenSq < 1e-10) return 0.0;
    
    return (w.x() * v.x() + w.y() * v.y()) / lenSq;
}

QList<int> TransectPathGenerator::determineOptimalOrder(
    const QList<QList<QLineF>>& validSegments,
    const QPointF& entryPoint)
{
    QList<int> order;
    QList<bool> visited(validSegments.size(), false);
    
    QPointF currentPoint = entryPoint;
    
    for (int i = 0; i < validSegments.size(); ++i) {
        int nearest = -1;
        qreal minDist = std::numeric_limits<qreal>::max();
        
        for (int j = 0; j < validSegments.size(); ++j) {
            if (visited[j] || validSegments[j].isEmpty()) continue;
            
            QPointF startPoint = validSegments[j].first().p1();
            QPointF endPoint = validSegments[j].last().p2();
            
            qreal dist = qMin(
                QLineF(currentPoint, startPoint).length(),
                QLineF(currentPoint, endPoint).length()
            );
            
            if (dist < minDist) {
                minDist = dist;
                nearest = j;
            }
        }
        
        if (nearest >= 0) {
            order.append(nearest);
            visited[nearest] = true;
            
            // Update current point
            const QList<QLineF>& segments = validSegments[nearest];
            QPointF startPoint = segments.first().p1();
            QPointF endPoint = segments.last().p2();
            
            currentPoint = (QLineF(currentPoint, endPoint).length() < 
                           QLineF(currentPoint, startPoint).length()) 
                           ? startPoint : endPoint;
        }
    }
    
    return order;
}

/* Example usage in your code:
#include "TransectPathGenerator.h"

QPolygonF mainArea;
mainArea << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 100) << QPointF(0, 100);

QList<QPolygonF> fences;
QPolygonF fence1;
fence1 << QPointF(30, 30) << QPointF(50, 30) << QPointF(50, 50) << QPointF(30, 50);
fences.append(fence1);

QList<QLineF> transectLines;
for (int i = 0; i < 10; ++i) {
    qreal x = i * 10;
    transectLines.append(QLineF(x, -10, x, 110));
}

QPointF entryPoint(0, 0);
auto path = TransectPathGenerator::generatePath(mainArea, fences, transectLines, entryPoint);

for (const auto& segment : path) {
    if (segment.isTransit) {
        // Move to position without data collection
        qDebug() << "Transit from" << segment.start << "to" << segment.end;
    } else {
        // Survey mode: collect data while moving
        qDebug() << "Survey transect" << segment.transectIndex 
                 << "from" << segment.start << "to" << segment.end;
    }
}
*/
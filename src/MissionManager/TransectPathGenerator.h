// TransectPathGenerator.h
#ifndef TRANSECTPATHGENERATOR_H
#define TRANSECTPATHGENERATOR_H

#include <QPolygonF>
#include <QLineF>
#include <QPointF>
#include <QList>

class TransectPathGenerator {
public:
    struct PathSegment {
        QPointF start;
        QPointF end;
        int transectIndex;
        bool isTransit; // true if moving between transects, false if surveying
    };

    // Main function to generate fence-aware path
    static QList<PathSegment> generatePath(
        const QPolygonF& mainArea,
        const QList<QPolygonF>& fences,
        const QList<QLineF>& transectLines,
        const QPointF& entryPoint = QPointF());

private:
    static QList<QLineF> clipTransectToArea(
        const QLineF& transect,
        const QPolygonF& mainArea,
        const QList<QPolygonF>& fences);
    
    static qreal getParameterOnLine(const QLineF& line, const QPointF& point);
    
    static QList<int> determineOptimalOrder(
        const QList<QList<QLineF>>& validSegments,
        const QPointF& entryPoint);
};

#endif // TRANSECTPATHGENERATOR_H
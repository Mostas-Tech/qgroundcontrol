#pragma once

#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

#include "QGCMapPolygon.h"
#include "QmlObjectListModel.h"

class FieldPolygonEditorController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QGCMapPolygon* mapPolygon READ mapPolygon CONSTANT)
    Q_PROPERTY(QmlObjectListModel* exclusionPolygonsModel READ exclusionPolygonsModel CONSTANT)
    Q_PROPERTY(bool polygonValid READ polygonValid NOTIFY polygonValidChanged)
    Q_PROPERTY(bool exclusionPolygonsValid READ exclusionPolygonsValid NOTIFY exclusionPolygonsValidChanged)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY vertexCountChanged)
    Q_PROPERTY(int exclusionPolygonCount READ exclusionPolygonCount NOTIFY exclusionPolygonCountChanged)

public:
    explicit FieldPolygonEditorController(QObject* parent = nullptr);

    QGCMapPolygon* mapPolygon() { return &_mapPolygon; }
    QmlObjectListModel* exclusionPolygonsModel() { return &_exclusionPolygonsModel; }
    bool polygonValid() const { return _mapPolygon.count() >= 3; }
    bool exclusionPolygonsValid() const;
    int vertexCount() const { return _mapPolygon.count(); }
    int exclusionPolygonCount() const { return _exclusionPolygonsModel.count(); }

    Q_INVOKABLE void setPolygonPath(const QVariantList& polygonPath);
    Q_INVOKABLE QVariantList polygonPath() const { return _mapPolygon.path(); }
    Q_INVOKABLE void setExclusionPolygonPaths(const QVariantList& exclusionPolygonPaths);
    Q_INVOKABLE QVariantList exclusionPolygonPaths() const;
    Q_INVOKABLE void addExclusionPolygon();
    Q_INVOKABLE void removeExclusionPolygon(int index);
    Q_INVOKABLE void clearExclusionPolygons();

signals:
    void polygonValidChanged();
    void exclusionPolygonsValidChanged();
    void vertexCountChanged();
    void exclusionPolygonCountChanged();

private slots:
    void _handlePolygonCountChanged(int count);
    void _handleExclusionPolygonCountChanged();

private:
    void _connectExclusionPolygon(QGCMapPolygon* polygon);

    QGCMapPolygon _mapPolygon;
    QmlObjectListModel _exclusionPolygonsModel;
    bool _lastPolygonValid = false;
    bool _lastExclusionPolygonsValid = true;
    int _lastExclusionPolygonCount = 0;
};

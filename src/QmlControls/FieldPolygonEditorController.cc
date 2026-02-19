#include "FieldPolygonEditorController.h"

FieldPolygonEditorController::FieldPolygonEditorController(QObject* parent)
    : QObject(parent)
    , _mapPolygon(this)
    , _exclusionPolygonsModel(this)
{
    _lastPolygonValid = polygonValid();
    _lastExclusionPolygonsValid = exclusionPolygonsValid();
    _lastExclusionPolygonCount = _exclusionPolygonsModel.count();

    connect(&_mapPolygon, &QGCMapPolygon::countChanged,
            this, &FieldPolygonEditorController::_handlePolygonCountChanged);

    connect(&_exclusionPolygonsModel, &QmlObjectListModel::countChanged,
            this, &FieldPolygonEditorController::_handleExclusionPolygonCountChanged);
}

void FieldPolygonEditorController::setPolygonPath(const QVariantList& polygonPath)
{
    _mapPolygon.setPath(polygonPath);
    _mapPolygon.setDirty(false);
}

bool FieldPolygonEditorController::exclusionPolygonsValid() const
{
    for (int i = 0; i < _exclusionPolygonsModel.count(); ++i) {
        const QGCMapPolygon* polygon = _exclusionPolygonsModel.value<QGCMapPolygon*>(i);
        if (!polygon) {
            continue;
        }
        const int vertexCount = polygon->count();
        if (vertexCount > 0 && vertexCount < 3) {
            return false;
        }
    }

    return true;
}

void FieldPolygonEditorController::setExclusionPolygonPaths(const QVariantList& exclusionPolygonPaths)
{
    clearExclusionPolygons();

    for (const QVariant& polygonPathVar : exclusionPolygonPaths) {
        const QVariantList polygonPath = polygonPathVar.toList();
        if (polygonPath.isEmpty()) {
            continue;
        }

        QGCMapPolygon* polygon = new QGCMapPolygon(this);
        polygon->setPath(polygonPath);
        polygon->setDirty(false);
        _connectExclusionPolygon(polygon);
        _exclusionPolygonsModel.append(polygon);
    }

    _handleExclusionPolygonCountChanged();
}

QVariantList FieldPolygonEditorController::exclusionPolygonPaths() const
{
    QVariantList exclusionPolygonPaths;
    for (int i = 0; i < _exclusionPolygonsModel.count(); ++i) {
        const QGCMapPolygon* polygon = _exclusionPolygonsModel.value<QGCMapPolygon*>(i);
        if (!polygon || polygon->count() < 3) {
            continue;
        }
        exclusionPolygonPaths.append(polygon->path());
    }

    return exclusionPolygonPaths;
}

void FieldPolygonEditorController::addExclusionPolygon()
{
    QGCMapPolygon* polygon = new QGCMapPolygon(this);
    polygon->setDirty(false);
    _connectExclusionPolygon(polygon);
    _exclusionPolygonsModel.append(polygon);
    _handleExclusionPolygonCountChanged();
}

void FieldPolygonEditorController::removeExclusionPolygon(int index)
{
    if (index < 0 || index >= _exclusionPolygonsModel.count()) {
        return;
    }

    QObject* polygonObject = _exclusionPolygonsModel.removeAt(index);
    if (polygonObject) {
        polygonObject->deleteLater();
    }

    _handleExclusionPolygonCountChanged();
}

void FieldPolygonEditorController::clearExclusionPolygons()
{
    _exclusionPolygonsModel.clearAndDeleteContents();
    _handleExclusionPolygonCountChanged();
}

void FieldPolygonEditorController::_handlePolygonCountChanged(int count)
{
    Q_UNUSED(count)

    emit vertexCountChanged();

    const bool newValid = polygonValid();
    if (_lastPolygonValid != newValid) {
        _lastPolygonValid = newValid;
        emit polygonValidChanged();
    }
}

void FieldPolygonEditorController::_handleExclusionPolygonCountChanged()
{
    const int newCount = _exclusionPolygonsModel.count();
    if (newCount != _lastExclusionPolygonCount) {
        _lastExclusionPolygonCount = newCount;
        emit exclusionPolygonCountChanged();
    }

    const bool newValid = exclusionPolygonsValid();
    if (_lastExclusionPolygonsValid != newValid) {
        _lastExclusionPolygonsValid = newValid;
        emit exclusionPolygonsValidChanged();
    }
}

void FieldPolygonEditorController::_connectExclusionPolygon(QGCMapPolygon* polygon)
{
    if (!polygon) {
        return;
    }

    connect(polygon, &QGCMapPolygon::countChanged, this, [this](int) {
        _handleExclusionPolygonCountChanged();
    });

    connect(polygon, &QGCMapPolygon::pathChanged, this, [this]() {
        _handleExclusionPolygonCountChanged();
    });
}

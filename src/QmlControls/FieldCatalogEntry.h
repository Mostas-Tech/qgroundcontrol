#pragma once

#include <QtCore/QObject>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

class FieldCatalogEntry : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(QString id MEMBER _id CONSTANT)
    Q_PROPERTY(QString name MEMBER _name CONSTANT)
    Q_PROPERTY(QString thumbnailFilePath MEMBER _thumbnailFilePath CONSTANT)
    Q_PROPERTY(QVariantList polygonPath MEMBER _polygonPath CONSTANT)
    Q_PROPERTY(QVariantList exclusionPolygonPaths MEMBER _exclusionPolygonPaths CONSTANT)
    Q_PROPERTY(int jobCount MEMBER _jobCount CONSTANT)
    Q_PROPERTY(QString createdAtUtc MEMBER _createdAtUtc CONSTANT)
    Q_PROPERTY(QString updatedAtUtc MEMBER _updatedAtUtc CONSTANT)
    Q_PROPERTY(int schemaVersion MEMBER _schemaVersion CONSTANT)
    Q_PROPERTY(int revision MEMBER _revision CONSTANT)
    Q_PROPERTY(bool syncPending MEMBER _syncPending CONSTANT)

public:
    explicit FieldCatalogEntry(QObject *parent = nullptr);

    QString _id;
    QString _name;
    QString _thumbnailFilePath;
    QVariantList _polygonPath;
    QVariantList _exclusionPolygonPaths;
    int _jobCount = 0;
    QString _createdAtUtc;
    QString _updatedAtUtc;
    int _schemaVersion = 1;
    int _revision = 0;
    bool _syncPending = false;
};

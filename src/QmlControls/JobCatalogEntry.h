#pragma once

#include <QtCore/QObject>
#include <QtQmlIntegration/QtQmlIntegration>

class JobCatalogEntry : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(QString id MEMBER _id CONSTANT)
    Q_PROPERTY(QString fieldId MEMBER _fieldId CONSTANT)
    Q_PROPERTY(QString name MEMBER _name CONSTANT)
    Q_PROPERTY(QString planFilePath MEMBER _planFilePath CONSTANT)
    Q_PROPERTY(QString thumbnailFilePath MEMBER _thumbnailFilePath CONSTANT)
    Q_PROPERTY(double pesticideLitersPerDekar MEMBER _pesticideLitersPerDekar CONSTANT)
    Q_PROPERTY(double dropletSizeMicron MEMBER _dropletSizeMicron CONSTANT)
    Q_PROPERTY(double nozzleValuePct MEMBER _nozzleValuePct CONSTANT)
    Q_PROPERTY(double boomWidthM MEMBER _boomWidthM CONSTANT)
    Q_PROPERTY(QString notes MEMBER _notes CONSTANT)
    Q_PROPERTY(bool needsRegeneration MEMBER _needsRegeneration CONSTANT)
    Q_PROPERTY(int fieldRevisionRequired MEMBER _fieldRevisionRequired CONSTANT)
    Q_PROPERTY(QString createdAtUtc MEMBER _createdAtUtc CONSTANT)
    Q_PROPERTY(QString updatedAtUtc MEMBER _updatedAtUtc CONSTANT)
    Q_PROPERTY(int schemaVersion MEMBER _schemaVersion CONSTANT)
    Q_PROPERTY(int revision MEMBER _revision CONSTANT)
    Q_PROPERTY(bool syncPending MEMBER _syncPending CONSTANT)

public:
    explicit JobCatalogEntry(QObject *parent = nullptr);

    QString _id;
    QString _fieldId;
    QString _name;
    QString _planFilePath;
    QString _thumbnailFilePath;
    double _pesticideLitersPerDekar = 0.0;
    double _dropletSizeMicron = 0.0;
    double _nozzleValuePct = 0.0;
    double _boomWidthM = 0.0;
    QString _notes;
    bool _needsRegeneration = false;
    int _fieldRevisionRequired = 0;
    QString _createdAtUtc;
    QString _updatedAtUtc;
    int _schemaVersion = 1;
    int _revision = 0;
    bool _syncPending = false;
};

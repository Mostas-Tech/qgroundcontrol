#pragma once

#include "ComplexMissionItem.h"
#include "MissionItem.h"
#include "SettingsFact.h"
#include "QGCMapPolygon.h"

#include <QPolygonF>
#include <QLineF>
#include <QRectF>
#include <QPointF>
#include <QJsonArray>
#include <QJsonObject>
#include <QtCore/QLoggingCategory>
#include <limits>
#include <QPainterPath>

class PlanMasterController;
class QmlObjectListModel;
class QGCFencePolygon;
class QGCFenceCircle;
class Vehicle;

Q_DECLARE_LOGGING_CATEGORY(AgriculturalStyleComplexItemLog)

/// Camera-agnostic, agriculture row/leg generator base for Spray/Spreader.
/// - Generates parallel legs from a user polygon using spacing + angle
/// - No camera calc/trigger logic
/// - Multirotor-only defaults (turnaround distance)
class AgriculturalStyleComplexItem : public ComplexMissionItem
{
    Q_OBJECT

public:
    /// @param flyView true: for Fly View, false: for Plan View
    AgriculturalStyleComplexItem(PlanMasterController* masterController, bool flyView);

    void setSequenceNumber(int sequenceNumber) override;
    void setDirty(bool dirty) override;

    // ---------------------------- UI / QML API ----------------------------
    Q_PROPERTY(QGCMapPolygon* surveyAreaPolygon READ surveyAreaPolygon CONSTANT)
    Q_PROPERTY(QVariantList   fieldPolygonsMap  READ fieldPolygonsVariant NOTIFY fieldPolygonsMapChanged)

    Q_PROPERTY(QVariantList   visualTransectPoints       READ visualTransectPoints       NOTIFY visualTransectPointsChanged)
    Q_PROPERTY(QVariantList   visualFieldTransectPairs   READ visualFieldTransectPairs   NOTIFY visualFieldTransectPairsChanged)
    Q_PROPERTY(QGeoCoordinate coordinate                 READ coordinate                 NOTIFY coordinateChanged)
    Q_PROPERTY(QGeoCoordinate exitCoordinate             READ exitCoordinate             NOTIFY exitCoordinateChanged)

    Q_PROPERTY(Fact* lineSpacing    READ lineSpacing    CONSTANT)
    Q_PROPERTY(Fact* gridAngle      READ gridAngle      CONSTANT)
    Q_PROPERTY(Fact* entryLocation  READ entryLocation  CONSTANT)

    Q_PROPERTY(Fact* speedMode      READ speedMode      CONSTANT) // 0=Auto 1=Fixed
    Q_PROPERTY(Fact* fixedSpeed     READ fixedSpeed     CONSTANT) // m/s
    Q_PROPERTY(Fact* pesticideLitersPerDekar READ pesticideLitersPerDekar CONSTANT)
    Q_PROPERTY(Fact* pesticideDropletSize READ pesticideDropletSize CONSTANT)
    Q_PROPERTY(Fact* spraySpeedProfile READ spraySpeedProfile CONSTANT)
    Q_PROPERTY(double recommendedVehicleSpeed READ recommendedVehicleSpeed NOTIFY spraySolutionChanged)
    Q_PROPERTY(double recommendedFlowRate    READ recommendedFlowRate    NOTIFY spraySolutionChanged)
    Q_PROPERTY(bool   spraySolutionValid     READ spraySolutionValid     NOTIFY spraySolutionChanged)
    Q_PROPERTY(QString spraySolutionStatus   READ spraySolutionStatus   NOTIFY spraySolutionChanged)
    Q_PROPERTY(bool sprayParametersConfirmed READ sprayParametersConfirmed NOTIFY sprayParametersConfirmedChanged)

    Q_PROPERTY(Fact* turnAroundDistance         READ turnAroundDistance         CONSTANT)
    Q_PROPERTY(Fact* fieldPadding              READ fieldPadding              CONSTANT)
    Q_PROPERTY(Fact* terrainAdjustTolerance     READ terrainAdjustTolerance     CONSTANT)
    Q_PROPERTY(Fact* terrainAdjustMaxClimbRate  READ terrainAdjustMaxClimbRate  CONSTANT)
    Q_PROPERTY(Fact* terrainAdjustMaxDescentRate READ terrainAdjustMaxDescentRate CONSTANT)

    Q_INVOKABLE void rotateEntryPoint(); // callable from QML
    Q_INVOKABLE void updatetransect();   // callable from QML
    Q_INVOKABLE void recalcMissionItems();
    Q_INVOKABLE void confirmSprayParameters();


    // Accessors for QML
    QGCMapPolygon* surveyAreaPolygon() { return &_surveyAreaPolygon; }
    QVariantList   fieldPolygonsVariant() const;
    QVariantList   visualTransectPoints()     { return _visualTransectPoints; }
    QVariantList   visualFieldTransectPairs() { return _visualFieldTransectPairs; }

    Fact* lineSpacing()   { return &_lineSpacingFact; }
    Fact* gridAngle()     { return &_gridAngleFact; }
    Fact* entryLocation() { return &_entryLocationFact; }

    Fact* speedMode()     { return &_speedModeFact; }
    Fact* fixedSpeed()    { return &_fixedSpeedFact; }
    Fact* pesticideLitersPerDekar() { return &_pesticideLitersPerDekarFact; }
    Fact* pesticideDropletSize()    { return &_pesticideDropletSizeFact; }
    Fact* spraySpeedProfile()       { return &_spraySpeedProfileFact; }
    double recommendedVehicleSpeed() const { return _recommendedVehicleSpeed; }
    double recommendedFlowRate() const { return _recommendedFlowRate; }
    bool   spraySolutionValid() const { return _spraySolutionValid; }
    QString spraySolutionStatus() const { return _spraySolutionStatus; }
    bool sprayParametersConfirmed() const { return _sprayParametersConfirmed; }

    Fact* turnAroundDistance()         { return &_turnAroundDistanceFact; }
    Fact* fieldPadding()                { return &_fieldPaddingFact; }
    Fact* terrainAdjustTolerance()     { return &_terrainAdjustToleranceFact; }
    Fact* terrainAdjustMaxClimbRate()  { return &_terrainAdjustMaxClimbRateFact; }
    Fact* terrainAdjustMaxDescentRate(){ return &_terrainAdjustMaxDescentRateFact; }

    // ---------------------------- ComplexMissionItem overrides ----------------------------
    // Visuals
    QString mapVisualQML() const override { return QStringLiteral("AgriculturalStyleMapVisuals.qml"); }

    // Persistence
    void  save(QJsonArray& planItems) override;
    bool  load(const QJsonObject& complexObject, int sequenceNumber, QString& errorString) override;

    // Geometry/coords
    int    lastSequenceNumber() const override;
    double complexDistance() const override { return _complexDistance; }
    double greatestDistanceTo(const QGeoCoordinate& other) const override;

    // VisualMissionItem base
    bool           isSimpleItem()           const override { return false; }
    bool           isStandaloneCoordinate() const override { return false; }
    bool           specifiesAltitudeOnly()  const override { return false; }
    QGeoCoordinate coordinate()             const override { return _coordinate; }
    QGeoCoordinate exitCoordinate()         const override { return _exitCoordinate; }
    int            sequenceNumber()         const override { return _sequenceNumber; }

    // --- VisualMissionItem required overrides ---
    bool specifiesCoordinate() const override { return _coordinate.isValid(); }

    bool   dirty()                 const override { return _dirty; }
    double amslEntryAlt()          const override;
    double amslExitAlt()           const override;
    double specifiedGimbalYaw()          override;
    double specifiedGimbalPitch()        override;
    bool   exitCoordinateSameAsEntry() const override { return false; }
    void   setCoordinate(const QGeoCoordinate& coordinate) override;
    void   applyNewAltitude(double newAltitude) override;
    double additionalTimeDelay() const override { return 0.0; } // no extra delay

    // --- ComplexMissionItem required overrides ---
    double minAMSLAltitude() const override;
    double maxAMSLAltitude() const override;

    // Speed: Fixed when selected; otherwise undefined (vehicle/script decides)
    double specifiedFlightSpeed() override;

    // Mission item building
    void appendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent) override;

    // Base pattern strings (can be overridden by derived classes)
    QString commandDescription() const override { return tr("Agricultural Pattern"); }
    QString commandName()        const override { return tr("Agricultural"); }
    QString abbreviation()       const override { return tr("AG"); }

    // Entry location enums (must match JSON metadata indices)
    enum EntryLocation {
        EntryLocationTopLeft = 0,
        EntryLocationTopRight,
        EntryLocationBottomLeft,
        EntryLocationBottomRight
    };

    // Speed mode enums (must match JSON metadata indices)
    enum SpeedMode {
        SpeedModeAuto  = 0,   ///< Script/vehicle controls airspeed along legs
        SpeedModeFixed = 1    ///< Use fixedSpeed (m/s)
    };

    enum SpraySpeedProfile {
        SpraySpeedProfileNormal = 0,
        SpraySpeedProfileFast   = 1,
    };

signals:
    void visualTransectPointsChanged();
    void visualFieldTransectPairsChanged();
    void fieldPolygonsMapChanged();
    void spraySolutionChanged();
    void sprayParametersConfirmedChanged();

protected:
    // Row/leg geometry representation (like Transect but camera-agnostic)
    enum CoordType {
        CoordTypeInterior,      ///< interior waypoint for flight path
        CoordTypeSurveyEntry,   ///< entry edge of polygon
        CoordTypeSurveyExit,    ///< exit edge of polygon
        CoordTypeTurnaround     ///< turnaround extension
    };

    struct CoordInfo_t {
        QGeoCoordinate  coord;
        CoordType       coordType = CoordTypeInterior;
    };

    // Recalc legs when inputs change
    void _rebuildTransects();
    void _recalcComplexDistance();
    void _bindVehicleParameterFactsIfNeeded();
    void _handleVehicleParametersReady(bool ready);
    void _handleManagerVehicleChanged(Vehicle* vehicle);
    void _resetVehicleParameterFacts();
    void _handleSprayInputsEdited();
    bool _sprayInputsValid() const;
    void _setSprayParametersConfirmed(bool confirmed);
    void _applyOptimizedSpacing(double spacingMeters);

    // Builders
    void _buildAndAppendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent);
    void _appendWaypoint(QList<MissionItem*>& items, QObject* missionItemParent, int& seqNum,
                         MAV_FRAME mavFrame, float holdTime, const QGeoCoordinate& coordinate);
    void _appendLoadedMissionItems(QList<MissionItem*>& items, QObject* missionItemParent);
    void _applyPesticideCalculations();

    // Helpers
    QList<QLineF> _generateParallelLines(const QPolygonF& area, double spacingMeters, double angleDeg) const; // centerlines before clipping
    QList<QList<CoordInfo_t>> _clipLinesToPolygon(const QList<QLineF>& lines, const QPolygonF& area) const;
    QList<QList<CoordInfo_t>> _orderLegsEntryFirst(const QList<QList<CoordInfo_t>>& legs) const; // honors entryLocation; no zigzag

protected slots:
    void _polyChanged();

private:
    // Geo helpers used by the current implementation
    QPolygonF _surveyAreaToNed(const QGeoCoordinate& ref) const;
    static QPolygonF fencePolygonToNed(const QGCFencePolygon* fence, const QGeoCoordinate& ref);
    QLineF _generateMidLine(const QGeoCoordinate& center, double angleDeg) const;

private:
    // State
    int            _sequenceNumber = 0;
    QGeoCoordinate _coordinate;     ///< first coord of first productive leg
    QGeoCoordinate _exitCoordinate; ///< last coord of last leg (before return)

    QGeoCoordinate _refForNed;
    QVector<QGeoCoordinate> _outerGeo;
    QVector<QVector<QGeoCoordinate>> _holePolysGeo;
    QVector<std::pair<QGeoCoordinate,double>> _circleHolesGeo;

    double _robotRadiusM{2};

    QGCMapPolygon     _surveyAreaPolygon;
    QList<QGCMapPolygon*> _fieldPolygonsMap;
    QVariantList      _visualTransectPoints;      ///< rendered preview polyline
    QVariantList      _visualFieldTransectPairs;  ///< rendered field preview polyline
    QList<QList<CoordInfo_t>> _transects;         ///< legs (ordered, no zigzag)

    // Fences (provided by GeoFenceController in the .cpp)
    QmlObjectListModel* _geoFenceCircles  = nullptr;
    QmlObjectListModel* _geoFencePolygons = nullptr;

    // Distance/time
    bool   _ignoreRecalc   = false;
    double _complexDistance = qQNaN();

    // Facts
    QMap<QString, FactMetaData*> _metaDataMap;

    SettingsFact _lineSpacingFact;
    SettingsFact _gridAngleFact;
    SettingsFact _entryLocationFact;
    SettingsFact _speedModeFact;
    SettingsFact _fixedSpeedFact;
    SettingsFact _pesticideLitersPerDekarFact;
    SettingsFact _pesticideDropletSizeFact;
    SettingsFact _spraySpeedProfileFact;
    SettingsFact _turnAroundDistanceFact;
    SettingsFact _fieldPaddingFact;
    SettingsFact _terrainAdjustToleranceFact;
    SettingsFact _terrainAdjustMaxClimbRateFact;
    SettingsFact _terrainAdjustMaxDescentRateFact;
    SettingsFact _startDirectionFact;

    Fact* _vehicleMinSpeedFact = nullptr;
    Fact* _vehicleMaxSpeedFact = nullptr;
    Fact* _vehicleMinFlowFact = nullptr;
    Fact* _vehicleMaxFlowFact = nullptr;
    bool  _vehicleParamFactsBound = false;
    Vehicle* _vehicleFactSource = nullptr;

    double  _recommendedVehicleSpeed = qQNaN();
    double  _recommendedFlowRate = qQNaN();
    bool    _spraySolutionValid = false;
    QString _spraySolutionStatus;
    bool    _sprayParametersConfirmed = false;
    double  _lastOptimizedSpacing = qQNaN();
    bool    _lastOptimizedSpacingValid = false;

    QObject*            _loadedMissionItemsParent = nullptr;    ///< Parent for loaded mission items
    QList<MissionItem*> _loadedMissionItems;                    ///< Mission items loaded from plan file

    // JSON keys
    static constexpr const char* _jsonKey                         = "AgriculturalStyleComplexItem";
    static constexpr const char* _jsonVisualTransectPointsKey     = "visualTransectPoints";
    static constexpr const char* _jsonVisualFieldTransectPairsKey = "visualFieldTransectPairs";
    static constexpr const char* _jsonItemsKey                    = "Items";
    static constexpr const char* _jsonVehicleSpeedKey             = "VehicleSpeed"; // reserved for future terrain mode
    static constexpr const char* _jsonSprayInputsConfirmedKey     = "sprayInputsConfirmed";

    // Settings names (must match Agriculture.SettingsGroup.json)
    static constexpr const char* lineSpacingName                  = "LineSpacing";
    static constexpr const char* gridAngleName                    = "GridAngle";
    static constexpr const char* entryLocationName                = "EntryLocation";
    static constexpr const char* speedModeName                    = "SpeedMode";
    static constexpr const char* fixedSpeedName                   = "FixedSpeed";
    static constexpr const char* pesticideLitersPerDekarName      = "PesticideLitersPerDekar";
    static constexpr const char* pesticideDropletSizeName         = "PesticideDropletSize";
    static constexpr const char* spraySpeedProfileName            = "SpraySpeedProfile";
    static constexpr const char* turnAroundDistanceName           = "TurnAroundDistanceMultiRotor"; // multirotor only
    static constexpr const char* fieldPaddingName                 = "FieldPadding";
    static constexpr const char* terrainAdjustToleranceName       = "TerrainAdjustTolerance";
    static constexpr const char* terrainAdjustMaxClimbRateName    = "TerrainAdjustMaxClimbRate";
    static constexpr const char* terrainAdjustMaxDescentRateName  = "TerrainAdjustMaxDescentRate";
};

#pragma once

#include "ComplexMissionItem.h"
#include "MissionItem.h"
#include "SettingsFact.h"
#include "QGCMapPolygon.h"
#include "TerrainQuery.h"
#include <QPolygonF>
#include <QLineF>
#include <QRectF>
#include <QPointF>
#include <limits>
#include "GeoFenceManager.h"
#include <QtCore/QLoggingCategory>
#include "QGCFencePolygon.h"
#include "QGCFenceCircle.h"
#include "QPainterPath.h"

Q_DECLARE_LOGGING_CATEGORY(AgriculturalStyleComplexItemLog)

class PlanMasterController;

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

    void            setSequenceNumber      (int sequenceNumber) override;
    void            setDirty               (bool dirty) override;

	    // ---------------------------- UI / QML API ----------------------------
    Q_PROPERTY(QGCMapPolygon*       surveyAreaPolygon   READ surveyAreaPolygon                                  CONSTANT)
    Q_PROPERTY(QVariantList fieldPolygonsMap READ fieldPolygonsVariant NOTIFY fieldPolygonsMapChanged)

    Q_PROPERTY(QVariantList     visualTransectPoints READ visualTransectPoints                               NOTIFY visualTransectPointsChanged)
    Q_PROPERTY(QVariantList visualFieldTransectPairs READ visualFieldTransectPairs NOTIFY visualFieldTransectPairsChanged)
    Q_PROPERTY(QGeoCoordinate coordinate READ coordinate NOTIFY coordinateChanged)
    Q_PROPERTY(QGeoCoordinate exitCoordinate READ exitCoordinate NOTIFY exitCoordinateChanged)      

    
    Q_PROPERTY(Fact*            lineSpacing         READ lineSpacing                                         CONSTANT)
    Q_PROPERTY(Fact*            gridAngle           READ gridAngle                                           CONSTANT)
    Q_PROPERTY(Fact*            entryLocation       READ entryLocation                                       CONSTANT)

    Q_PROPERTY(Fact*            speedMode           READ speedMode                                           CONSTANT) // 0=Auto 1=Fixed
    Q_PROPERTY(Fact*            fixedSpeed          READ fixedSpeed                                          CONSTANT) // m/s

    Q_PROPERTY(Fact*            turnAroundDistance  READ turnAroundDistance                                  CONSTANT)
    Q_PROPERTY(Fact*            terrainAdjustTolerance      READ terrainAdjustTolerance                      CONSTANT)
    Q_PROPERTY(Fact*            terrainAdjustMaxClimbRate   READ terrainAdjustMaxClimbRate                   CONSTANT)
    Q_PROPERTY(Fact*            terrainAdjustMaxDescentRate READ terrainAdjustMaxDescentRate                 CONSTANT)
    Q_INVOKABLE void rotateEntryPoint(void); // callable from QML
    Q_INVOKABLE void updatetransect(void); // callable from QML
	    // Accessors for QML
    QGCMapPolygon*  surveyAreaPolygon   (void)      { return &_surveyAreaPolygon; }
    QVariantList fieldPolygonsVariant() const;
    QVariantList    visualTransectPoints(void)      { return _visualTransectPoints; }
    QVariantList    visualFieldTransectPairs(void) { return _visualFieldTransectPairs; }

    Fact* lineSpacing            (void) { return &_lineSpacingFact; }
    Fact* gridAngle              (void) { return &_gridAngleFact; }


    Fact* speedMode              (void) { return &_speedModeFact; }
    Fact* fixedSpeed             (void) { return &_fixedSpeedFact; }
    Fact* entryLocation         (void) { return &_entryLocationFact; }

    Fact* turnAroundDistance     (void) { return &_turnAroundDistanceFact; }

    Fact* terrainAdjustTolerance     (void) { return &_terrainAdjustToleranceFact; }
    Fact* terrainAdjustMaxClimbRate  (void) { return &_terrainAdjustMaxClimbRateFact; }
    Fact* terrainAdjustMaxDescentRate(void) { return &_terrainAdjustMaxDescentRateFact; }

	    // ---------------------------- ComplexMissionItem overrides ----------------------------
	    // Visuals
    QString         mapVisualQML        (void) const override { return QStringLiteral("AgriculturalStyleMapVisuals.qml"); }

	    // Persistence
    void            save                (QJsonArray& planItems) override;
    bool            load                (const QJsonObject& complexObject, int sequenceNumber, QString& errorString) override;

	    // Geometry/coords
    int             lastSequenceNumber  (void) const override;
    double          complexDistance     (void) const override { return _complexDistance; }
    double          greatestDistanceTo  (const QGeoCoordinate &other) const override;

	    // VisualMissionItem base
    bool            isSimpleItem                (void) const override { return false; }
    bool            isStandaloneCoordinate      (void) const override { return false; }
    bool            specifiesAltitudeOnly       (void) const override { return false; }
    QGeoCoordinate  coordinate                  (void) const override { return _coordinate; }
    QGeoCoordinate  exitCoordinate              (void) const override { return _exitCoordinate; }
    int             sequenceNumber              (void) const override { return _sequenceNumber; }
    // --- VisualMissionItem required overrides ---
    bool specifiesCoordinate() const override       { return _coordinate.isValid(); }

    bool    dirty                       (void) const override { return _dirty; }
    double  amslEntryAlt                (void) const override;
    double  amslExitAlt                 (void) const override;
    double  specifiedGimbalYaw          (void) override;
    double  specifiedGimbalPitch        (void) override;
    bool    exitCoordinateSameAsEntry   (void) const override { return false; }
    void    setCoordinate               (const QGeoCoordinate& coordinate) override;
    void    applyNewAltitude            (double newAltitude) override;
    double  additionalTimeDelay         (void) const override { return 0.0; } // no extra delay

    // --- ComplexMissionItem required overrides ---
    double  minAMSLAltitude             (void) const override;
    double  maxAMSLAltitude             (void) const override;

	    // Speed: Fixed when selected; otherwise undefined (vehicle/script decides)
    double          specifiedFlightSpeed        (void) override;

	    // Mission item building
    void            appendMissionItems          (QList<MissionItem*>& items, QObject* missionItemParent) override;

	    // Base pattern strings (can be overridden by derived classes)
    QString         commandDescription  (void) const override { return tr("Agricultural Pattern"); }
    QString         commandName         (void) const override { return tr("Agricultural"); }
    QString         abbreviation        (void) const override { return tr("AG"); }

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

	    // ---------------------------- Signals ----------------------------
   signals:
    void visualTransectPointsChanged();
    void visualFieldTransectPairsChanged();
    void fieldPolygonsMapChanged();
	    // ---------------------------- Internals ----------------------------
   private:
    // In AgriculturalStyleComplexItem.h (private section)
    QPolygonF _surveyAreaToNed(const QGeoCoordinate& ref) const;
    static QPolygonF fencePolygonToNed(const QGCFencePolygon* fence, const QGeoCoordinate& ref);
    QLineF _generateMidLine(const QGeoCoordinate& center, double angleDeg) const;
   protected:
    // Row/leg geometry representation (like Transect but camera-agnostic)
    enum CoordType {
	CoordTypeInterior,              ///< interior waypoint for flight path
	CoordTypeSurveyEntry,           ///< entry edge of polygon
	CoordTypeSurveyExit,            ///< exit edge of polygon
	CoordTypeTurnaround             ///< turnaround extension
    };

    struct CoordInfo_t {
	QGeoCoordinate  coord;
	CoordType       coordType = CoordTypeInterior;
    };

	    // Recalc legs when inputs change
    void        _rebuildTransects();
    void        _recalcComplexDistance();

	    // Builders
    void        _buildAndAppendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent);
    void        _appendWaypoint(QList<MissionItem*>& items, QObject* missionItemParent, int& seqNum, MAV_FRAME mavFrame, float holdTime, const QGeoCoordinate& coordinate);

	    // Helpers
    QList<QLineF>    _generateParallelLines(const QPolygonF& area, double spacingMeters, double angleDeg) const; // centerlines before clipping
    QList<QList<CoordInfo_t>> _clipLinesToPolygon(const QList<QLineF>& lines, const QPolygonF& area) const;
    QList<QList<CoordInfo_t>> _orderLegsEntryFirst(const QList<QList<CoordInfo_t>>& legs) const; // honors entryLocation; no zigzag
    QList<QPolygonF> fieldPolygons;
   protected slots:
    void _polyChanged();

   protected:
    // State
    int                 _sequenceNumber = 0;
    QGeoCoordinate      _coordinate;          ///< first coord of first productive leg
    QGeoCoordinate      _exitCoordinate;      ///< last coord of last leg (before return)

	QGeoCoordinate _refForNed;
	QVector<QGeoCoordinate> _outerGeo;
	QVector<QVector<QGeoCoordinate>> _holePolysGeo;
	QVector<std::pair<QGeoCoordinate,double>> _circleHolesGeo;

	double _robotRadiusM{2};

    QGCMapPolygon           _surveyAreaPolygon;
    QList<QGCMapPolygon*> _fieldPolygonsMap;
    QVariantList        _visualTransectPoints;       ///< rendered preview polyline
    QVariantList        _visualFieldTransectPairs;  ///< rendered field preview polyline
    QList<QList<CoordInfo_t>> _transects;            ///< legs (ordered, no zigzag)
    QmlObjectListModel* _geoFenceCircles  = nullptr;
    QmlObjectListModel* _geoFencePolygons = nullptr;


	    // Distance/time
    bool            _ignoreRecalc = false;
    double          _complexDistance = qQNaN();

	    // Facts
    QMap<QString, FactMetaData*> _metaDataMap;

    SettingsFact _lineSpacingFact;
    SettingsFact _gridAngleFact;
    SettingsFact _entryLocationFact;
    SettingsFact _speedModeFact;
    SettingsFact _fixedSpeedFact;
    SettingsFact _turnAroundDistanceFact;
    SettingsFact _terrainAdjustToleranceFact;
    SettingsFact _terrainAdjustMaxClimbRateFact;
    SettingsFact _terrainAdjustMaxDescentRateFact;
    SettingsFact _startDirectionFact;

	    // JSON keys
    static constexpr const char* _jsonKey                          = "AgriculturalStyleComplexItem";
    static constexpr const char* _jsonVisualTransectPointsKey      = "visualTransectPoints";
    static constexpr const char* _jsonVisualFieldTransectPairsKey  = "visualFieldTransectPairs";
    static constexpr const char* _jsonItemsKey                     = "Items";
    static constexpr const char* _jsonVehicleSpeedKey              = "VehicleSpeed"; // reserved for future terrain mode

	    // Settings names (must match Agriculture.SettingsGroup.json)
    static constexpr const char* lineSpacingName                   = "LineSpacing";
    static constexpr const char* gridAngleName                     = "GridAngle";
    static constexpr const char* entryLocationName                 = "EntryLocation";
    static constexpr const char* speedModeName                     = "SpeedMode";
    static constexpr const char* fixedSpeedName                    = "FixedSpeed";
    static constexpr const char* turnAroundDistanceName            = "TurnAroundDistanceMultiRotor"; // multirotor only
    static constexpr const char* terrainAdjustToleranceName        = "TerrainAdjustTolerance";
    static constexpr const char* terrainAdjustMaxClimbRateName     = "TerrainAdjustMaxClimbRate";
    static constexpr const char* terrainAdjustMaxDescentRateName   = "TerrainAdjustMaxDescentRate";
};

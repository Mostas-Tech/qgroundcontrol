#include "SprayComplexItem.h"

#include "PlanMasterController.h"
#include "VisualMissionItem.h"
#include "ComplexMissionItem.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

QGC_LOGGING_CATEGORY(SprayComplexItemLog, "qgc.missionmanager.spray")

const QString SprayComplexItem::name(QStringLiteral("Spray"));
static constexpr int kMavCmdNavSprayWaypoint = 42710;
static constexpr int kMavCmdDoSpraySettings = 42711;
static constexpr float kEntryYawAlignmentHoldSeconds = 5.0f;
static constexpr float kEntryYawRateDegPerSec = 30.0f;

SprayComplexItem::SprayComplexItem(PlanMasterController* masterController, bool flyView)
    : AgriculturalStyleComplexItem(masterController, flyView)
{
    _editorQml = "qrc:/qml/QGroundControl/Controls/SprayItemEditor.qml";
    // Spray-only Facts (rate, droplet) and script-time behavior can be added here.
}

void SprayComplexItem::appendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent)
{
    // Keep base save/load behavior for spray settings/waypoints.
    AgriculturalStyleComplexItem::appendMissionItems(items, missionItemParent);
}

MissionItem* SprayComplexItem::_createScriptTimeItem(int sequenceNumber, int action, MAV_FRAME frame,
                                                     QObject* missionItemParent) const
{
    const double modeValue = (_scriptTimeInfoMode >= 0.5) ? 1.0 : 0.0;
    return new MissionItem(sequenceNumber,
                           static_cast<MAV_CMD>(kMavCmdDoSpraySettings),
                           frame,
                           action,
                           0.0,
                           modeValue,
                           0.0,
                           0.0,
                           0.0,
                           0.0,
                           true,
                           false,
                           missionItemParent);
}

void SprayComplexItem::_appendPostEntryMissionItems(QList<MissionItem*>& items,
                                                    QObject* missionItemParent,
                                                    int& seqNum,
                                                    MAV_FRAME frame,
                                                    const QList<AgriculturalStyleComplexItem::CoordInfo_t>& leg)
{
    Q_UNUSED(frame);

    if (!_entryYawAlignmentEnabled || leg.size() < 2) {
        return;
    }

    const double heading = leg.first().coord.azimuthTo(leg.last().coord);

    // Configure the already-appended entry waypoint for immediate pass-through with yaw hint.
    if (!items.isEmpty() && items.last()->command() == MAV_CMD_NAV_WAYPOINT) {
        items.last()->setParam1(0.0);                           // no hold here
        items.last()->setParam4(heading);                       // yaw (absolute heading in degrees)
    }

    // Explicitly block progression until heading is reached.
    MissionItem* yawItem = new MissionItem(seqNum++,
                                           MAV_CMD_CONDITION_YAW,
                                           MAV_FRAME_MISSION,
                                           heading,                // target heading (deg)
                                           kEntryYawRateDegPerSec, // yaw speed (deg/s)
                                           0.0,                    // shortest direction
                                           0.0,                    // absolute heading
                                           0.0,
                                           0.0,
                                           0.0,
                                           true,
                                           false,
                                           missionItemParent);
    items.append(yawItem);

    // Then add an explicit wait waypoint at entry position before the spray leg starts.
    _appendWaypoint(items, missionItemParent, seqNum, frame, kEntryYawAlignmentHoldSeconds, leg.first().coord);
    if (!items.isEmpty() && items.last()->command() == MAV_CMD_NAV_WAYPOINT) {
        items.last()->setParam4(heading); // hold waypoint yaw (absolute heading in degrees)
    }
}

MAV_CMD SprayComplexItem::_exitWaypointCommand() const
{
    return static_cast<MAV_CMD>(kMavCmdNavSprayWaypoint);
}

void SprayComplexItem::save(QJsonArray& planItems)
{
    // Ask base to build its inner object first
    QJsonArray tmp;
    AgriculturalStyleComplexItem::save(tmp);

            // tmp[0] looks like: { "type":"ComplexItem", "complexItem": { "AgriculturalStyleComplexItem": { ... } } }
    QJsonObject innerComplex;
    if (!tmp.isEmpty() && tmp.first().isObject()) {
        const QJsonObject baseObj = tmp.first().toObject();
        if (baseObj.contains(QStringLiteral("complexItem")) && baseObj.value(QStringLiteral("complexItem")).isObject()) {
            innerComplex = baseObj.value(QStringLiteral("complexItem")).toObject();
        }
    }

            // Wrap with V2 envelope: type + complexItemType + complexItem
    QJsonObject out;
    out[VisualMissionItem::jsonTypeKey]             = VisualMissionItem::jsonTypeComplexItemValue;  // "type": "ComplexItem"
    out[ComplexMissionItem::jsonComplexItemTypeKey] = QString::fromLatin1(jsonComplexItemTypeValue); // "complexItemType": "spray"
    out[QStringLiteral("complexItem")]              = innerComplex;
    out[QString::fromLatin1(_jsonScriptTimeInfoModeKey)] = _scriptTimeInfoMode;
    out[QString::fromLatin1(_jsonEntryYawAlignmentEnabledKey)] = _entryYawAlignmentEnabled;

    planItems.append(out);
}

bool SprayComplexItem::load(const QJsonObject& itemObject, int sequenceNumber, QString& errorString)
{
    // Expecting V2 shape:
    // { "type":"ComplexItem", "complexItemType":"spray", "complexItem": { "AgriculturalStyleComplexItem": { ... } } }
    if (!itemObject.contains(QStringLiteral("complexItem")) || !itemObject.value(QStringLiteral("complexItem")).isObject()) {
        errorString = tr("Spray: missing complexItem object");
        return false;
    }

    _scriptTimeInfoMode = itemObject.value(QString::fromLatin1(_jsonScriptTimeInfoModeKey)).toDouble(0.0);
    _scriptTimeInfoMode = (_scriptTimeInfoMode >= 0.5) ? 1.0 : 0.0;
    _entryYawAlignmentEnabled = itemObject.value(QString::fromLatin1(_jsonEntryYawAlignmentEnabledKey)).toBool(true);

    const QJsonObject inner = itemObject.value(QStringLiteral("complexItem")).toObject();
    return AgriculturalStyleComplexItem::load(inner, sequenceNumber, errorString);
}

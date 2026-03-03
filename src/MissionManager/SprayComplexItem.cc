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

    const QJsonObject inner = itemObject.value(QStringLiteral("complexItem")).toObject();
    return AgriculturalStyleComplexItem::load(inner, sequenceNumber, errorString);
}

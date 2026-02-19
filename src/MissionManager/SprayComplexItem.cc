#include "SprayComplexItem.h"

#include "PlanMasterController.h"
#include "JsonHelper.h"
#include "VisualMissionItem.h"
#include "ComplexMissionItem.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

QGC_LOGGING_CATEGORY(SprayComplexItemLog, "qgc.missionmanager.spray")

const QString SprayComplexItem::name(QStringLiteral("Spray"));

namespace {
constexpr float kScriptMsgPumpPct     = 75.0f;  // hard-coded first pass (ignores UI for now)
constexpr float kScriptMsgNozzlePct   = 75.0f;  // hard-coded first pass (ignores UI for now)
constexpr float kScriptMsgFlowLpm     = 0.0f;   // reserved for future
constexpr float kScriptMsgFlags       = 0.0f;   // reserved for future bitmask
} // namespace

SprayComplexItem::SprayComplexItem(PlanMasterController* masterController, bool flyView)
    : AgriculturalStyleComplexItem(masterController, flyView)
{
    _editorQml = "qrc:/qml/QGroundControl/Controls/SprayItemEditor.qml";
   // Spray-only Facts (rate, droplet) and SCRIPT_TIME injection will be added later.
}

void SprayComplexItem::appendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent)
{
    // Keep base save/load behavior:
    // - If mission items were loaded from plan snapshot, reuse them exactly.
    // - Otherwise build waypoints/scripts from current transects.
    AgriculturalStyleComplexItem::appendMissionItems(items, missionItemParent);
}

MissionItem* SprayComplexItem::_createScriptTimeItem(int sequenceNumber, int action, MAV_FRAME frame,
                                                     QObject* missionItemParent) const
{
    Q_UNUSED(frame);

    if (action != ScriptTimeActionStart && action != ScriptTimeActionStop) {
        return nullptr;
    }

    return new MissionItem(sequenceNumber,
                           MAV_CMD_DO_SEND_SCRIPT_MESSAGE,
                           MAV_FRAME_MISSION,
                           action,                     // param1: script action (20=start, 21=stop)
                           kScriptMsgPumpPct,          // param2: pump percent
                           kScriptMsgNozzlePct,        // param3: nozzle percent
                           kScriptMsgFlowLpm,          // param4: reserved (flow L/min)
                           0.0,                        // param5: EMPTY
                           0.0,                        // param6: EMPTY
                           0.0,                        // param7: EMPTY
                           true,                       // autoContinue
                           false,                      // isCurrentItem
                           missionItemParent);
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
    const QJsonObject inner = itemObject.value(QStringLiteral("complexItem")).toObject();
    return AgriculturalStyleComplexItem::load(inner, sequenceNumber, errorString);
}

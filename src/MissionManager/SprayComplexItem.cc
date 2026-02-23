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
constexpr float kScriptTimeTimeoutSec = 0.0f;  // 0 => no timeout
constexpr float kScriptArgPumpPct     = 75.0f; // param3 hint for script
constexpr float kScriptArgNozzlePct   = 75.0f; // param4 hint for script
constexpr float kScriptArgFlags       = 0.0f;  // param6 reserved
} // namespace

SprayComplexItem::SprayComplexItem(PlanMasterController* masterController, bool flyView)
    : AgriculturalStyleComplexItem(masterController, flyView)
{
    _editorQml = "qrc:/qml/QGroundControl/Controls/SprayItemEditor.qml";
    // Spray-only Facts (rate, droplet) and script-time behavior can be added here.
}

void SprayComplexItem::appendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent)
{
    // Keep base save/load behavior:
    // - If mission items were loaded from plan snapshot, reuse them exactly.
    // - Otherwise build waypoints/script-time commands from current transects.
    AgriculturalStyleComplexItem::appendMissionItems(items, missionItemParent);
}

MissionItem* SprayComplexItem::_createScriptTimeItem(int sequenceNumber, int action, MAV_FRAME frame,
                                                     QObject* missionItemParent) const
{
    Q_UNUSED(frame);

    if (action != ScriptTimeActionStart && action != ScriptTimeActionStop && action != ScriptTimeActionInfo) {
        return nullptr;
    }

    float param1 = static_cast<float>(action);
    float param2 = kScriptTimeTimeoutSec;
    float param3 = 0.0f;
    float param4 = 0.0f;
    float param5 = 0.0f;
    float param6 = 0.0f;

    if (action == ScriptTimeActionStart) {
        // Requested mapping for START marker:
        // param1 = droplet size, param2 = liters/dekar.
        const double dropletSize = pesticideDropletSize()->rawValue().toDouble();
        const double litersPerDekar = pesticideLitersPerDekar()->rawValue().toDouble();
        param1 = static_cast<float>(dropletSize);
        param2 = static_cast<float>(litersPerDekar);

        // Keep start action and hints in later params so script side can still disambiguate.
        param3 = static_cast<float>(ScriptTimeActionStart);
        param4 = kScriptArgPumpPct;
        param5 = kScriptArgNozzlePct;
        param6 = kScriptArgFlags;
    }
    else if (action == ScriptTimeActionInfo) {
        // Info-only action:
        // param2 carries dynamic mission mode set on upload path:
        // 0 = normal start/restart, 1 = resume.
        param1 = static_cast<float>(ScriptTimeActionInfo);
        param2 = static_cast<float>(_scriptTimeInfoMode > 0.5 ? 1.0 : 0.0);
        param3 = 0.0f;
        param4 = 0.0f;
        param5 = 0.0f;
        param6 = 0.0f;
    }

    return new MissionItem(sequenceNumber,
                           MAV_CMD_NAV_SCRIPT_TIME,
                           MAV_FRAME_MISSION,
                           param1,
                           param2,
                           param3,
                           param4,
                           param5,
                           param6,
                           0.0,                     // param7: EMPTY
                           true,                    // autoContinue
                           false,                   // isCurrentItem
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
    _scriptTimeInfoMode = itemObject.value(QString::fromLatin1(jsonScriptTimeInfoModeKey)).toDouble(0.0) > 0.5 ? 1.0 : 0.0;
    qCDebug(SprayComplexItemLog) << "Spray load scriptTimeInfoMode:" << _scriptTimeInfoMode;

    if (!itemObject.contains(QStringLiteral("complexItem")) || !itemObject.value(QStringLiteral("complexItem")).isObject()) {
        errorString = tr("Spray: missing complexItem object");
        return false;
    }
    const QJsonObject inner = itemObject.value(QStringLiteral("complexItem")).toObject();
    return AgriculturalStyleComplexItem::load(inner, sequenceNumber, errorString);
}

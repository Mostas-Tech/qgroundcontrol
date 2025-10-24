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

SprayComplexItem::SprayComplexItem(PlanMasterController* masterController, bool flyView)
    : AgriculturalStyleComplexItem(masterController, flyView)
{
    _editorQml = "qrc:/qml/QGroundControl/Controls/SprayItemEditor.qml";
   // Spray-only Facts (rate, droplet) and SCRIPT_TIME injection will be added later.
}

void SprayComplexItem::appendMissionItems(QList<MissionItem*>& items, QObject* missionItemParent)
{
    // For now: identical to base (waypoints + optional DO_CHANGE_SPEED if Fixed).
    // TODO(SPRAY): Insert MAV_CMD_SCRIPT_TIME at leg start/stop when param map is finalized.
    _buildAndAppendMissionItems(items, missionItemParent);
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

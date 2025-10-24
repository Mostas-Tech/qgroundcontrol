#include "SprayPlanCreator.h"
#include "PlanMasterController.h"
#include "SprayComplexItem.h"
#include "VisualMissionItem.h"
#include "TakeoffMissionItem.h"

SprayPlanCreator::SprayPlanCreator(PlanMasterController* planMasterController, QObject* parent)
                                                                                                 // Reuse Survey icon for now; replace later with a Spray icon
    : PlanCreator(planMasterController, SprayComplexItem::name, QStringLiteral("/qmlimages/PlanCreator/SurveyPlanCreator.png"), parent)
{
}

void SprayPlanCreator::createPlan(const QGeoCoordinate& mapCenterCoord)
{
    _planMasterController->removeAll();

            // Add takeoff at map center
    VisualMissionItem* takeoffVmi = _missionController->insertTakeoffItem(mapCenterCoord, -1);

            // Insert Spray complex item
    _missionController->insertComplexMissionItem(SprayComplexItem::name, mapCenterCoord, -1);

            // Add a simple Land/RTL at the end (let MissionController pick correct type)
    _missionController->insertLandItem(mapCenterCoord, -1);

            // Make takeoff current
    if (auto* toi = qobject_cast<TakeoffMissionItem*>(takeoffVmi)) {
        _missionController->setCurrentPlanViewSeqNum(toi->sequenceNumber(), true);
    }
}

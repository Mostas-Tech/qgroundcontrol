#include "SprayComplexItemTest.h"

#include "MissionItem.h"
#include "SprayComplexItem.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtMath>

namespace {

constexpr int kMavCmdNavSprayWaypoint = 42710;
constexpr float kEntryYawAlignmentHoldSeconds = 5.0f;
constexpr float kEntryYawRateDegPerSec = 30.0f;
constexpr double kHeadingToleranceDeg = 1e-3;
constexpr const char* kJsonEntryYawAlignmentEnabledKey = "entryYawAlignmentEnabled";

double angularDifferenceDeg(double a, double b)
{
    const double rawDiff = qAbs(a - b);
    return qMin(rawDiff, 360.0 - rawDiff);
}

}

SprayComplexItemTest::SprayComplexItemTest()
{
    const double edgeDistance = 100.0;
    _polyVertices.append(QGeoCoordinate(47.633550640000003, -122.08982199));
    _polyVertices.append(_polyVertices[0].atDistanceAndAzimuth(edgeDistance, 90));
    _polyVertices.append(_polyVertices[1].atDistanceAndAzimuth(edgeDistance, 180));
    _polyVertices.append(_polyVertices[2].atDistanceAndAzimuth(edgeDistance, -90.0));
}

void SprayComplexItemTest::init()
{
    OfflineMissionTest::init();
    _sprayItem = new SprayComplexItem(planController(), false /* flyView */);
    _initSprayItem();
}

void SprayComplexItemTest::cleanup()
{
    _sprayItem = nullptr; // Deleted when planController is deleted
    OfflineMissionTest::cleanup();
}

void SprayComplexItemTest::_initSprayItem()
{
    _sprayItem->surveyAreaPolygon()->appendVertices(_polyVertices);
    _sprayItem->lineSpacing()->setRawValue(20.0);
    _sprayItem->confirmSprayParameters();
    QVERIFY(_sprayItem->visualTransectPoints().count() >= 2);
    _sprayItem->setDirty(false);
}

void SprayComplexItemTest::_testItemGenerationUsesEntryConditionYawAndHoldWaypoint()
{
    QList<MissionItem*> items;
    _sprayItem->appendMissionItems(items, this);

    bool foundLeg = false;
    for (int i = 0; i + 3 < items.count(); ++i) {
        if (items[i]->command() != MAV_CMD_NAV_WAYPOINT) {
            continue;
        }

        if (items[i + 1]->command() != MAV_CMD_CONDITION_YAW ||
            items[i + 2]->command() != MAV_CMD_NAV_WAYPOINT ||
            items[i + 3]->command() != static_cast<MAV_CMD>(kMavCmdNavSprayWaypoint)) {
            continue;
        }

        QVERIFY(qAbs(items[i]->param1()) <= 1e-6);
        QVERIFY(qAbs(items[i + 1]->param2() - kEntryYawRateDegPerSec) <= 1e-6);
        QVERIFY(qAbs(items[i + 2]->param1() - kEntryYawAlignmentHoldSeconds) <= 1e-6);
        QVERIFY(items[i + 2]->coordinate() == items[i]->coordinate());
        foundLeg = true;
        i += 3;
    }

    QVERIFY(foundLeg);
}

void SprayComplexItemTest::_testEntryConditionYawAndHoldWaypointHeadingMatchBearing()
{
    QList<MissionItem*> items;
    _sprayItem->appendMissionItems(items, this);

    bool validated = false;
    for (int i = 0; i + 3 < items.count(); ++i) {
        if (items[i]->command() != MAV_CMD_NAV_WAYPOINT ||
            items[i + 1]->command() != MAV_CMD_CONDITION_YAW ||
            items[i + 2]->command() != MAV_CMD_NAV_WAYPOINT ||
            items[i + 3]->command() != static_cast<MAV_CMD>(kMavCmdNavSprayWaypoint)) {
            continue;
        }

        if (qAbs(items[i + 2]->param1() - kEntryYawAlignmentHoldSeconds) > 1e-6) {
            continue;
        }

        const double expectedHeading = items[i]->coordinate().azimuthTo(items[i + 3]->coordinate());
        const double entryHeading = items[i]->param4();
        const double conditionHeading = items[i + 1]->param1();
        const double holdWaypointHeading = items[i + 2]->param4();
        QVERIFY(angularDifferenceDeg(expectedHeading, entryHeading) <= kHeadingToleranceDeg);
        QVERIFY(angularDifferenceDeg(expectedHeading, conditionHeading) <= kHeadingToleranceDeg);
        QVERIFY(angularDifferenceDeg(expectedHeading, holdWaypointHeading) <= kHeadingToleranceDeg);
        validated = true;
        i += 3;
    }

    QVERIFY(validated);
}

void SprayComplexItemTest::_testSequenceNumberAccounting()
{
    QList<MissionItem*> items;
    _sprayItem->appendMissionItems(items, this);
    QCOMPARE(items.count() - 1, _sprayItem->lastSequenceNumber());
}

void SprayComplexItemTest::_testTogglePersistence()
{
    QJsonArray savedItems;
    _sprayItem->save(savedItems);
    QVERIFY(!savedItems.isEmpty());

    QJsonObject savedObject = savedItems.first().toObject();
    savedObject[QString::fromLatin1(kJsonEntryYawAlignmentEnabledKey)] = false;

    SprayComplexItem* loadedItem = new SprayComplexItem(planController(), false /* flyView */);
    QString errorString;
    QVERIFY(loadedItem->load(savedObject, 0 /* sequenceNumber */, errorString));
    QVERIFY(errorString.isEmpty());

    // Force mission rebuild path and clear loaded mission-item snapshot.
    loadedItem->gridAngle()->setRawValue(loadedItem->gridAngle()->rawValue().toDouble() + 1.0);

    QList<MissionItem*> regeneratedItems;
    loadedItem->appendMissionItems(regeneratedItems, this);
    int sprayExitWaypointCount = 0;
    int conditionYawCount = 0;
    int alignmentHoldWaypointCount = 0;
    bool foundSprayExitWaypoint = false;
    for (const MissionItem* item : regeneratedItems) {
        if (item->command() == static_cast<MAV_CMD>(kMavCmdNavSprayWaypoint)) {
            foundSprayExitWaypoint = true;
            ++sprayExitWaypointCount;
        }
        if (item->command() == MAV_CMD_CONDITION_YAW) {
            ++conditionYawCount;
        }
        if (item->command() == MAV_CMD_NAV_WAYPOINT &&
            qAbs(item->param1() - kEntryYawAlignmentHoldSeconds) <= 1e-6) {
            ++alignmentHoldWaypointCount;
        }
    }
    QVERIFY(foundSprayExitWaypoint);
    QCOMPARE(conditionYawCount, 0);
    QCOMPARE(alignmentHoldWaypointCount, 0);
    QVERIFY(sprayExitWaypointCount > 0);

    loadedItem->deleteLater();
}

UT_REGISTER_TEST(SprayComplexItemTest, TestLabel::Unit, TestLabel::MissionManager)

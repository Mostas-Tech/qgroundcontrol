#pragma once

#include <QtPositioning/QGeoCoordinate>

#include "BaseClasses/MissionTest.h"

class SprayComplexItem;

class SprayComplexItemTest : public OfflineMissionTest
{
    Q_OBJECT

public:
    SprayComplexItemTest();

protected:
    void init() final;
    void cleanup() final;

private slots:
    void _testItemGenerationUsesEntryConditionYawAndHoldWaypoint();
    void _testEntryConditionYawAndHoldWaypointHeadingMatchBearing();
    void _testSequenceNumberAccounting();
    void _testTogglePersistence();

private:
    void _initSprayItem();

    SprayComplexItem* _sprayItem = nullptr;
    QList<QGeoCoordinate> _polyVertices;
};

#pragma once

#include "UnitTest.h"

class AdsbTrafficTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _categoryCodesFollowMavlink();
    void _requestsFollowTheServer();
    void _responseIsRead();
    void _trafficIsCarriedForward();
    void _nearestAircraftAreKept();
    void _iconsAreDrawn();
    void _nearestTrafficTakesTurns();
    void _vehicleEchoesOfLocalTrafficAreIgnored();
    void _collisionReportsSetTheThreatLevel();
};

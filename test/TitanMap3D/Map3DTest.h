#pragma once

#include "UnitTest.h"

class Map3DTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _utmRoundTrip();
    void _worldFrameRebasesAndChangesZone();
    void _kalmanConvergesToStep();
    void _kalmanAngleTakesShortWay();
    void _trackerJumpsWhenDisarmed();
    void _trackerSmoothsWhenArmed();
    void _chaseCameraSitsBehindVehicle();
    void _firstPersonStaysAboveGround();
    void _quadtreeRequestsRootsFirst();
    void _meshHasSkirtsBelowEdges();
    void _gridSizeFollowsDemResolution();
    void _cloudCoverageFollowsDensity();
    void _elevationDecodesTerrarium();
    void _smoothedTrailEndsAtVehicle();
    void _modelLoadsWithNoseForward();
};

#pragma once

#include "UnitTest.h"

class LinkStatsTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _ratesAverageWholeSeconds();
    void _lostPacketsFollowSequenceGaps();
    void _qualityCoversTheLastThreeSeconds();
    void _maxPacketInterval();
    void _chartKeepsTheLastMinute();
    void _resetClearsEverything();
    void _pageLoads();
};

#pragma once

#include "UnitTest.h"

class TitanUpdaterTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _commitsAreRead();
    void _stableReleaseIsJudgedByVersion();
    void _developmentReleaseIsJudgedByCommit();
    void _platformAssetIsFound();
};

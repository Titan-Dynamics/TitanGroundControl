#pragma once

#include "UnitTest.h"

class HudPainterTest : public UnitTest
{
    Q_OBJECT

private slots:
    void init() override;
    void _overlayLeavesVideoVisible();
    void _instrumentFillsItsPanel();
    void _paintsWithoutVehicle();
    void _survivesMissingAndExtremeValues();
    void _paintsEveryAttitudeAndSize();
};

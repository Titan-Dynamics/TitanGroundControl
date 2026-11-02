#pragma once

#include "UnitTest.h"

class TelemetryPanelTest : public UnitTest
{
    Q_OBJECT

private slots:
    void init() override;
    void _alertLevelsFollowTheValue();
    void _barFillsBetweenItsEnds();
    void _itemSurvivesSaving();
    void _layoutIsRestored();
    void _catalogListsTheVehicleValues();
    void _batteryReportsUsePerKilometre();
};

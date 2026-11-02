#include "TelemetryPanelTest.h"

#include <cmath>

#include <QtCore/QScopeGuard>

#include "BatteryFactGroupListModel.h"
#include "Fact.h"
#include "MAVLinkLib.h"
#include "MultiVehicleManager.h"
#include "QmlObjectListModel.h"
#include "TelemetryPanelController.h"
#include "TelemetryPanelItem.h"
#include "Vehicle.h"

void TelemetryPanelTest::init()
{
    UnitTest::init();
    MultiVehicleManager::instance()->init();
}

void TelemetryPanelTest::_alertLevelsFollowTheValue()
{
    Vehicle* const vehicle = MultiVehicleManager::instance()->offlineEditingVehicle();
    QVERIFY(vehicle);
    // Heading, as its displayed value is the raw one whatever the unit settings
    Fact* const heading = vehicle->heading();
    const auto restore = qScopeGuard([heading, saved = heading->rawValue()] { heading->setRawValue(saved); });

    TelemetryPanelItem item;
    item.setVehicle(vehicle);
    item.setFact(QStringLiteral("Vehicle"), QStringLiteral("Heading"));
    QCOMPARE(item.fact(), heading);

    item.setAlert(TelemetryPanelItem::AlertAbove);
    item.setCautionValue(100.0);
    item.setWarningValue(200.0);
    heading->setRawValue(50.0);
    QCOMPARE(item.level(), 0);
    heading->setRawValue(150.0);
    QCOMPARE(item.level(), 1);
    heading->setRawValue(250.0);
    QCOMPARE(item.level(), 2);

    item.setAlert(TelemetryPanelItem::AlertBelow);
    QCOMPARE(item.level(), 0);
    heading->setRawValue(80.0);
    QCOMPARE(item.level(), 2);

    // An empty threshold is off
    item.setWarningValue(qQNaN());
    QCOMPARE(item.level(), 1);
    item.setAlert(TelemetryPanelItem::NoAlert);
    QCOMPARE(item.level(), 0);
}

void TelemetryPanelTest::_barFillsBetweenItsEnds()
{
    Vehicle* const vehicle = MultiVehicleManager::instance()->offlineEditingVehicle();
    QVERIFY(vehicle);
    // Heading, as its displayed value is the raw one whatever the unit settings
    Fact* const heading = vehicle->heading();
    const auto restore = qScopeGuard([heading, saved = heading->rawValue()] { heading->setRawValue(saved); });

    TelemetryPanelItem item;
    item.setVehicle(vehicle);
    item.setFact(QStringLiteral("Vehicle"), QStringLiteral("Heading"));
    item.setStyle(TelemetryPanelItem::BarStyle);
    item.setBarMinimum(0.0);
    item.setBarMaximum(200.0);

    heading->setRawValue(50.0);
    QCOMPARE(item.fraction(), 0.25);
    heading->setRawValue(300.0);
    QCOMPARE(item.fraction(), 1.0);
    heading->setRawValue(-10.0);
    QCOMPARE(item.fraction(), 0.0);
}

void TelemetryPanelTest::_itemSurvivesSaving()
{
    Vehicle* const vehicle = MultiVehicleManager::instance()->offlineEditingVehicle();
    QVERIFY(vehicle);

    TelemetryPanelItem item;
    item.setVehicle(vehicle);
    item.setFact(QStringLiteral("Vehicle"), QStringLiteral("GroundSpeed"));
    item.setLabel(QStringLiteral("Speed"));
    item.setShowUnits(false);
    item.setShowPercentage(true);
    item.setStyle(TelemetryPanelItem::BarStyle);
    item.setBarMinimum(-5.0);
    item.setBarMaximum(40.0);
    item.setAlert(TelemetryPanelItem::AlertBelow);
    item.setCautionValue(8.0);

    TelemetryPanelItem copy;
    copy.setVehicle(vehicle);
    copy.fromJson(item.toJson());
    QCOMPARE(copy.fact(), vehicle->groundSpeed());
    QCOMPARE(copy.label(), QStringLiteral("Speed"));
    QCOMPARE(copy.displayLabel(), QStringLiteral("Speed"));
    QCOMPARE(copy.showUnits(), false);
    QCOMPARE(copy.showPercentage(), true);
    QCOMPARE(copy.style(), TelemetryPanelItem::BarStyle);
    QCOMPARE(copy.barMinimum(), -5.0);
    QCOMPARE(copy.barMaximum(), 40.0);
    QCOMPARE(copy.alert(), TelemetryPanelItem::AlertBelow);
    QCOMPARE(copy.cautionValue(), 8.0);
    QVERIFY(std::isnan(copy.warningValue()));
}

void TelemetryPanelTest::_layoutIsRestored()
{
    {
        TelemetryPanelController controller;
        controller.resetToDefaults();
        QVERIFY(controller.items()->count() > 0);

        TelemetryPanelItem* const added = controller.appendItem();
        added->setFact(QStringLiteral("Vehicle"), QStringLiteral("Heading"));
        added->setStyle(TelemetryPanelItem::BarStyle);
        controller.moveItem(controller.items()->count() - 1, 0);
        controller.setColumns(2);
        controller.setTextSize(TelemetryPanelController::LargeText);
        // Saved as the controller goes, before its save delay has run out
    }

    TelemetryPanelController restored;
    QCOMPARE(restored.columns(), 2);
    QCOMPARE(restored.textSize(), TelemetryPanelController::LargeText);
    QVERIFY(restored.hasBars());
    TelemetryPanelItem* const first = restored.items()->value<TelemetryPanelItem*>(0);
    QCOMPARE(first->factName(), QStringLiteral("Heading"));
    QCOMPARE(first->style(), TelemetryPanelItem::BarStyle);

    restored.removeItem(first);
    QVERIFY(restored.items()->value<TelemetryPanelItem*>(0)->factName() != QStringLiteral("Heading"));
    restored.resetToDefaults();
    QCOMPARE(restored.textSize(), TelemetryPanelController::MediumText);
}

void TelemetryPanelTest::_catalogListsTheVehicleValues()
{
    TelemetryPanelController controller;
    const QVariantList catalog = controller.valueCatalog();
    QVERIFY(!catalog.isEmpty());
    QCOMPARE(catalog.constFirst().toMap().value(QStringLiteral("group")).toString(), QStringLiteral("Vehicle"));

    bool foundAltitude = false;
    for (const QVariant& entry : catalog) {
        const QVariantMap value = entry.toMap();
        foundAltitude = foundAltitude || (value.value(QStringLiteral("name"))
                                              .toString()
                                              .compare(QStringLiteral("AltitudeRelative"), Qt::CaseInsensitive) == 0);
    }
    QVERIFY(foundAltitude);
}

void TelemetryPanelTest::_batteryReportsUsePerKilometre()
{
    Vehicle* const vehicle = MultiVehicleManager::instance()->offlineEditingVehicle();
    QVERIFY(vehicle);
    Fact* const groundSpeed = vehicle->groundSpeed();
    const auto restore =
        qScopeGuard([groundSpeed, saved = groundSpeed->rawValue()] { groundSpeed->setRawValue(saved); });

    // 12 V at 10 A is 120 W
    mavlink_battery_status_t status{};
    status.id = 0;
    // MAVLink structs are packed, so their arrays cannot be bound by reference
    for (size_t cell = 0; cell < sizeof(status.voltages) / sizeof(status.voltages[0]); cell++) {
        status.voltages[cell] = UINT16_MAX;
    }
    status.voltages[0] = 12000;
    status.current_battery = 1000;
    status.current_consumed = -1;
    status.battery_remaining = -1;
    mavlink_message_t message{};
    (void) mavlink_msg_battery_status_encode(1, 1, &message, &status);

    BatteryFactGroup battery(0);
    groundSpeed->setRawValue(10.0);  // 36 km/h
    battery.handleMessage(vehicle, message);
    QCOMPARE(qRound(battery.mahPerKm()->rawValue().toDouble() * 10.0), 2778);
    QCOMPARE(qRound(battery.whPerKm()->rawValue().toDouble() * 100.0), 333);

    // Nearly stopped, the figures would be meaningless
    groundSpeed->setRawValue(0.2);
    battery.handleMessage(vehicle, message);
    QVERIFY(std::isnan(battery.mahPerKm()->rawValue().toDouble()));
    QVERIFY(std::isnan(battery.whPerKm()->rawValue().toDouble()));
}

UT_REGISTER_TEST(TelemetryPanelTest, TestLabel::Unit)

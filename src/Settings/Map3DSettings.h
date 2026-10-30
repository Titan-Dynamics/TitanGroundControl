#pragma once

#include <QtQmlIntegration/QtQmlIntegration>

#include "SettingsGroup.h"

/// Settings for the Titan 3D map, matching Mission Planner's Map3D settings and defaults.
class Map3DSettings : public SettingsGroup
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
public:
    Map3DSettings(QObject* parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(cameraDistance)
    DEFINE_SETTINGFACT(cameraHeight)
    DEFINE_SETTINGFACT(cameraAngle)
    DEFINE_SETTINGFACT(fieldOfView)
    DEFINE_SETTINGFACT(vehicleScale)
    DEFINE_SETTINGFACT(vehicleColor)
    DEFINE_SETTINGFACT(stlPath)
    DEFINE_SETTINGFACT(waypointMarkerSize)
    DEFINE_SETTINGFACT(adsbCircleSize)
    DEFINE_SETTINGFACT(cloudDensity)
    DEFINE_SETTINGFACT(showHeadingLine)
    DEFINE_SETTINGFACT(showNavBearingLine)
    DEFINE_SETTINGFACT(showGpsHeadingLine)
    DEFINE_SETTINGFACT(showTurnRadius)
    DEFINE_SETTINGFACT(showTrail)
    DEFINE_SETTINGFACT(fpvMode)
    DEFINE_SETTINGFACT(smoothMovement)
    DEFINE_SETTINGFACT(statsForNerds)
};

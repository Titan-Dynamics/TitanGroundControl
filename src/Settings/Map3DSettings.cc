#include "Map3DSettings.h"

// A fresh settings group: the earlier GeoMap-based 3D map used "Map3D" with other units
DECLARE_SETTINGGROUP(Map3D, "TitanMap3D")
{
}

DECLARE_SETTINGSFACT(Map3DSettings, cameraDistance)
DECLARE_SETTINGSFACT(Map3DSettings, cameraHeight)
DECLARE_SETTINGSFACT(Map3DSettings, cameraAngle)
DECLARE_SETTINGSFACT(Map3DSettings, fieldOfView)
DECLARE_SETTINGSFACT(Map3DSettings, vehicleScale)
DECLARE_SETTINGSFACT(Map3DSettings, vehicleColor)
DECLARE_SETTINGSFACT(Map3DSettings, stlPath)
DECLARE_SETTINGSFACT(Map3DSettings, waypointMarkerSize)
DECLARE_SETTINGSFACT(Map3DSettings, adsbCircleSize)
DECLARE_SETTINGSFACT(Map3DSettings, cloudDensity)
DECLARE_SETTINGSFACT(Map3DSettings, showHeadingLine)
DECLARE_SETTINGSFACT(Map3DSettings, showNavBearingLine)
DECLARE_SETTINGSFACT(Map3DSettings, showGpsHeadingLine)
DECLARE_SETTINGSFACT(Map3DSettings, showTurnRadius)
DECLARE_SETTINGSFACT(Map3DSettings, showTrail)
DECLARE_SETTINGSFACT(Map3DSettings, fpvMode)
DECLARE_SETTINGSFACT(Map3DSettings, smoothMovement)
DECLARE_SETTINGSFACT(Map3DSettings, statsForNerds)

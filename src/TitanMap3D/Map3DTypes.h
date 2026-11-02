#pragma once

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtGui/QColor>

#include "Map3DScene.h"

/// Data passed between the 3D map's QML item (GUI thread) and its renderer (render thread) once per frame.
namespace Map3D {

/// A mission position as Mission Planner's 3D map draws it. Altitudes are relative: drawn above the terrain under
/// each point (home on the ground, the guided target at the guided height).
struct MissionPoint
{
    enum class Kind
    {
        None,
        Home,
        Waypoint,
        Roi,
        Guided
    };

    Kind kind = Kind::None;  ///< None marks a mission item without a position, keeping indices aligned
    double lat = 0;
    double lng = 0;
    double alt = 0;
};

enum class NavMode
{
    Other,
    Auto,
    Return,
    Guided
};

struct AdsbAircraft
{
    quint32 icao = 0;
    QString callsign;
    double lat = 0;
    double lng = 0;
    double alt = 0;      ///< metres AMSL
    double heading = 0;  ///< degrees
    double speed = 0;    ///< m/s
    quint16 squawk = 0;
    bool alert = false;
    double timeSeconds = 0;  ///< when the last report arrived
    QString aircraftType;    ///< ICAO type designator, such as B738
    QString categoryName;
    bool onGround = false;
    int threatLevel = 0;  ///< MAV_COLLISION_THREAT_LEVEL from the vehicle
};

/// Everything the renderer needs for a frame, gathered on the GUI thread.
struct FrameInput
{
    bool connected = false;
    bool armed = false;
    Fix fix;  ///< altitude is relative to home; the renderer adds the terrain under the vehicle
    double roll = 0;
    double pitch = 0;
    double yaw = 0;
    double navBearing = 0;
    double groundCourse = 0;
    double groundSpeed = 0;
    double turnRadius = 0;
    double relativeAltitude = 0;

    bool hasMap2DCenter = false;
    double map2DLat = 0;
    double map2DLng = 0;
    double freeLookYaw = 0;
    double freeLookPitch = 0;

    double cameraDistance = 0.8;
    double cameraHeight = 0.2;
    double cameraAngle = 0;
    double fieldOfView = 60;
    double vehicleScale = 1;
    QColor vehicleColor = Qt::red;
    QString modelPath;  ///< file or resource path of the vehicle STL
    double waypointMarkerSize = 25;
    double adsbCircleSize = 500;
    int cloudDensity = 50;
    bool showHeadingLine = true;
    bool showNavBearingLine = true;
    bool showGpsHeadingLine = true;
    bool showTurnRadius = true;
    bool showTrail = false;
    bool fpvMode = false;
    bool smoothMovement = true;
    bool statsForNerds = false;

    QList<MissionPoint> waypoints;  ///< home then the mission items, None where an item has no position
    NavMode navMode = NavMode::Other;
    int currentWaypoint = 0;        ///< the vehicle's current mission item (sequence number)
    bool hasGuidedTarget = false;
    MissionPoint guidedTarget;
    QList<AdsbAircraft> adsb;
    QString imageryMapType;
    qreal devicePixelRatio = 1;
    QColor skyTop;
    QColor skyHorizon;
};

/// ADS-B traffic where it was drawn last frame, for hover tooltips.
struct AdsbScreenPosition
{
    float x = 0;
    float y = 0;
    float radius = 0;
    quint32 icao = 0;
    double distance = 0;  ///< from our vehicle, metres
};

/// What the renderer reports back after a frame.
struct FrameOutput
{
    bool terrainBusy = true;
    int markersDrawn = 0;
    QList<AdsbScreenPosition> adsb;  ///< nearest last
    QVariantMap stats;
};

}  // namespace Map3D

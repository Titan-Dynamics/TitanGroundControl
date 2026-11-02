#include "AdsbCategory.h"

#include <QtCore/QCoreApplication>

namespace AdsbCategory {

ADSB_EMITTER_TYPE fromCode(QStringView code)
{
    // A0-A7, B0-B7 and C0-C3 follow MAVLink's numbering in order; C4-C7 have no MAVLink type
    if (code.size() != 2) {
        return ADSB_EMITTER_TYPE_NO_INFO;
    }
    const int set = code.at(0).toUpper().unicode() - 'A';
    const int index = code.at(1).unicode() - '0';
    if ((set < 0) || (set > 2) || (index < 0) || (index > 7)) {
        return ADSB_EMITTER_TYPE_NO_INFO;
    }
    const int type = (set * 8) + index;
    return (type <= ADSB_EMITTER_TYPE_POINT_OBSTACLE) ? static_cast<ADSB_EMITTER_TYPE>(type)
                                                      : ADSB_EMITTER_TYPE_NO_INFO;
}

QString name(int emitterType)
{
    switch (emitterType) {
        case ADSB_EMITTER_TYPE_LIGHT:
            return QCoreApplication::translate("AdsbCategory", "Light");
        case ADSB_EMITTER_TYPE_SMALL:
            return QCoreApplication::translate("AdsbCategory", "Small");
        case ADSB_EMITTER_TYPE_LARGE:
            return QCoreApplication::translate("AdsbCategory", "Large");
        case ADSB_EMITTER_TYPE_HIGH_VORTEX_LARGE:
            return QCoreApplication::translate("AdsbCategory", "High Vortex Large");
        case ADSB_EMITTER_TYPE_HEAVY:
            return QCoreApplication::translate("AdsbCategory", "Heavy");
        case ADSB_EMITTER_TYPE_HIGHLY_MANUV:
            return QCoreApplication::translate("AdsbCategory", "Highly Maneuverable");
        case ADSB_EMITTER_TYPE_ROTOCRAFT:
            return QCoreApplication::translate("AdsbCategory", "Rotorcraft");
        case ADSB_EMITTER_TYPE_GLIDER:
            return QCoreApplication::translate("AdsbCategory", "Glider");
        case ADSB_EMITTER_TYPE_LIGHTER_AIR:
            return QCoreApplication::translate("AdsbCategory", "Lighter Than Air");
        case ADSB_EMITTER_TYPE_PARACHUTE:
            return QCoreApplication::translate("AdsbCategory", "Parachute");
        case ADSB_EMITTER_TYPE_ULTRA_LIGHT:
            return QCoreApplication::translate("AdsbCategory", "Ultralight");
        case ADSB_EMITTER_TYPE_UAV:
            return QCoreApplication::translate("AdsbCategory", "UAV");
        case ADSB_EMITTER_TYPE_SPACE:
            return QCoreApplication::translate("AdsbCategory", "Space");
        case ADSB_EMITTER_TYPE_EMERGENCY_SURFACE:
            return QCoreApplication::translate("AdsbCategory", "Emergency Surface");
        case ADSB_EMITTER_TYPE_SERVICE_SURFACE:
            return QCoreApplication::translate("AdsbCategory", "Service Surface");
        case ADSB_EMITTER_TYPE_POINT_OBSTACLE:
            return QCoreApplication::translate("AdsbCategory", "Point Obstacle");
        case ADSB_EMITTER_TYPE_UNASSIGNED:
        case ADSB_EMITTER_TYPE_UNASSIGNED2:
        case ADSB_EMITTER_TYPE_UNASSGINED3:
            return QCoreApplication::translate("AdsbCategory", "Unassigned");
        default:
            return QCoreApplication::translate("AdsbCategory", "No Info");
    }
}

}  // namespace AdsbCategory

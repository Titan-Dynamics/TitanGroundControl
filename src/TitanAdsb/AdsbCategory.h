#pragma once

#include <QtCore/QString>
#include <QtCore/QStringView>

#include "MAVLinkEnums.h"

/// ADS-B emitter categories, as internet traffic feeds code them (DO-260B's A0 to C7) and MAVLink numbers them.
namespace AdsbCategory {

/// The MAVLink emitter type for a feed's category code, such as A3. Codes MAVLink has no type for give NO_INFO.
ADSB_EMITTER_TYPE fromCode(QStringView code);

/// A readable name for a MAVLink emitter type, such as "Large"
QString name(int emitterType);

}  // namespace AdsbCategory

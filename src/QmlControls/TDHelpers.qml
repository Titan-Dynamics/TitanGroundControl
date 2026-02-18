pragma Singleton

import QtQuick

QtObject {
    function displayModeName(name) {
        if (name === "FBW A" || name === "FLY_BY_WIRE_A") return "FBWA"
        if (name === "FBW B" || name === "FLY_BY_WIRE_B") return "FBWB"
        return name
    }
}

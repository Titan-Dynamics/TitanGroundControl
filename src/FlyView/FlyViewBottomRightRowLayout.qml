import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.TitanTelemetry

RowLayout {
    // Titan: the fork's telemetry panel in place of upstream's TelemetryValuesBar
    TelemetryPanel {
        Layout.alignment:   Qt.AlignBottom
        extraWidth:         instrumentPanel.extraValuesWidth
    }

    FlyViewInstrumentPanel {
        id:                 instrumentPanel
        Layout.alignment:   Qt.AlignBottom
        visible:            QGroundControl.corePlugin.options.flyView.showInstrumentPanel && _showSingleVehicleUI
    }
}

import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.TitanMap3D

ToolStripActionList {
    id: _root

    model: [
        Map3DShowAction { },
        Map3DSettingsAction { },
        GuidedActionTakeoff { },
        GuidedActionLand { },
        GuidedActionRTL { },
        GuidedActionPause { },
        FlyViewAdditionalActionsButton { },
        FlyViewGripperButton { }
    ]
}

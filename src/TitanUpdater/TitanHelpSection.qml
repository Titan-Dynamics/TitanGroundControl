import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// The top of the Help settings page: Titan Ground Control's source code and release links, and its updater.
/// Installing waits for the vehicle to be disconnected, since on Windows and Linux the app closes for it.
ColumnLayout {
    id:         root
    spacing:    ScreenTools.defaultFontPixelHeight / 2

    readonly property bool  _vehicleConnected:  !!QGroundControl.multiVehicleManager.activeVehicle
    readonly property int   _state:             TitanUpdater.state
    readonly property bool  _busy:              _state === TitanUpdater.Checking || _state === TitanUpdater.Downloading

    function _link(url) {
        return "<a href=\"" + url + "\">" + url + "</a>"
    }

    function _status() {
        switch (_state) {
        case TitanUpdater.Checking:
            return qsTr("Checking for updates...")
        case TitanUpdater.UpToDate:
            return qsTr("Up to date. The latest is %1.").arg(TitanUpdater.latestVersion)
        case TitanUpdater.Available:
            return qsTr("%1 is available.").arg(TitanUpdater.latestVersion)
        case TitanUpdater.Downloading:
            return qsTr("Downloading %1... %2%").arg(TitanUpdater.latestVersion).arg(Math.round(TitanUpdater.progress * 100))
        case TitanUpdater.ReadyToInstall:
            if (TitanUpdater.closesToInstall) {
                return qsTr("%1 is downloaded. Installing closes %2 and updates it.").arg(TitanUpdater.latestVersion).arg(QGroundControl.appName)
            }
            return Qt.platform.os === "osx"
                    ? qsTr("%1 is downloaded. Install opens it: drag %2 into Applications to replace this one.").arg(TitanUpdater.latestVersion).arg(QGroundControl.appName)
                    : qsTr("%1 is downloaded. Install opens the folder it is in.").arg(TitanUpdater.latestVersion)
        case TitanUpdater.Failed:
            return TitanUpdater.errorString
        default:
            return ""
        }
    }

    function _install() {
        if (mainWindow._hasUnsavedMission()) {
            QGroundControl.showMessageDialog(root, qsTr("Unsaved Mission"),
                                             qsTr("Your mission has changes that are not saved or uploaded. Install the update and lose them?"),
                                             Dialog.Yes | Dialog.No, function() { root._closeAndInstall() })
        } else {
            _closeAndInstall()
        }
    }

    function _closeAndInstall() {
        if (TitanUpdater.install()) {
            mainWindow.finishCloseProcess()
        }
    }

    Component.onCompleted: {
        if (TitanUpdater.state === TitanUpdater.Idle) {
            TitanUpdater.check()
        }
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    QGCLabel {
        text:           QGroundControl.appName
        font.bold:      true
        font.pointSize: ScreenTools.mediumFontPointSize
    }

    GridLayout {
        columns:        2
        columnSpacing:  ScreenTools.defaultFontPixelWidth * 2

        QGCLabel { text: qsTr("Source code") }
        QGCLabel {
            linkColor:          qgcPal.text
            text:               root._link(TitanUpdater.sourceUrl)
            onLinkActivated:    (link) => Qt.openUrlExternally(link)
        }

        QGCLabel { text: qsTr("Releases") }
        QGCLabel {
            linkColor:          qgcPal.text
            text:               root._link(TitanUpdater.releasesUrl)
            onLinkActivated:    (link) => Qt.openUrlExternally(link)
        }

        QGCLabel { text: qsTr("Development build") }
        QGCLabel {
            linkColor:          qgcPal.text
            text:               root._link(TitanUpdater.developmentReleaseUrl)
            onLinkActivated:    (link) => Qt.openUrlExternally(link)
        }

        QGCLabel { text: qsTr("Version") }
        QGCLabel { text: TitanUpdater.currentVersion }

        QGCLabel { text: qsTr("Update from") }
        QGCComboBox {
            model:          [ qsTr("Stable releases"), qsTr("Development builds") ]
            currentIndex:   TitanUpdater.channel
            sizeToContents: true
            enabled:        !root._busy
            onActivated: (index) => {
                TitanUpdater.channel = index
                TitanUpdater.check()
            }
        }
    }

    QGCButton {
        text:       qsTr("Check for Updates")
        primary:    true
        enabled:    !root._busy
        onClicked:  TitanUpdater.check()
    }

    QGCLabel {
        Layout.fillWidth:   true
        Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 70
        text:               root._status()
        wrapMode:           Text.WordWrap
        color:              root._state === TitanUpdater.Failed ? qgcPal.colorRed : qgcPal.text
        visible:            text !== ""
    }

    ProgressBar {
        Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 40
        from:                   0
        to:                     1
        value:                  TitanUpdater.progress
        visible:                root._state === TitanUpdater.Downloading
    }

    RowLayout {
        spacing: ScreenTools.defaultFontPixelWidth

        QGCButton {
            text:       Qt.platform.os === "android" ? qsTr("Download in Browser") : qsTr("Download")
            primary:    true
            visible:    root._state === TitanUpdater.Available
            onClicked:  TitanUpdater.download()
        }

        QGCButton {
            text:       qsTr("Cancel")
            visible:    root._state === TitanUpdater.Downloading
            onClicked:  TitanUpdater.cancel()
        }

        QGCButton {
            text:       TitanUpdater.closesToInstall ? qsTr("Install and Restart") : qsTr("Install")
            primary:    true
            visible:    root._state === TitanUpdater.ReadyToInstall
            enabled:    !root._vehicleConnected
            onClicked:  root._install()
        }

        QGCButton {
            text:       qsTr("Release Notes")
            visible:    TitanUpdater.releasePageUrl !== "" && (root._state === TitanUpdater.Available || root._state === TitanUpdater.UpToDate || root._state === TitanUpdater.ReadyToInstall)
            onClicked:  Qt.openUrlExternally(TitanUpdater.releasePageUrl)
        }
    }

    QGCLabel {
        text:       qsTr("Disconnect the vehicle to install the update.")
        color:      qgcPal.colorOrange
        visible:    root._state === TitanUpdater.ReadyToInstall && root._vehicleConnected
    }
}

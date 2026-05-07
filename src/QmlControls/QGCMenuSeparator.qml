import QtQuick
import QtQuick.Controls

MenuSeparator {
    // MenuSeparator doesn't collapse on !visible so we have to hack it in
    height: visible ? implicitHeight : 0
}

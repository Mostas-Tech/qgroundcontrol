// AgriculturalStyleComplexItemTabBar.qml
import QtQuick
import QtQuick.Controls
import QGroundControl

Item {
    id: root
    property var complexItem
    property var masterController

    signal showEditor()
    signal showTerrain()
    signal showStats()

    Row {
        spacing: 12
        QGCTabButton {
            text: qsTr("Editor")
            onClicked: root.showEditor()
        }
        QGCTabButton {
            text: qsTr("Terrain")
            enabled: false // hidden/disabled for now; wire later
            onClicked: root.showTerrain()
        }
        QGCTabButton {
            text: qsTr("Stats")
            enabled: false // optional; add later
            onClicked: root.showStats()
        }
    }
}

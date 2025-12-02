import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls


Rectangle {
    id:             control
    width:          _rightPanelWidth
    height:         mainLayout.height + (ScreenTools.defaultFontPixelHeight * 0.5)
    color:          qgcPal.window
    radius:         ScreenTools.defaultFontPixelWidth * 0.5
    
    property var    _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property real   _rightPanelWidth: ScreenTools.defaultFontPixelWidth * 30

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    ColumnLayout {
        id:                 mainLayout
        anchors.top:        parent.top
        anchors.left:       parent.left
        anchors.right:      parent.right
        anchors.margins:    ScreenTools.defaultFontPixelWidth * 0.5
        spacing:            ScreenTools.defaultFontPixelHeight * 0.25

        QGCLabel {
            text:           qsTr("Spraying Profile")
            font.bold:      true
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle {
            Layout.fillWidth:   true
            height:             1
            color:              qgcPal.text
            opacity:            0.2
        }

        GridLayout {
            columns: 2
            Layout.fillWidth: true

            QGCLabel {
                text: qsTr("Flow Rate:")
                Layout.fillWidth: true
            }
            QGCLabel {
                text: (_activeVehicle ? _activeVehicle.sprayingFlowRate.toFixed(2) : "0.00") + " L/m"
                Layout.alignment: Qt.AlignRight
                font.bold: true
            }

            QGCLabel {
                text: qsTr("Total Flow:")
                Layout.fillWidth: true
            }
            QGCLabel {
                text: (_activeVehicle ? _activeVehicle.sprayingCumulativeFlow.toFixed(2) : "0.00") + " L"
                Layout.alignment: Qt.AlignRight
                font.bold: true
            }
        }

        QGCButton {
            text:               qsTr("Reset Total")
            Layout.fillWidth:   true
            onClicked: {
                if (_activeVehicle) {
                    _activeVehicle.resetSprayingCumulativeFlow()
                }
            }
        }
    }
}

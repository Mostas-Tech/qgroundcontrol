import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.FlightMap

// Editor for AgriculturalStyleComplexItem (used by Spray/Spreader until mission-specific editors exist)
AgriculturalStyleComplexItemEditor {
    // Required up the chain:
    // property real availableWidth
    // property var  missionItem
    property real _m: ScreenTools.defaultFontPixelWidth

    Component {
        id: _transectValuesComponent

        ColumnLayout {
            spacing: _m * 1.5
            Layout.fillWidth: true

            // ========== GEOMETRY ==========
            QGCLabel {
                text: qsTr("Geometry")
                font.bold: true
                Layout.topMargin: _m
            }

            GridLayout {
                Layout.fillWidth:   true
                columnSpacing:      _m
                rowSpacing:         _m
                columns:            2

                // Angle (deg)
                QGCLabel { text: qsTr("Angle (deg)") }
                FactTextField {
                    fact:                   missionItem.gridAngle
                    Layout.fillWidth:       true
                    onUpdated:              angleSlider.value = missionItem.gridAngle.value
                }
                QGCSlider {
                    id:                     angleSlider
                    from:                   0
                    to:                     359
                    stepSize:               1
                    live:                   true
                    Layout.fillWidth:       true
                    Layout.columnSpan:      2
                    Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                    onValueChanged:         missionItem.gridAngle.value = value
                    Component.onCompleted:  value = missionItem.gridAngle.value
                }

                // Spacing (swath width, m)
                QGCLabel { text: qsTr("Spacing (m)") }
                FactTextField {
                    fact:                   missionItem.lineSpacing
                    Layout.fillWidth:       true
                    onUpdated:              spacingSlider.value = missionItem.lineSpacing.value
                }
                QGCSlider {
                    id:                     spacingSlider
                    from:                   0.1
                    to:                     50.0
                    stepSize:               0.1
                    live:                   true
                    Layout.fillWidth:       true
                    Layout.columnSpan:      2
                    Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                    onValueChanged:         missionItem.lineSpacing.value = value
                    Component.onCompleted:  value = missionItem.lineSpacing.value
                }

                // Turnaround distance (m)
                QGCLabel {
                    text:    qsTr("Turnaround distance (m)")
                    visible: !forPresets
                }
                FactTextField {
                    fact:               missionItem.turnAroundDistance
                    Layout.fillWidth:   true
                    visible:            !forPresets
                    onUpdated:          turnSlider.value = missionItem.turnAroundDistance.value
                }
                QGCSlider {
                    id:                     turnSlider
                    from:                   0.0
                    to:                     200.0
                    stepSize:               0.5
                    live:                   true
                    Layout.fillWidth:       true
                    Layout.columnSpan:      2
                    Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                    visible:                !forPresets
                    onValueChanged:         missionItem.turnAroundDistance.value = value
                    Component.onCompleted:  value = missionItem.turnAroundDistance.value
                }
            }

            // ========== SPEED ==========
            QGCLabel {
                text: qsTr("Speed")
                font.bold: true
                Layout.topMargin: _m
            }

            GridLayout {
                Layout.fillWidth:   true
                columnSpacing:      _m
                rowSpacing:         _m
                columns:            2

                // Speed mode (0 Auto, 1 Fixed)
                QGCLabel { text: qsTr("Manual speed") }
                QGCCheckBox {
                    id:         manualSpeedCheck
                    checked:    missionItem.speedMode.value === 1
                    onClicked:  missionItem.speedMode.value = checked ? 1 : 0
                }

                // Fixed speed value (m/s), enabled only when manual
                QGCLabel {
                    text:    qsTr("Manual speed (m/s)")
                    enabled: manualSpeedCheck.checked
                }
                FactTextField {
                    fact:     missionItem.fixedSpeed
                    enabled:  manualSpeedCheck.checked
                    Layout.fillWidth: true
                    onUpdated: speedValueSlider.value = missionItem.fixedSpeed.value
                }
                QGCSlider {
                    id:                     speedValueSlider
                    from:                   0.0
                    to:                     30.0
                    stepSize:               0.1
                    live:                   true
                    enabled:                manualSpeedCheck.checked
                    Layout.fillWidth:       true
                    Layout.columnSpan:      2
                    Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                    onValueChanged:         missionItem.fixedSpeed.value = value
                    Component.onCompleted:  value = missionItem.fixedSpeed.value
                }
            }

            // ========== PATTERN ==========
            QGCLabel {
                text: qsTr("Pattern")
                font.bold: true
                Layout.topMargin: _m
            }

            GridLayout {
                Layout.fillWidth:   true
                columnSpacing:      _m
                rowSpacing:         _m
                columns:            2

                QGCButton {
                    text: qsTr("Rotate Entry")
                    Layout.fillWidth: true
                    onClicked: missionItem.rotateEntryPoint()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Rotate entry corner clockwise: Top Left → Top Right → Bottom Right → Bottom Left → Top Left.")
                }
            }
        }
    }

    // Optional: polygon loader action (kept functional)
    KMLOrSHPFileDialog {
        id:    kmlOrSHPLoadDialog
        title: qsTr("Select Polygon File")
        onAcceptedForLoad: (file) => {
            missionItem.surveyAreaPolygon.loadKMLOrSHPFile(file)
            close()
        }
    }
}

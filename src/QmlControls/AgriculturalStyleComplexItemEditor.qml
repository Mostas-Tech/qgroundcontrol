import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

// Minimal, camera-free editor for Agricultural/Spray complex items
Rectangle {
    id:         _root
    width:      availableWidth
    color:      qgcPal.windowShadeDark
    radius:     _radius
    height:     contentCol.implicitHeight + _margin * 2

    // Provided by the Loader (Plan view)
    // property real availableWidth
    // property var  missionItem

    readonly property real  _margin:  ScreenTools.defaultFontPixelWidth / 2
    readonly property real  _radius:  ScreenTools.defaultFontPixelWidth / 2
    readonly property real  _m:       ScreenTools.defaultFontPixelWidth

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    ColumnLayout {
        id:                 contentCol
        anchors.fill:       parent
        anchors.margins:    _margin
        spacing:            _m * 1.5

        // Help text shown until polygon is valid
        QGCLabel {
            Layout.fillWidth:       true
            wrapMode:               Text.WordWrap
            horizontalAlignment:    Text.AlignHCenter
            text:                   qsTr("Use the Polygon Tools to create the polygon which outlines your spray area.")
            visible:                !missionItem.surveyAreaPolygon.isValid
        }

        // Everything else only when polygon is valid
        ColumnLayout {
            Layout.fillWidth:   true
            spacing:            _m
            visible:            missionItem.surveyAreaPolygon.isValid

            // ===== Geometry =====
            SectionHeader {
                id:                 geomHeader
                Layout.fillWidth:   true
                text:               qsTr("Geometry")
                checked:            true
            }

            ColumnLayout {
                Layout.fillWidth:   true
                spacing:            _m
                visible:            geomHeader.checked

                GridLayout {
                    // ---- Shared layout tuning for this grid ----
                    readonly property real labelColWidth: ScreenTools.defaultFontPixelWidth * 12
                    Layout.fillWidth:   true
                    columnSpacing:      _m
                    rowSpacing:         _m
                    columns:            2

                    // Angle (deg)
                    QGCLabel {
                        text: qsTr("Angle (deg)")
                        // Fix the label column so the field column gets all extra width
                        Layout.minimumWidth: parent.labelColWidth
                        Layout.maximumWidth: parent.labelColWidth
                        elide: Text.ElideRight
                    }
                    FactTextField {
                        fact:               missionItem.gridAngle
                        Layout.fillWidth:   true
                        onUpdated:          angleSlider.value = missionItem.gridAngle.value
                    }
                    QGCSlider {
                        id:                     angleSlider
                        from:                   0
                        to:                     359
                        stepSize:               1
                        live:                   true
                        tickmarksEnabled:       false
                        Layout.fillWidth:       true
                        Layout.columnSpan:      2
                        Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                        // bind to Fact; initialize without writing back
                        Component.onCompleted:  value = missionItem.gridAngle.value
                        onValueChanged:         missionItem.gridAngle.value = value
                    }

                    // Spacing (m)
                    QGCLabel {
                        text: qsTr("Spacing (m)")
                        Layout.minimumWidth: parent.labelColWidth
                        Layout.maximumWidth: parent.labelColWidth
                        elide: Text.ElideRight
                    }
                    FactTextField {
                        fact:               missionItem.lineSpacing
                        Layout.fillWidth:   true
                    }
                    QGCSlider {
                        id:                     spacingSlider
                        from:                   2          // match metadata min
                        to:                     50.0
                        stepSize:               0.1
                        live:                   false
                        tickmarksEnabled:       false
                        Layout.fillWidth:       true
                        Layout.columnSpan:      2
                        Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                        value:                  missionItem.lineSpacing.value
                        property bool initialized: false
                        Component.onCompleted:  initialized = true
                        onValueChanged: {
                            if (!initialized) return
                            if (pressed || activeFocus) missionItem.lineSpacing.value = value
                        }
                    }

                    // Turnaround distance (m)
                    QGCLabel {
                        text: qsTr("Turnaround distance (m)")
                        Layout.minimumWidth: parent.labelColWidth
                        Layout.maximumWidth: parent.labelColWidth
                        elide: Text.ElideRight
                    }
                    FactTextField {
                        fact:               missionItem.turnAroundDistance
                        Layout.fillWidth:   true
                    }
                    QGCSlider {
                        id:                     turnSlider
                        from:                   0.0
                        to:                     200.0
                        stepSize:               0.5
                        live:                   false
                        tickmarksEnabled:       false
                        Layout.fillWidth:       true
                        Layout.columnSpan:      2
                        Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                        value:                  missionItem.turnAroundDistance.value
                        property bool initialized: false
                        Component.onCompleted:  initialized = true
                        onValueChanged: {
                            if (!initialized) return
                            if (pressed || activeFocus) missionItem.turnAroundDistance.value = value
                        }
                    }

                    QGCButton {
                        text: qsTr("Rotate Entry")
                        Layout.fillWidth: true
                        onClicked: missionItem.rotateEntryPoint()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Rotate entry corner clockwise: Top Left → Top Right → Bottom Right → Bottom Left → Top Left.")
                    }
                    QGCButton {
                        text: qsTr("Calculate Route")
                        Layout.fillWidth: true
                        onClicked: missionItem.rotateEntryPoint()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("")
                    }
                }
            }

            // ===== Speed =====
            SectionHeader {
                id:                 speedHeader
                Layout.fillWidth:   true
                text:               qsTr("Speed")
                checked:            true
            }

            ColumnLayout {
                Layout.fillWidth:   true
                spacing:            _m
                visible:            speedHeader.checked

                GridLayout {
                    readonly property real labelColWidth: ScreenTools.defaultFontPixelWidth * 12
                    Layout.fillWidth:   true
                    columnSpacing:      _m
                    rowSpacing:         _m
                    columns:            2

                    // Speed mode (0 Auto, 1 Fixed)
                    QGCLabel {
                        text: qsTr("Manual speed")
                        Layout.minimumWidth: parent.labelColWidth
                        Layout.maximumWidth: parent.labelColWidth
                        elide: Text.ElideRight
                    }
                    QGCCheckBox {
                        id:         manualSpeedCheck
                        checked:    missionItem.speedMode.value === 1
                        onClicked:  missionItem.speedMode.value = checked ? 1 : 0
                    }

                    // Fixed speed (m/s)
                    QGCLabel {
                        text:    qsTr("Manual speed (m/s)")
                        enabled: manualSpeedCheck.checked
                        Layout.minimumWidth: parent.labelColWidth
                        Layout.maximumWidth: parent.labelColWidth
                        elide: Text.ElideRight
                    }
                    FactTextField {
                        fact:       missionItem.fixedSpeed
                        enabled:    manualSpeedCheck.checked
                        Layout.fillWidth: true
                    }
                    QGCSlider {
                        id:                     speedSlider
                        from:                   0.0
                        to:                     30.0
                        stepSize:               0.1
                        live:                   false
                        tickmarksEnabled:       false
                        enabled:                manualSpeedCheck.checked
                        Layout.fillWidth:       true
                        Layout.columnSpan:      2
                        Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                        value:                  missionItem.fixedSpeed.value
                        property bool initialized: false
                        Component.onCompleted:  initialized = true
                        onValueChanged: {
                            if (!initialized) return
                            if (pressed || activeFocus) missionItem.fixedSpeed.value = value
                        }
                    }
                }
            }

            // ===== Notes / Future =====
            SectionHeader {
                id:                 notesHeader
                Layout.fillWidth:   true
                text:               qsTr("Notes")
                checked:            false
            }
            ColumnLayout {
                Layout.fillWidth:   true
                spacing:            _m
                visible:            notesHeader.checked

                QGCLabel {
                    Layout.fillWidth: true
                    wrapMode:         Text.WordWrap
                    text: qsTr("This mission uses equal-direction legs (no zigzag). Future options will include terrain following and spray start/stop triggers.")
                }
            }
        }
    }
}

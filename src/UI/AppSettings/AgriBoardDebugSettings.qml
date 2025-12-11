/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

SettingsPage {
    id: root

    property var _settingsManager:  QGroundControl.settingsManager
    property var _debugSettings:    _settingsManager.agriBoardDebugSettings
    property var _activeVehicle:    QGroundControl.multiVehicleManager.activeVehicle
    property var _logLines:         []
    property int _pumpOverrideAction: 30
    property int _nozzleOverrideAction: 31

    QGCPalette { id: qgcPal }

    function _log(line) {
        var limit = _debugSettings.logLineLimit.rawValue || 200
        var ts = Qt.formatDateTime(new Date(), "hh:mm:ss.zzz")
        _logLines.push(ts + " " + line)
        while (_logLines.length > limit) {
            _logLines.shift()
        }
        logArea.text = _logLines.join("\n")
    }

    function _targetCompId() {
        if (!_activeVehicle) {
            return 1 // MAV_COMP_ID_AUTOPILOT1 fallback
        }
        var comp = _activeVehicle.defaultComponentId
        if (!comp || comp <= 0) {
            comp = 1 // avoid MAV_COMP_ID_ALL which is rejected
        }
        return comp
    }

    function _sendCommand(cmd, p1, p2, p3, p4, p5, p6, p7) {
        if (!_activeVehicle) {
            _log(qsTr("No active vehicle"))
            return
        }
        var compId = _targetCompId()
        // ShowError=false to avoid GUI popups for SCRIPT_MESSAGE acks that may report UNSUPPORTED even when Lua handles them.
        _activeVehicle.sendCommand(compId, cmd, false,
                                   p1 || 0, p2 || 0, p3 || 0, p4 || 0, p5 || 0, p6 || 0, p7 || 0)
        _log(qsTr("Sent cmd %1 -> comp %2 [%3,%4,%5,%6,%7,%8,%9]").arg(cmd).arg(compId).arg(p1 || 0).arg(p2 || 0).arg(p3 || 0).arg(p4 || 0).arg(p5 || 0).arg(p6 || 0).arg(p7 || 0))
    }

    function _parseFloat(text, fallback) {
        var v = parseFloat(text)
        if (isNaN(v)) {
            return fallback
        }
        return v
    }

    Connections {
        target: _activeVehicle
        function onMavCommandResult(vehicleId, targetComponent, command, ackResult, failureCode) {
            if (!_activeVehicle || vehicleId !== _activeVehicle.id) {
                return
            }
            _log(qsTr("ACK cmd %1 -> result %2 (failure %3)").arg(command).arg(ackResult).arg(failureCode))
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 70
        spacing: ScreenTools.defaultFontPixelHeight

        SettingsGroupLayout {
            Layout.fillWidth: true
            heading: qsTr("AgriBoard bridge (SCRIPT_MESSAGE)")

            QGCLabel {
                text: _activeVehicle ?
                        qsTr("Active vehicle: %1 (sys %2)").arg(_activeVehicle.vehicleTypeString).arg(_activeVehicle.id) :
                        qsTr("No active vehicle")
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label: qsTr("Spray action (param1)")
                fact: _debugSettings.sprayCommandId
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelWidth
                LabelledFactTextField {
                    Layout.fillWidth: true
                    label: qsTr("Script start action")
                    fact: _debugSettings.scriptStartAction
                }
                LabelledFactTextField {
                    Layout.fillWidth: true
                    label: qsTr("Script stop action")
                    fact: _debugSettings.scriptStopAction
                }
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label: qsTr("Log line limit")
                fact: _debugSettings.logLineLimit
            }
        }

        SettingsGroupLayout {
            Layout.fillWidth: true
            enabled: _debugSettings.enabled.rawValue
            heading: qsTr("Spray control (SCRIPT_MESSAGE)")

            RowLayout {
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelWidth
                QGCTextField {
                    id: sprayRateField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Rate L/dekar")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                QGCTextField {
                    id: sprayNozzleField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Nozzle PWM")
                    inputMethodHints: Qt.ImhDigitsOnly
                }
                QGCTextField {
                    id: sprayWidthField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Boom width m (optional)")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
            }

            RowLayout {
                spacing: ScreenTools.defaultFontPixelWidth
                QGCButton {
                    text: qsTr("Send START")
                    onClicked: {
                        var rate = _parseFloat(sprayRateField.text, 0)
                        var noz  = _parseFloat(sprayNozzleField.text, 0)
                        var bw   = _parseFloat(sprayWidthField.text, 0)
                        // param1 carries action id; param2-4 carry spray args for Lua bridge
                        _sendCommand(217, _debugSettings.sprayCommandId.rawValue, rate, noz, bw, 0, 0, 0)
                    }
                }
                QGCButton {
                    text: qsTr("Send STOP")
                    onClicked: _sendCommand(217, _debugSettings.sprayCommandId.rawValue, 0, 0, 0, 0, 0, 0)
                }
            }
        }

        SettingsGroupLayout {
            Layout.fillWidth: true
            enabled: _debugSettings.enabled.rawValue
            heading: qsTr("Calibration / Test (MAV_CMD_DO_SEND_SCRIPT_MESSAGE 217)")

            RowLayout {
                spacing: ScreenTools.defaultFontPixelWidth
                QGCButton {
                    text: qsTr("CAL_START (param1=10)")
                    onClicked: _sendCommand(217, 10, 0, 0, 0, 0, 0, 0)
                }
                QGCButton {
                    text: qsTr("CAL_STOP (param1=11)")
                    onClicked: _sendCommand(217,
                                            11,
                                            _parseFloat(calLitersField.text, 0),
                                            0, 0, 0, 0, 0)
                }
                QGCTextField {
                    id: calLitersField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Liters for CAL_STOP")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
            }

            RowLayout {
                spacing: ScreenTools.defaultFontPixelWidth
                QGCTextField {
                    id: testFlowField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Test flow L/min (param2)")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                QGCButton {
                    text: qsTr("TEST start (param1=12)")
                    onClicked: _sendCommand(217,
                                            12,
                                            _parseFloat(testFlowField.text, 0),
                                            0, 0, 0, 0, 0)
                }
                QGCButton {
                    text: qsTr("TEST stop")
                    onClicked: _sendCommand(217, 12, 0, 0, 0, 0, 0, 0)
                }
            }
        }

        SettingsGroupLayout {
            Layout.fillWidth: true
            enabled: _debugSettings.enabled.rawValue
            heading: qsTr("Mission script bridge (fixed PWM)")

            RowLayout {
                spacing: ScreenTools.defaultFontPixelWidth
                QGCTextField {
                    id: scriptPumpPct
                    Layout.fillWidth: true
                    placeholderText: qsTr("Pump % (param2, optional)")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                QGCTextField {
                    id: scriptNozPct
                    Layout.fillWidth: true
                    placeholderText: qsTr("Nozzle % (param3, optional)")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
            }

            RowLayout {
                spacing: ScreenTools.defaultFontPixelWidth
                QGCButton {
                    text: qsTr("SCRIPT START")
                    onClicked: _sendCommand(217,
                                            _debugSettings.scriptStartAction.rawValue,
                                            _parseFloat(scriptPumpPct.text, 0),
                                            _parseFloat(scriptNozPct.text, 0),
                                            0, 0, 0, 0)
                }
                QGCButton {
                    text: qsTr("SCRIPT STOP")
                    onClicked: _sendCommand(217,
                                            _debugSettings.scriptStopAction.rawValue,
                                            0, 0, 0, 0, 0, 0)
                }
            }
        }

        SettingsGroupLayout {
            Layout.fillWidth: true
            enabled: _debugSettings.enabled.rawValue
            heading: qsTr("Manual overrides (SCRIPT_MESSAGE)")

            RowLayout {
                spacing: ScreenTools.defaultFontPixelWidth
                QGCTextField {
                    id: pumpOverridePct
                    Layout.fillWidth: true
                    placeholderText: qsTr("Pump % (0-100, <=0 auto)")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                QGCButton {
                    text: qsTr("Pump override")
                    onClicked: _sendCommand(217,
                                            _pumpOverrideAction,
                                            _parseFloat(pumpOverridePct.text, 0),
                                            0, 0, 0, 0, 0)
                }
                QGCButton {
                    text: qsTr("Pump AUTO")
                    onClicked: _sendCommand(217, _pumpOverrideAction, 0, 0, 0, 0, 0, 0)
                }
            }

            RowLayout {
                spacing: ScreenTools.defaultFontPixelWidth
                QGCTextField {
                    id: nozOverridePct
                    Layout.fillWidth: true
                    placeholderText: qsTr("Nozzle % (0-100, <=0 auto)")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                QGCButton {
                    text: qsTr("Nozzle override")
                    onClicked: _sendCommand(217,
                                            _nozzleOverrideAction,
                                            _parseFloat(nozOverridePct.text, 0),
                                            0, 0, 0, 0, 0)
                }
                QGCButton {
                    text: qsTr("Nozzle AUTO")
                    onClicked: _sendCommand(217, _nozzleOverrideAction, 0, 0, 0, 0, 0, 0)
                }
            }
        }

        SettingsGroupLayout {
            Layout.fillWidth: true
            heading: qsTr("Log (local)")
            ColumnLayout {
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelWidth
                TextArea {
                    id: logArea
                    Layout.fillWidth: true
                    Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 12
                    readOnly: true
                    text: ""
                    wrapMode: TextArea.NoWrap
                    color: qgcPal.text
                    selectionColor: qgcPal.buttonHighlight
                    background: Rectangle { color: qgcPal.windowShade }
                }
                RowLayout {
                    spacing: ScreenTools.defaultFontPixelWidth
                    QGCButton {
                        text: qsTr("Clear")
                        onClicked: {
                            _logLines = []
                            logArea.text = ""
                        }
                    }
                }
                QGCLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Commands are sent over MAVLink to the ArduCopter Lua bridge. Check vehicle messages for bridge responses (STATUSTEXT/FLOWM_*). Manual pump/nozzle overrides use SCRIPT_MESSAGE param1=30/31 with param2 as percent (<=0 resets to AUTO).")
                }
            }
        }
    }
}

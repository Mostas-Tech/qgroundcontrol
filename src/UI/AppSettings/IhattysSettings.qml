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

import QGroundControl.FactControls
import QGroundControl.Controls

SettingsPage {
    property var    _settingsManager:   QGroundControl.settingsManager
    property var    _ihattysSettings:   _settingsManager.ihattysSettings
    property Fact   _serverEnabled:    _ihattysSettings.ihattysServerEnabled

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Ihattys Server")
        visible:            _ihattysSettings.visible

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              fact.shortDescription
            fact:               _ihattysSettings.ihattysServerHostAddress
            visible:            fact.visible
        }

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              fact.shortDescription
            fact:               _ihattysSettings.ihattysServerPort
            visible:            fact.visible
        }

        QGCLabel {
            Layout.fillWidth:   true
            font.pointSize:     ScreenTools.smallFontPointSize
            text:               qsTr("Changing host or port restarts the server.")
        }

        RowLayout {
            Layout.fillWidth:   true

            QGCLabel {
                Layout.fillWidth:   true
                text:               _serverEnabled.rawValue ? qsTr("Status: Running") : qsTr("Status: Stopped")
            }

            QGCButton {
                text:       _serverEnabled.rawValue ? qsTr("Stop") : qsTr("Start")
                onClicked:  _serverEnabled.rawValue = !_serverEnabled.rawValue
            }
        }
    }
}

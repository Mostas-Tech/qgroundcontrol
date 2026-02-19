import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Item {
    id: root

    property var catalogManager
    property var flyMissionController: null
    property string selectedFieldId: ""

    property bool _fieldEditorActive: false
    property bool _fieldEditorCreateMode: true
    property string _fieldEditorFieldId: ""

    signal flyViewRequested()

    readonly property bool _showingDetail: selectedFieldId !== "" && !root._fieldEditorActive
    readonly property real _defaultMargins: ScreenTools.defaultFontPixelWidth
    readonly property real _thumbSize: ScreenTools.defaultFontPixelWidth * 12

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: true
    }

    function _fieldExists(fieldId) {
        if (!catalogManager || !catalogManager.fields) {
            return false
        }

        for (let i = 0; i < catalogManager.fields.count; i++) {
            const field = catalogManager.fields.get(i)
            if (field && field.id === fieldId) {
                return true
            }
        }

        return false
    }

    function _openCreateFieldEditor() {
        root._fieldEditorCreateMode = true
        root._fieldEditorFieldId = ""
        root._fieldEditorActive = true
    }

    function _openEditFieldEditor(editFieldId) {
        root._fieldEditorCreateMode = false
        root._fieldEditorFieldId = editFieldId
        root._fieldEditorActive = true
    }

    function _thumbnailSource(path) {
        if (!path || path === "") {
            return ""
        }

        if (path.indexOf("://") !== -1) {
            return path
        }

        const normalizedPath = String(path).replace(/\\/g, "/")
        if (/^[A-Za-z]:\//.test(normalizedPath)) {
            return "file:///" + normalizedPath
        }
        if (normalizedPath.startsWith("/")) {
            return "file://" + normalizedPath
        }

        return normalizedPath
    }

    Connections {
        target: catalogManager
        ignoreUnknownSignals: true

        function onFieldsChanged() {
            if (root.selectedFieldId !== "" && !root._fieldExists(root.selectedFieldId)) {
                root.selectedFieldId = ""
            }
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root._fieldEditorActive ? fieldEditorComponent
                                                 : (root._showingDetail ? fieldDetailComponent : fieldsListComponent)
    }

    Component {
        id: fieldsListComponent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: root._defaultMargins
            spacing: root._defaultMargins

            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    Layout.fillWidth: true
                    text: qsTr("Fields")
                    font.pointSize: ScreenTools.largeFontPointSize
                }

                QGCButton {
                    text: qsTr("Fly View")
                    onClicked: root.flyViewRequested()
                }

                QGCButton {
                    text: qsTr("Create Field")
                    onClicked: root._openCreateFieldEditor()
                }
            }

            QGCLabel {
                Layout.fillWidth: true
                visible: catalogManager && catalogManager.lastError !== ""
                text: catalogManager ? catalogManager.lastError : ""
                color: qgcPal.warningText
                wrapMode: Text.WordWrap
            }

            QGCFlickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: fieldsColumn.height

                ColumnLayout {
                    id: fieldsColumn
                    width: parent.width
                    spacing: root._defaultMargins

                    Repeater {
                        id: fieldsRepeater
                        model: catalogManager ? catalogManager.fields : null

                        Rectangle {
                            readonly property var fieldEntry: object

                            Layout.fillWidth: true
                            color: qgcPal.windowShade
                            border.color: qgcPal.buttonHighlight
                            radius: ScreenTools.defaultFontPixelHeight * 0.4
                            implicitHeight: cardContent.implicitHeight + root._defaultMargins * 2

                            RowLayout {
                                id: cardContent
                                anchors.fill: parent
                                anchors.margins: root._defaultMargins
                                spacing: root._defaultMargins

                                Rectangle {
                                    Layout.preferredWidth: root._thumbSize
                                    Layout.preferredHeight: root._thumbSize
                                    color: qgcPal.window
                                    radius: ScreenTools.defaultFontPixelHeight * 0.25

                                    Image {
                                        id: fieldThumbnailImage
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        source: root._thumbnailSource(fieldEntry.thumbnailFilePath)
                                        fillMode: Image.PreserveAspectCrop
                                        visible: source !== ""
                                        asynchronous: true
                                    }

                                    QGCLabel {
                                        anchors.centerIn: parent
                                        visible: !fieldThumbnailImage.visible
                                        text: qsTr("No Image")
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: ScreenTools.defaultFontPixelHeight * 0.25

                                    QGCLabel {
                                        Layout.fillWidth: true
                                        text: fieldEntry.name
                                        elide: Text.ElideRight
                                        font.pointSize: ScreenTools.mediumFontPointSize
                                    }

                                    QGCLabel {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 jobs").arg(fieldEntry.jobCount)
                                        color: qgcPal.text
                                        opacity: 0.8
                                    }
                                }

                                QGCButton {
                                    text: qsTr("Open")
                                    onClicked: root.selectedFieldId = fieldEntry.id
                                }

                                QGCButton {
                                    text: qsTr("Delete")
                                    onClicked: {
                                        QGroundControl.showMessageDialog(
                                            root,
                                            qsTr("Delete Field"),
                                            qsTr("Delete field '%1' and all linked jobs?").arg(fieldEntry.name),
                                            Dialog.Yes | Dialog.No,
                                            function() { catalogManager.deleteField(fieldEntry.id) }
                                        )
                                    }
                                }
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        visible: fieldsRepeater.count === 0
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("No fields available. Create a field to get started.")
                    }
                }
            }
        }
    }

    Component {
        id: fieldDetailComponent

        FieldDetailView {
            anchors.fill: parent
            catalogManager: root.catalogManager
            fieldId: root.selectedFieldId
            flyMissionController: root.flyMissionController
            onBackRequested: root.selectedFieldId = ""
            onEditFieldRequested: (editFieldId) => root._openEditFieldEditor(editFieldId)
        }
    }

    Component {
        id: fieldEditorComponent

        FieldPolygonEditorView {
            anchors.fill: parent
            catalogManager: root.catalogManager
            createMode: root._fieldEditorCreateMode
            fieldId: root._fieldEditorFieldId

            onCanceled: {
                root._fieldEditorActive = false
            }

            onSaved: (savedFieldId) => {
                root._fieldEditorActive = false
                root.selectedFieldId = savedFieldId
            }
        }
    }
}

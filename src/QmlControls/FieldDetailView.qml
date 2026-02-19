import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Item {
    id: root

    property var catalogManager
    property string fieldId
    property var flyMissionController

    signal backRequested()
    signal editFieldRequested(string fieldId)

    readonly property real _defaultMargins: ScreenTools.defaultFontPixelWidth
    readonly property real _fieldThumbHeight: ScreenTools.defaultFontPixelHeight * 14
    readonly property int _resumeMissionIndex: flyMissionController ? flyMissionController.resumeMissionIndex : -1
    readonly property int _missionItemCount: flyMissionController ? flyMissionController.missionItemCount : 0
    readonly property bool _missionIncomplete: _resumeMissionIndex > 0 && (_resumeMissionIndex < _missionItemCount - 2)
    readonly property bool _uploadInProgress: catalogManager ? catalogManager.uploadInProgress : false

    property int _fieldRevisionTrigger: 0
    readonly property var _fieldEntry: {
        _fieldRevisionTrigger
        return _findField(fieldId)
    }
    readonly property var _jobsModel: catalogManager ? catalogManager.jobsForField(fieldId) : null

    property bool _jobEditorActive: false
    property bool _jobEditorCreateMode: true
    property string _editingJobId: ""

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: true
    }

    function _findField(searchFieldId) {
        if (!catalogManager || !catalogManager.fields || searchFieldId === "") {
            return null
        }

        for (let i = 0; i < catalogManager.fields.count; i++) {
            const fieldEntry = catalogManager.fields.get(i)
            if (fieldEntry && fieldEntry.id === searchFieldId) {
                return fieldEntry
            }
        }

        return null
    }

    function _openCreateJobEditor() {
        root._jobEditorCreateMode = true
        root._editingJobId = ""
        root._jobEditorActive = true
    }

    function _openEditJobEditor(jobId) {
        root._jobEditorCreateMode = false
        root._editingJobId = jobId
        root._jobEditorActive = true
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
            root._fieldRevisionTrigger += 1
            if (!root._fieldEntry) {
                root.backRequested()
            }
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root._jobEditorActive ? jobEditorComponent : detailComponent
    }

    Component {
        id: detailComponent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: root._defaultMargins
            spacing: root._defaultMargins

            RowLayout {
                Layout.fillWidth: true

                QGCButton {
                    text: qsTr("Back")
                    onClicked: root.backRequested()
                }

                QGCLabel {
                    Layout.fillWidth: true
                    text: root._fieldEntry ? root._fieldEntry.name : qsTr("Field")
                    font.pointSize: ScreenTools.largeFontPointSize
                    elide: Text.ElideRight
                }
            }

            QGCLabel {
                Layout.fillWidth: true
                visible: catalogManager && catalogManager.lastError !== ""
                text: catalogManager ? catalogManager.lastError : ""
                color: qgcPal.warningText
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root._fieldThumbHeight
                color: qgcPal.windowShade
                border.color: qgcPal.buttonHighlight
                radius: ScreenTools.defaultFontPixelHeight * 0.4

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: root._defaultMargins
                    spacing: root._defaultMargins

                    Rectangle {
                        Layout.preferredWidth: root._fieldThumbHeight - root._defaultMargins * 2
                        Layout.preferredHeight: Layout.preferredWidth
                        color: qgcPal.window
                        radius: ScreenTools.defaultFontPixelHeight * 0.25

                        Image {
                            id: fieldThumbnailImage
                            anchors.fill: parent
                            anchors.margins: 1
                            source: root._fieldEntry ? root._thumbnailSource(root._fieldEntry.thumbnailFilePath) : ""
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
                        spacing: ScreenTools.defaultFontPixelHeight * 0.3

                        QGCLabel {
                            Layout.fillWidth: true
                            text: root._fieldEntry ? root._fieldEntry.name : ""
                            font.pointSize: ScreenTools.mediumFontPointSize
                            elide: Text.ElideRight
                        }

                        QGCLabel {
                            Layout.fillWidth: true
                            text: root._fieldEntry ? qsTr("Revision %1").arg(root._fieldEntry.revision) : ""
                            visible: root._fieldEntry
                        }

                        QGCLabel {
                            Layout.fillWidth: true
                            text: root._fieldEntry ? qsTr("%1 jobs").arg(root._fieldEntry.jobCount) : ""
                            visible: root._fieldEntry
                        }
                    }

                    ColumnLayout {
                        spacing: ScreenTools.defaultFontPixelHeight * 0.3

                        QGCButton {
                            text: qsTr("Edit Field")
                            enabled: !!root._fieldEntry
                            onClicked: root.editFieldRequested(root.fieldId)
                        }

                        QGCButton {
                            text: qsTr("Delete Field")
                            enabled: !!root._fieldEntry
                            onClicked: {
                                if (!root._fieldEntry) {
                                    return
                                }

                                QGroundControl.showMessageDialog(
                                    root,
                                    qsTr("Delete Field"),
                                    qsTr("Delete field '%1' and all linked jobs?").arg(root._fieldEntry.name),
                                    Dialog.Yes | Dialog.No,
                                    function() {
                                        catalogManager.deleteField(root.fieldId)
                                        root.backRequested()
                                    }
                                )
                            }
                        }

                        QGCButton {
                            text: qsTr("Create Job")
                            enabled: !!root._fieldEntry
                            onClicked: root._openCreateJobEditor()
                        }
                    }
                }
            }

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("Jobs")
                font.pointSize: ScreenTools.mediumFontPointSize
            }

            QGCFlickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: jobsColumn.height

                ColumnLayout {
                    id: jobsColumn
                    width: parent.width
                    spacing: root._defaultMargins

                    Repeater {
                        id: jobsRepeater
                        model: root._jobsModel

                        Rectangle {
                            readonly property var jobEntry: object

                            Layout.fillWidth: true
                            color: qgcPal.windowShade
                            border.color: qgcPal.buttonHighlight
                            radius: ScreenTools.defaultFontPixelHeight * 0.35
                            implicitHeight: jobCard.implicitHeight + root._defaultMargins * 2

                            RowLayout {
                                id: jobCard
                                anchors.fill: parent
                                anchors.margins: root._defaultMargins
                                spacing: root._defaultMargins

                                Rectangle {
                                    Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 10
                                    Layout.preferredHeight: Layout.preferredWidth
                                    color: qgcPal.window
                                    radius: ScreenTools.defaultFontPixelHeight * 0.25

                                    Image {
                                        id: jobThumbnailImage
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        source: root._thumbnailSource(jobEntry.thumbnailFilePath)
                                        fillMode: Image.PreserveAspectCrop
                                        visible: source !== ""
                                        asynchronous: true
                                    }

                                    QGCLabel {
                                        anchors.centerIn: parent
                                        visible: !jobThumbnailImage.visible
                                        text: qsTr("No Image")
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: ScreenTools.defaultFontPixelHeight * 0.25

                                    QGCLabel {
                                        Layout.fillWidth: true
                                        text: jobEntry.name
                                        font.pointSize: ScreenTools.mediumFontPointSize
                                        elide: Text.ElideRight
                                    }

                                    QGCLabel {
                                        Layout.fillWidth: true
                                        text: jobEntry.notes === "" ? qsTr("No notes") : jobEntry.notes
                                        wrapMode: Text.WordWrap
                                        elide: Text.ElideRight
                                        maximumLineCount: 2
                                        opacity: 0.85
                                    }

                                    QGCLabel {
                                        Layout.fillWidth: true
                                        visible: jobEntry.needsRegeneration
                                        color: qgcPal.warningText
                                        text: qsTr("Needs regeneration before Start/Resume")
                                    }
                                }

                                ColumnLayout {
                                    spacing: ScreenTools.defaultFontPixelHeight * 0.25

                                    RowLayout {
                                        spacing: ScreenTools.defaultFontPixelWidth * 0.5

                                        QGCButton {
                                            text: qsTr("Edit")
                                            onClicked: root._openEditJobEditor(jobEntry.id)
                                        }

                                        QGCButton {
                                            text: qsTr("Remove")
                                            onClicked: {
                                                QGroundControl.showMessageDialog(
                                                    root,
                                                    qsTr("Remove Job"),
                                                    qsTr("Delete job '%1'?").arg(jobEntry.name),
                                                    Dialog.Yes | Dialog.No,
                                                    function() { catalogManager.deleteJob(jobEntry.id) }
                                                )
                                            }
                                        }
                                    }

                                    RowLayout {
                                        spacing: ScreenTools.defaultFontPixelWidth * 0.5

                                        QGCButton {
                                            text: qsTr("Start")
                                            enabled: !jobEntry.needsRegeneration && !root._uploadInProgress
                                            onClicked: catalogManager.startJob(jobEntry.id)
                                        }

                                        QGCButton {
                                            text: qsTr("Resume")
                                            enabled: !jobEntry.needsRegeneration && root._missionIncomplete && !root._uploadInProgress
                                            onClicked: catalogManager.resumeJob(jobEntry.id, root._missionIncomplete)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        visible: jobsRepeater.count === 0
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("No jobs in this field.")
                    }
                }
            }
        }
    }

    Component {
        id: jobEditorComponent

        JobEditorView {
            anchors.fill: parent
            catalogManager: root.catalogManager
            fieldId: root.fieldId
            createMode: root._jobEditorCreateMode
            jobId: root._editingJobId

            onCanceled: {
                root._jobEditorActive = false
                root._editingJobId = ""
            }

            onSaved: (savedJobId) => {
                root._jobEditorActive = false
                root._editingJobId = savedJobId
            }
        }
    }
}

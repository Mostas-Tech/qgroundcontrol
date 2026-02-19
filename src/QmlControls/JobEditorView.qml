import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightMap

Item {
    id: root

    property var catalogManager
    property string fieldId: ""
    property bool createMode: true
    property string jobId: ""

    signal canceled()
    signal saved(string jobId)

    readonly property real _defaultMargins: ScreenTools.defaultFontPixelWidth
    readonly property bool _hasSprayTransects: !!root._sprayItem
                                               && !!root._sprayItem.visualTransectPoints
                                               && root._sprayItem.visualTransectPoints.length >= 2
    readonly property bool _canSave: !root._saveInProgress
                                      && !!catalogManager
                                      && jobNameField.text.trim() !== ""
                                      && !!root._sprayItem
                                      && root._sprayItem.surveyAreaPolygon.isValid
                                      && root._sprayItem.sprayParametersConfirmed
                                      && root._hasSprayTransects

    readonly property var _missionController: _planMasterController.missionController
    readonly property var _visualItems: _missionController.visualItems
    readonly property var _geoFenceController: _planMasterController.geoFenceController
    readonly property var _jobsModel: catalogManager ? catalogManager.jobsForField(fieldId) : null

    property var _sprayItem: null
    property bool _initialized: false
    property bool _saveInProgress: false
    property string _planFilePath: ""

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: true
    }

    PlanMasterController {
        id: _planMasterController
        flyView: false

        Component.onCompleted: {
            _planMasterController.start()
            Qt.callLater(root._initializeEditor)
        }
    }

    MapFitFunctions {
        id: mapFitFunctions
        map: editorMap
        usePlannedHomePosition: true
        planMasterController: _planMasterController
    }

    function _findJob(searchJobId) {
        if (!_jobsModel || searchJobId === "") {
            return null
        }

        for (let i = 0; i < _jobsModel.count; i++) {
            const jobEntry = _jobsModel.get(i)
            if (jobEntry && jobEntry.id === searchJobId) {
                return jobEntry
            }
        }

        return null
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

    function _isSprayItem(item) {
        return item && !item.isSimpleItem && item.mapVisualQML === "SprayMapVisual.qml"
    }

    function _enforceSingleSprayItem() {
        let sprayCount = 0
        let sprayCandidate = null
        let hasUnsupportedItems = false

        for (let i = 1; i < _visualItems.count; i++) {
            const item = _visualItems.get(i)
            if (_isSprayItem(item)) {
                sprayCount++
                sprayCandidate = item
            } else {
                hasUnsupportedItems = true
            }
        }

        if (sprayCount !== 1 || hasUnsupportedItems) {
            _planMasterController.removeAll()
            const inserted = _missionController.insertComplexMissionItem("Spray", editorMap.center, -1, true)
            sprayCandidate = _isSprayItem(inserted) ? inserted : null

            if (!sprayCandidate) {
                for (let i = 1; i < _visualItems.count; i++) {
                    const item = _visualItems.get(i)
                    if (_isSprayItem(item)) {
                        sprayCandidate = item
                        break
                    }
                }
            }
        }

        root._sprayItem = sprayCandidate
        if (root._sprayItem) {
            _missionController.setCurrentPlanViewSeqNum(root._sprayItem.sequenceNumber, true)
        }
    }

    function _applyFieldPolygonSnapshot() {
        if (!catalogManager || !root._sprayItem) {
            return
        }

        const fieldPolygonSnapshot = catalogManager.activeJobFieldPolygonPath()
        if (!fieldPolygonSnapshot || fieldPolygonSnapshot.length < 3) {
            return
        }

        root._sprayItem.surveyAreaPolygon.beginReset()
        root._sprayItem.surveyAreaPolygon.clear()
        root._sprayItem.surveyAreaPolygon.appendVertices(fieldPolygonSnapshot)
        root._sprayItem.surveyAreaPolygon.endReset()

        if (root._sprayItem.recalcMissionItems) {
            root._sprayItem.recalcMissionItems()
        }
    }

    function _applyFieldExclusionPolygonsSnapshot() {
        if (!catalogManager || !_planMasterController || !_planMasterController.geoFenceController) {
            return
        }

        const jobSnapshotExclusionPolygonPaths = catalogManager.activeJobFieldExclusionPolygonPaths()
        const fieldEntry = _findField(fieldId)
        const fieldExclusionPolygonPaths = fieldEntry ? fieldEntry.exclusionPolygonPaths : []
        const effectiveExclusionPolygonPaths = (jobSnapshotExclusionPolygonPaths && jobSnapshotExclusionPolygonPaths.length > 0)
                ? jobSnapshotExclusionPolygonPaths
                : fieldExclusionPolygonPaths

        if (_planMasterController.geoFenceController.setExclusionPolygons) {
            _planMasterController.geoFenceController.setExclusionPolygons(effectiveExclusionPolygonPaths)
        }

        if (root._sprayItem && root._sprayItem.updatetransect) {
            root._sprayItem.updatetransect()
        } else if (root._sprayItem && root._sprayItem.recalcMissionItems) {
            root._sprayItem.recalcMissionItems()
        }
    }

    function _initializeEditor() {
        if (_initialized || !catalogManager) {
            return
        }

        if (createMode) {
            catalogManager.beginCreateJob(fieldId)
            jobNameField.text = ""
            jobNotesField.text = ""
        } else {
            catalogManager.beginEditJob(jobId)
            const jobEntry = _findJob(jobId)
            jobNameField.text = jobEntry ? jobEntry.name : ""
            jobNotesField.text = jobEntry ? jobEntry.notes : ""
        }

        _planFilePath = catalogManager.activeJobPlanFilePath()

        _planMasterController.removeAll()

        if (_planFilePath !== "" && QGCFileDialogController.fileExists(_planFilePath)) {
            _planMasterController.loadFromFile(_planFilePath)
        }

        _enforceSingleSprayItem()
        _applyFieldPolygonSnapshot()
        _applyFieldExclusionPolygonsSnapshot()

        Qt.callLater(function() {
            mapFitFunctions.fitMapViewportToMissionItems()
        })

        _initialized = true
    }

    function _cancelEditor() {
        if (catalogManager) {
            catalogManager.clearContexts()
        }
        root.canceled()
    }

    function _saveJob() {
        if (!_canSave || !catalogManager) {
            return
        }

        if (_planMasterController.readyForSaveState() !== VisualMissionItem.ReadyForSave) {
            QGroundControl.showMessageDialog(
                root,
                qsTr("Unable to Save Job"),
                qsTr("Complete all required spray inputs before saving this job.")
            )
            return
        }

        if (!root._sprayItem.sprayParametersConfirmed || !root._hasSprayTransects) {
            QGroundControl.showMessageDialog(
                root,
                qsTr("Unable to Save Job"),
                qsTr("Confirm spray parameters to generate mission transects before saving this job.")
            )
            return
        }

        root._saveInProgress = true

        _applyFieldPolygonSnapshot()
        _applyFieldExclusionPolygonsSnapshot()

        if (root._sprayItem) {
            if (root._sprayItem.confirmSprayParameters) {
                root._sprayItem.confirmSprayParameters()
            } else if (root._sprayItem.recalcMissionItems) {
                root._sprayItem.recalcMissionItems()
            }
        }

        if (!root._hasSprayTransects) {
            root._saveInProgress = false
            QGroundControl.showMessageDialog(
                root,
                qsTr("Unable to Save Job"),
                qsTr("Spray transects could not be generated. Adjust job inputs and try again.")
            )
            return
        }

        catalogManager.setActiveJobSprayMetrics(
            root._sprayItem.pesticideLitersPerDekar.value,
            root._sprayItem.pesticideDropletSize.value
        )
        catalogManager.commitActiveJob(jobNameField.text.trim(), jobNotesField.text.trim())

        if (catalogManager.lastError !== "") {
            root._saveInProgress = false
            return
        }

        _planFilePath = catalogManager.activeJobPlanFilePath()
        if (_planFilePath !== "") {
            _planMasterController.saveToFile(_planFilePath)
        }

        const savedJobId = catalogManager.activeJobId
        const finishSave = function() {
            root._saveInProgress = false
            root.saved(savedJobId)
        }

        const thumbnailFilePath = catalogManager.activeJobThumbnailFilePath()
        if (thumbnailFilePath === "") {
            finishSave()
            return
        }

        editorMap.grabToImage(function(result) {
            result.saveToFile(thumbnailFilePath)
            finishSave()
        })
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root._defaultMargins
        spacing: root._defaultMargins

        RowLayout {
            Layout.fillWidth: true

            QGCButton {
                text: qsTr("Back")
                enabled: !root._saveInProgress
                onClicked: root._cancelEditor()
            }

            QGCLabel {
                Layout.fillWidth: true
                text: root.createMode ? qsTr("Create Job") : qsTr("Edit Job")
                font.pointSize: ScreenTools.largeFontPointSize
                elide: Text.ElideRight
            }

            QGCButton {
                text: root._saveInProgress ? qsTr("Saving...") : qsTr("Save Job")
                enabled: root._canSave
                onClicked: root._saveJob()
            }
        }

        QGCLabel {
            Layout.fillWidth: true
            visible: catalogManager && catalogManager.lastError !== ""
            text: catalogManager ? catalogManager.lastError : ""
            color: qgcPal.warningText
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth

            QGCLabel {
                text: qsTr("Job name")
            }

            QGCTextField {
                id: jobNameField
                Layout.fillWidth: true
                placeholderText: qsTr("Required")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth

            QGCLabel {
                text: qsTr("Notes")
            }

            QGCTextField {
                id: jobNotesField
                Layout.fillWidth: true
                placeholderText: qsTr("Optional")
            }
        }

        QGCLabel {
            Layout.fillWidth: true
            text: qsTr("Spray polygon is synchronized from the field boundary. No-go zones are shown in red.")
            color: qgcPal.text
            opacity: 0.85
            wrapMode: Text.WordWrap
        }

        FlightMap {
            id: editorMap

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: ScreenTools.defaultFontPixelHeight * 18

            mapName: "JobEditor"
            allowGCSLocationCenter: true
            allowVehicleLocationCenter: true
            planView: true

            zoomLevel: QGroundControl.flightMapZoom
            center: QGroundControl.flightMapPosition

            property rect centerViewport: Qt.rect(0, 0, width, height)

            onZoomLevelChanged: {
                QGroundControl.flightMapZoom = editorMap.zoomLevel
            }
            onCenterChanged: {
                QGroundControl.flightMapPosition = editorMap.center
            }

            Repeater {
                model: root._visualItems

                delegate: MissionItemMapVisual {
                    map: editorMap
                    interactive: false
                    vehicle: _planMasterController.controllerVehicle
                    onClicked: (sequenceNumber) => {
                        root._missionController.setCurrentPlanViewSeqNum(sequenceNumber, false)
                    }
                }
            }

            Repeater {
                model: root._geoFenceController ? root._geoFenceController.polygons : null

                QGCMapPolygonVisuals {
                    readonly property var fencePolygon: object
                    mapControl: editorMap
                    mapPolygon: fencePolygon
                    interactive: false
                    visible: !!fencePolygon && !fencePolygon.inclusion
                    borderWidth: 1
                    borderColor: "darkred"
                    interiorColor: "crimson"
                    altColor: QGroundControl.globalPalette.surveyPolygonTerrainCollision
                    interiorOpacity: 0.20
                }
            }

            MissionLineView {
                model: root._missionController.simpleFlightPathSegments
            }

            MapItemView {
                model: root._missionController.directionArrows

                delegate: MapLineArrow {
                    fromCoord: object ? object.coordinate1 : undefined
                    toCoord: object ? object.coordinate2 : undefined
                    arrowPosition: 3
                    z: QGroundControl.zOrderWaypointLines + 1
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 22
            color: qgcPal.windowShade
            border.color: qgcPal.buttonHighlight
            radius: ScreenTools.defaultFontPixelHeight * 0.35

            QGCFlickable {
                anchors.fill: parent
                contentHeight: sprayEditorColumn.implicitHeight
                clip: true

                ColumnLayout {
                    id: sprayEditorColumn

                    width: parent.width
                    spacing: ScreenTools.defaultFontPixelHeight * 0.5
                    anchors.margins: ScreenTools.defaultFontPixelWidth

                    Loader {
                        id: sprayEditorLoader
                        Layout.fillWidth: true
                        sourceComponent: root._sprayItem ? sprayEditorComponent : missingSprayComponent
                    }
                }
            }
        }
    }

    Component {
        id: sprayEditorComponent

        SprayItemEditor {
            property real availableWidth: sprayEditorLoader.width
            property var missionItem: root._sprayItem
        }
    }

    Component {
        id: missingSprayComponent

        QGCLabel {
            Layout.fillWidth: true
            color: qgcPal.warningText
            wrapMode: Text.WordWrap
            text: qsTr("Unable to initialize Spray mission item for this job.")
        }
    }
}

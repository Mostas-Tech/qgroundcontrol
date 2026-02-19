import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtPositioning

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightMap

Item {
    id: root

    property var catalogManager
    property bool createMode: true
    property string fieldId: ""

    signal canceled()
    signal saved(string fieldId)

    readonly property real _defaultMargins: ScreenTools.defaultFontPixelWidth
    readonly property bool _canSave: !root._saveInProgress
                                      && !!catalogManager
                                      && fieldNameField.text.trim() !== ""
                                      && polygonController.polygonValid

    property bool _initialized: false
    property bool _saveInProgress: false
    property int _activeExclusionIndex: -1

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: true
    }

    FieldPolygonEditorController {
        id: polygonController
    }

    QtObject {
        id: mapFitFunctions

        function fitMapViewportToMissionItems() {
            root._fitMapToPolygons(root._allPolygonPaths())
        }
    }

    function _allPolygonPaths() {
        const polygonPaths = []
        const fieldPath = polygonController.polygonPath()
        if (fieldPath && fieldPath.length > 0) {
            polygonPaths.push(fieldPath)
        }

        const exclusionPaths = _collectExclusionPolygonPaths(true)
        for (let i = 0; i < exclusionPaths.length; i++) {
            const exclusionPath = exclusionPaths[i]
            if (exclusionPath && exclusionPath.length > 0) {
                polygonPaths.push(exclusionPath)
            }
        }

        return polygonPaths
    }

    function _collectExclusionPolygonPaths(includeIncomplete) {
        const exclusionPaths = []
        if (!polygonController || !polygonController.exclusionPolygonsModel) {
            return exclusionPaths
        }

        for (let i = 0; i < polygonController.exclusionPolygonsModel.count; i++) {
            const polygon = polygonController.exclusionPolygonsModel.get(i)
            if (!polygon || !polygon.path || polygon.path.length === 0) {
                continue
            }

            if (includeIncomplete || polygon.path.length >= 3) {
                exclusionPaths.push(polygon.path)
            }
        }

        return exclusionPaths
    }

    function _coordinateToNumericTuple(coordinateLike) {
        if (!coordinateLike) {
            return null
        }

        if (coordinateLike.latitude !== undefined && coordinateLike.longitude !== undefined) {
            return [Number(coordinateLike.latitude), Number(coordinateLike.longitude)]
        }

        if (coordinateLike.coordinate && coordinateLike.coordinate.latitude !== undefined && coordinateLike.coordinate.longitude !== undefined) {
            return [Number(coordinateLike.coordinate.latitude), Number(coordinateLike.coordinate.longitude)]
        }

        if (coordinateLike.length !== undefined && coordinateLike.length >= 2) {
            const latitude = Number(coordinateLike[0])
            const longitude = Number(coordinateLike[1])
            if (!isNaN(latitude) && !isNaN(longitude)) {
                return [latitude, longitude]
            }
        }

        return null
    }

    function _debugCoordinateTupleToText(tupleLike) {
        if (!tupleLike || tupleLike.length < 2) {
            return "<invalid>"
        }

        return "[" + tupleLike[0] + ", " + tupleLike[1] + "]"
    }

    function _polygonToNumericPath(polygonObject) {
        const numericPath = []
        if (!polygonObject) {
            console.warn("[FieldPolygonEditorView] _polygonToNumericPath polygonObject is null")
            return numericPath
        }

        const path = polygonObject.path
        const pathLen = (path && path.length !== undefined) ? path.length : -1
        const pathModelLen = (polygonObject.pathModel && polygonObject.pathModel.count !== undefined)
                ? polygonObject.pathModel.count : -1
        console.warn("[FieldPolygonEditorView] _polygonToNumericPath begin pathLen=", pathLen, "pathModelLen=", pathModelLen)

        if (path && path.length !== undefined) {
            for (let i = 0; i < path.length; i++) {
                const numericTuple = _coordinateToNumericTuple(path[i])
                if (numericTuple) {
                    numericPath.push(numericTuple)
                } else {
                    console.warn("[FieldPolygonEditorView] _polygonToNumericPath path vertex failed conversion index=", i)
                }
            }
        }

        if (numericPath.length === 0 && polygonObject.pathModel) {
            for (let i = 0; i < polygonObject.pathModel.count; i++) {
                const vertexObject = polygonObject.pathModel.get(i)
                const numericTuple = _coordinateToNumericTuple(vertexObject)
                if (numericTuple) {
                    numericPath.push(numericTuple)
                } else {
                    console.warn("[FieldPolygonEditorView] _polygonToNumericPath pathModel vertex failed conversion index=", i)
                }
            }
        }

        console.warn("[FieldPolygonEditorView] _polygonToNumericPath end numericPathLen=", numericPath.length)
        return numericPath
    }

    function _collectExclusionPolygonPathsForSave() {
        const exclusionPaths = []
        if (!polygonController || !polygonController.exclusionPolygonsModel) {
            console.warn("[FieldPolygonEditorView] _collectExclusionPolygonPathsForSave missing polygonController/exclusionPolygonsModel")
            return exclusionPaths
        }

        console.warn("[FieldPolygonEditorView] _collectExclusionPolygonPathsForSave modelCount=", polygonController.exclusionPolygonsModel.count)
        for (let i = 0; i < polygonController.exclusionPolygonsModel.count; i++) {
            const polygon = polygonController.exclusionPolygonsModel.get(i)
            const numericPath = _polygonToNumericPath(polygon)
            if (numericPath.length >= 3) {
                exclusionPaths.push(numericPath)
                console.warn("[FieldPolygonEditorView] accepted no-go polygon index=", i,
                             "vertexCount=", numericPath.length,
                             "first=", _debugCoordinateTupleToText(numericPath[0]))
            } else {
                console.warn("[FieldPolygonEditorView] dropped no-go polygon index=", i,
                             "numericVertexCount=", numericPath.length)
            }
        }

        console.warn("[FieldPolygonEditorView] _collectExclusionPolygonPathsForSave resultCount=", exclusionPaths.length)
        return exclusionPaths
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

    function _fitMapToPolygons(polygonPaths) {
        if (!polygonPaths || polygonPaths.length === 0) {
            return
        }

        let north = -90
        let south = 90
        let east = -180
        let west = 180
        let validCount = 0

        for (let polygonIndex = 0; polygonIndex < polygonPaths.length; polygonIndex++) {
            const path = polygonPaths[polygonIndex]
            if (!path || path.length === 0) {
                continue
            }

            for (let i = 0; i < path.length; i++) {
                const coordinate = path[i]
                if (!coordinate || !coordinate.isValid) {
                    continue
                }

                north = Math.max(north, coordinate.latitude)
                south = Math.min(south, coordinate.latitude)
                east = Math.max(east, coordinate.longitude)
                west = Math.min(west, coordinate.longitude)
                validCount++
            }
        }

        if (validCount === 0) {
            return
        }

        const latPad = Math.max((north - south) * 0.1, 0.0005)
        const lonPad = Math.max((east - west) * 0.1, 0.0005)

        const topLeft = QtPositioning.coordinate(north + latPad, west - lonPad)
        const bottomRight = QtPositioning.coordinate(south - latPad, east + lonPad)
        editorMap.setVisibleRegion(QtPositioning.rectangle(topLeft, bottomRight))
    }

    function _normalizeActiveExclusionIndex() {
        const exclusionCount = polygonController.exclusionPolygonCount
        if (exclusionCount <= 0) {
            root._activeExclusionIndex = -1
            return
        }

        if (root._activeExclusionIndex < 0 || root._activeExclusionIndex >= exclusionCount) {
            root._activeExclusionIndex = exclusionCount - 1
        }
    }

    function _initializeEditor() {
        if (_initialized || !catalogManager) {
            return
        }

        if (createMode) {
            fieldNameField.text = ""
            polygonController.setPolygonPath([])
            polygonController.setExclusionPolygonPaths([])
            root._activeExclusionIndex = -1
        } else {
            const fieldEntry = _findField(fieldId)
            fieldNameField.text = fieldEntry ? fieldEntry.name : ""
            polygonController.setPolygonPath(fieldEntry ? fieldEntry.polygonPath : [])
            polygonController.setExclusionPolygonPaths(fieldEntry ? fieldEntry.exclusionPolygonPaths : [])
            root._activeExclusionIndex = -1
        }

        _initialized = true
        Qt.callLater(function() {
            root._fitMapToPolygons(root._allPolygonPaths())
        })
    }

    function _cancelEditor() {
        if (catalogManager) {
            catalogManager.clearContexts()
        }
        root.canceled()
    }

    function _saveField() {
        if (!_canSave || !catalogManager) {
            console.warn("[FieldPolygonEditorView] _saveField blocked _canSave=", _canSave, "hasCatalogManager=", !!catalogManager)
            return
        }

        const trimmedName = fieldNameField.text.trim()
        if (trimmedName === "") {
            console.warn("[FieldPolygonEditorView] _saveField blocked empty trimmedName")
            return
        }

        if (createMode) {
            catalogManager.beginCreateFieldDraft()
        } else {
            catalogManager.beginEditField(fieldId)
        }

        const exclusionPolygonPaths = _collectExclusionPolygonPathsForSave()
        console.warn("[FieldPolygonEditorView] commitActiveField payload name=", trimmedName,
                     "mainVertexCount=", polygonController.vertexCount,
                     "controllerExclusionPolygonCount=", polygonController.exclusionPolygonCount,
                     "payloadExclusionCount=", exclusionPolygonPaths.length)
        for (let i = 0; i < exclusionPolygonPaths.length; i++) {
            const path = exclusionPolygonPaths[i]
            console.warn("[FieldPolygonEditorView] payload no-go index=", i,
                         "vertexCount=", path.length,
                         "first=", _debugCoordinateTupleToText(path[0]))
        }

        catalogManager.commitActiveField(trimmedName,
                                         polygonController.polygonPath(),
                                         exclusionPolygonPaths)

        console.warn("[FieldPolygonEditorView] commitActiveField returned lastError=", catalogManager.lastError,
                     "activeFieldId=", catalogManager.activeFieldId)

        if (catalogManager.lastError !== "") {
            return
        }

        const savedFieldId = catalogManager.activeFieldId
        if (savedFieldId === "") {
            return
        }

        root._saveInProgress = true

        const finishSave = function() {
            root._saveInProgress = false
            root.saved(savedFieldId)
        }

        const thumbnailFilePath = catalogManager.activeFieldThumbnailFilePath()
        if (thumbnailFilePath === "") {
            finishSave()
            return
        }

        editorMap.grabToImage(function(result) {
            result.saveToFile(thumbnailFilePath)
            finishSave()
        })
    }

    Component.onCompleted: Qt.callLater(_initializeEditor)

    onCreateModeChanged: {
        _initialized = false
        Qt.callLater(_initializeEditor)
    }

    onFieldIdChanged: {
        _initialized = false
        Qt.callLater(_initializeEditor)
    }

    Connections {
        target: polygonController
        ignoreUnknownSignals: true

        function onExclusionPolygonCountChanged() {
            root._normalizeActiveExclusionIndex()
        }
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
                text: root.createMode ? qsTr("Create Field") : qsTr("Edit Field")
                font.pointSize: ScreenTools.largeFontPointSize
                elide: Text.ElideRight
            }

            QGCButton {
                text: root._saveInProgress ? qsTr("Saving...") : qsTr("Save Field")
                enabled: root._canSave
                onClicked: root._saveField()
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
                text: qsTr("Field name")
            }

            QGCTextField {
                id: fieldNameField
                Layout.fillWidth: true
                placeholderText: qsTr("Required")
            }
        }

        QGCLabel {
            Layout.fillWidth: true
            text: polygonController.polygonValid
                    ? qsTr("Spray area ready (%1 vertices)").arg(polygonController.vertexCount)
                    : qsTr("Draw a spray-area polygon with at least 3 vertices before saving.")
            color: polygonController.polygonValid ? qgcPal.text : qgcPal.warningText
            wrapMode: Text.WordWrap
        }

        QGCLabel {
            Layout.fillWidth: true
            text: polygonController.exclusionPolygonsValid
                    ? qsTr("No-go zones: %1").arg(polygonController.exclusionPolygonCount)
                    : qsTr("Incomplete no-go zones are ignored on save until they have at least 3 vertices.")
            color: polygonController.exclusionPolygonsValid ? qgcPal.text : qgcPal.warningText
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth * 0.5

            QGCButton {
                text: qsTr("Edit Spray Area")
                enabled: !root._saveInProgress
                onClicked: root._activeExclusionIndex = -1
            }

            QGCButton {
                text: qsTr("Add No-Go Zone")
                enabled: !root._saveInProgress
                onClicked: {
                    polygonController.addExclusionPolygon()
                    root._activeExclusionIndex = polygonController.exclusionPolygonCount - 1
                }
            }

            QGCButton {
                text: qsTr("Select Next No-Go")
                enabled: polygonController.exclusionPolygonCount > 0 && !root._saveInProgress
                onClicked: {
                    if (root._activeExclusionIndex < 0) {
                        root._activeExclusionIndex = 0
                    } else {
                        root._activeExclusionIndex = (root._activeExclusionIndex + 1) % polygonController.exclusionPolygonCount
                    }
                }
            }

            QGCButton {
                text: qsTr("Delete Selected No-Go")
                enabled: root._activeExclusionIndex >= 0 && !root._saveInProgress
                onClicked: {
                    polygonController.removeExclusionPolygon(root._activeExclusionIndex)
                    root._normalizeActiveExclusionIndex()
                }
            }
        }

        QGCLabel {
            Layout.fillWidth: true
            text: root._activeExclusionIndex < 0
                    ? qsTr("Editing spray-area polygon.")
                    : qsTr("Editing no-go zone %1 of %2.")
                        .arg(root._activeExclusionIndex + 1)
                        .arg(polygonController.exclusionPolygonCount)
            color: qgcPal.text
            wrapMode: Text.WordWrap
        }

        FlightMap {
            id: editorMap

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: ScreenTools.defaultFontPixelHeight * 24

            mapName: "FieldPolygonEditor"
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

            QGCMapPolygonVisuals {
                mapControl: editorMap
                mapPolygon: polygonController.mapPolygon
                interactive: root._activeExclusionIndex < 0
                borderWidth: 1
                borderColor: "black"
                interiorColor: QGroundControl.globalPalette.surveyPolygonInterior
                altColor: QGroundControl.globalPalette.surveyPolygonTerrainCollision
                interiorOpacity: 0.45
            }

            Repeater {
                model: polygonController.exclusionPolygonsModel

                QGCMapPolygonVisuals {
                    readonly property var exclusionPolygon: object
                    mapControl: editorMap
                    mapPolygon: exclusionPolygon
                    interactive: index === root._activeExclusionIndex
                    borderWidth: 1
                    borderColor: index === root._activeExclusionIndex ? qgcPal.warningText : "darkred"
                    interiorColor: "crimson"
                    altColor: QGroundControl.globalPalette.surveyPolygonTerrainCollision
                    interiorOpacity: index === root._activeExclusionIndex ? 0.35 : 0.20
                }
            }
        }
    }
}

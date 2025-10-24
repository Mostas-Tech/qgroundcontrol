import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl

import QGroundControl.Controls
import QGroundControl.FlightMap

// Map visuals for Agricultural-style items (Spray/Spreader) with QGC polygon tools
Item {
    id: _root

    property var    map                                                 ///< Map control to place item in
    property bool   polygonInteractive: true
    property bool   interactive: true
    property var    vehicle

    // ---- Internals (parity with TransectStyleMapVisuals) ----
    property var    _missionItem:               object
    property var    _mapPolygon:                object.surveyAreaPolygon
    property var    _fieldPolygons:             object.fieldPolygonsMap
    property bool   _currentItem:               object.isCurrentItem
    property var    _transectPoints:            _missionItem.visualTransectPoints
    property var    _fieldtransectPoints:       _missionItem.visualFieldTransectPairs   // [p0,p1,p2,p3,...]
    property int    _transectCount:             _transectPoints.length / (_hasTurnaround ? 4 : 2)
    property bool   _hasTurnaround:             _missionItem.turnAroundDistance.rawValue !== 0
    property int    _firstTrueTransectIndex:    _hasTurnaround ? 1 : 0
    property int    _lastTrueTransectIndex:     _transectPoints.length - (_hasTurnaround ? 2 : 1)
    property int    _lastPointIndex:            _transectPoints.length - 1
    property bool   _showPartialEntryExit:      !_currentItem && _hasTurnaround && _transectPoints.length >= 2
    property var    _fullTransectsComponent:    null
    property var    _entryTransectsComponent:   null
    property var    _exitTransectsComponent:    null
    property var    _entryCoordinate
    property var    _exitCoordinate

    signal clicked(int sequenceNumber)

    function _addVisualElements() {
        if (!map) return
        objMgr.createObjects(
            [ fullTransectsComponent,
            entryTransectComponent, exitTransectComponent,
            entryPointComponent,    exitPointComponent,
            entryArrow1Component,   entryArrow2Component,
            exitArrow1Component,    exitArrow2Component,
            ],
            map,
            true /* parentObjectIsMap */
        )
    }
    function _destroyVisualElements() { objMgr.destroyObjects() }

    Component.onCompleted: {
    _addVisualElements()
    }
    Component.onDestruction: {
    _destroyVisualElements()
    }


    QGCDynamicObjectManager { id: objMgr }

    // ----- POLYGON & TOOLS -----

    // Area polygon
    QGCMapPolygonVisuals {
        id:                 mapPolygonVisuals
        mapControl:         map
        mapPolygon:         _mapPolygon
        interactive:        polygonInteractive && _missionItem.isCurrentItem && _root.interactive
        borderWidth:        1
        borderColor:        "black"
        interiorColor:      QGroundControl.globalPalette.surveyPolygonInterior
        altColor:           QGroundControl.globalPalette.surveyPolygonTerrainCollision
        interiorOpacity:    0.5 * _root.opacity
        
    }
    // Repeater {
    //     id: fieldPolysRepeater
    //     model: _fieldPolygons            // QList<QGCMapPolygon*> from object.fieldPolygonsMap

    //     delegate: QGCMapPolygonVisuals {
    //         mapControl:      map
    //         mapPolygon:      modelData   // each QGCMapPolygon*
    //         interactive:     false
    //         borderWidth:     1
    //         borderColor:     "red"
    //         interiorColor:   "yellow"
    //         altColor:        QGroundControl.globalPalette.surveyPolygonTerrainCollision
    //         interiorOpacity: 0.5 * _root.opacity
    //         visible:         _currentItem
    //     }
    // }

    // Full set of transects lines. Shown when item is selected.
    Component {
        id: fullTransectsComponent

        MapPolyline {
            line.color: "white"
            line.width: 2
            path:       _transectPoints
            visible:    _currentItem
            opacity:    _root.opacity
        }
    }



    Component {
        id: entryTransectComponent

        MapPolyline {
            line.color: "white"
            line.width: 2
            path:       _showPartialEntryExit ? [ _transectPoints[0], _transectPoints[1] ] : []
            visible:    _showPartialEntryExit
            opacity:    _root.opacity
        }
    }

    Component {
        id: exitTransectComponent

        MapPolyline {
            line.color: "white"
            line.width: 2
            path:       _showPartialEntryExit ? [ _transectPoints[_lastPointIndex - 1], _transectPoints[_lastPointIndex] ] : []
            visible:    _showPartialEntryExit
            opacity:    _root.opacity
        }
    }

    // ----- ENTRY POINT -----
    Component {
        id: entryPointComponent

        MapQuickItem {
            anchorPoint.x:  sourceItem.anchorPointX
            anchorPoint.y:  sourceItem.anchorPointY
            z:              QGroundControl.zOrderMapItems
            coordinate:     _missionItem.coordinate
            visible:        _missionItem.exitCoordinate.isValid
            opacity:        _root.opacity

            sourceItem: MissionItemIndexLabel {
                index:      _missionItem.sequenceNumber
                checked:    _missionItem.isCurrentItem
                onClicked:  if (_root.interactive) _root.clicked(_missionItem.sequenceNumber)
            }
        }
    }

    // ----- ENTRY ARROWS -----
    Component {
        id: entryArrow1Component

        MapLineArrow {
            fromCoord:      _transectPoints[_firstTrueTransectIndex]
            toCoord:        _transectPoints[_firstTrueTransectIndex + 1]
            arrowPosition:  1
            visible:        _currentItem
            opacity:        _root.opacity
        }
    }

    Component {
        id: entryArrow2Component

        MapLineArrow {
            fromCoord:      _transectPoints[nextTrueTransectIndex]
            toCoord:        _transectPoints[nextTrueTransectIndex + 1]
            arrowPosition:  1
            visible:        _currentItem && _transectCount > 3
            opacity:        _root.opacity

            property int nextTrueTransectIndex: _firstTrueTransectIndex + (_hasTurnaround ? 4 : 2)
        }
    }

    // ----- EXIT ARROWS -----
    Component {
        id: exitArrow1Component

        MapLineArrow {
            fromCoord:      _transectPoints[_lastTrueTransectIndex - 1]
            toCoord:        _transectPoints[_lastTrueTransectIndex]
            arrowPosition:  3
            visible:        _currentItem
            opacity:        _root.opacity
        }
    }

    Component {
        id: exitArrow2Component

        MapLineArrow {
            fromCoord:      _transectPoints[prevTrueTransectIndex - 1]
            toCoord:        _transectPoints[prevTrueTransectIndex]
            arrowPosition:  13
            visible:        _currentItem && _transectCount > 3
            opacity:        _root.opacity

            property int prevTrueTransectIndex: _lastTrueTransectIndex - (_hasTurnaround ? 4 : 2)
        }
    }

    // Exit point
    Component {
        id: exitPointComponent

        MapQuickItem {
            anchorPoint.x:  sourceItem.anchorPointX
            anchorPoint.y:  sourceItem.anchorPointY
            z:              QGroundControl.zOrderMapItems
            coordinate:     _missionItem.exitCoordinate
            visible:        _missionItem.exitCoordinate.isValid
            opacity:        _root.opacity

            sourceItem: MissionItemIndexLabel {
                index:      _missionItem.lastSequenceNumber
                checked:    _missionItem.isCurrentItem
                onClicked:  if (_root.interactive) _root.clicked(_missionItem.sequenceNumber)
            }
        }
    }
}

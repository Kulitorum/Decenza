import QtQuick
import QtQuick3D
import DecenzaDE1

Item {
    id: root

    property real speed: ScreensaverManager.pipesSpeed
    property real cameraRotationDuration: ScreensaverManager.pipesCameraSpeed * 1000
    property bool running: true

    // Pipe colors - classic screensaver palette
    readonly property var pipeColors: [
        "#C0C0C0",  // Silver
        "#FFD700",  // Gold
        "#CD7F32",  // Bronze
        "#4169E1",  // Royal Blue
        "#DC143C",  // Crimson
        "#228B22",  // Forest Green
        "#9932CC",  // Dark Orchid
        "#00CED1"   // Dark Turquoise
    ]

    // Scene bounds and voxel grid
    readonly property real sceneSize: 400
    readonly property real pipeRadius: 8
    readonly property int gridSize: 20
    readonly property real voxelSize: sceneSize / gridSize
    readonly property real segmentLength: voxelSize

    // Current pipe being built
    property var currentPipe: null
    property int currentColorIndex: 0

    // Voxel occupancy map
    property var voxelMap: ({})

    // Instance entries stored in lists
    property var cylinderEntries: []
    property var sphereEntries: []

    // Tip sphere position (animated separately)
    property vector3d tipPosition: Qt.vector3d(0, 0, 0)
    property color tipColor: "white"
    property bool showTip: false

    Component.onCompleted: {
        initializePipes()
    }

    // Recovery: if running/visible changes after Component.onCompleted,
    // ensure pipe generation starts when conditions are met
    onRunningChanged: {
        if (running && visible && !currentPipe && cylinderEntries.length === 0)
            initializePipes()
    }
    onVisibleChanged: {
        if (running && visible && !currentPipe && cylinderEntries.length === 0)
            initializePipes()
    }

    function initializePipes() {
        // Clear instance lists first, then destroy entries
        cylinderInstanceList.instances = []
        sphereInstanceList.instances = []

        // Destroy the entry objects
        for (var i = 0; i < cylinderEntries.length; i++) {
            if (cylinderEntries[i]) cylinderEntries[i].destroy()
        }
        for (var j = 0; j < sphereEntries.length; j++) {
            if (sphereEntries[j]) sphereEntries[j].destroy()
        }

        cylinderEntries = []
        sphereEntries = []
        voxelMap = {}
        currentColorIndex = 0
        currentPipe = null
        showTip = false

        if (root.running && root.visible) {
            startNewPipe()
        }
    }

    function voxelToWorld(vx, vy, vz) {
        var half = gridSize / 2
        return Qt.vector3d(
            (vx - half) * voxelSize,
            (vy - half) * voxelSize,
            (vz - half) * voxelSize
        )
    }

    function voxelKey(vx, vy, vz) {
        return vx + "," + vy + "," + vz
    }

    function isVoxelBlocked(vx, vy, vz) {
        if (vx < 0 || vx >= gridSize || vy < 0 || vy >= gridSize || vz < 0 || vz >= gridSize) {
            return true
        }
        return voxelMap[voxelKey(vx, vy, vz)] === true
    }

    function occupyVoxel(vx, vy, vz) {
        voxelMap[voxelKey(vx, vy, vz)] = true
    }

    function startNewPipe() {
        if (!root.running || !root.visible) return

        var attempts = 0
        var maxAttempts = 200
        var vx, vy, vz, dir

        while (attempts < maxAttempts) {
            vx = Math.floor(Math.random() * gridSize)
            vy = Math.floor(Math.random() * gridSize)
            vz = Math.floor(Math.random() * gridSize)

            if (!isVoxelBlocked(vx, vy, vz)) {
                dir = findValidDirection(vx, vy, vz, null)
                if (dir) break
                occupyVoxel(vx, vy, vz)
            }
            attempts++
        }

        if (attempts >= maxAttempts || !dir) {
            initializePipes()
            return
        }

        var startPos = voxelToWorld(vx, vy, vz)
        var color = pipeColors[currentColorIndex % pipeColors.length]

        currentPipe = {
            pos: startPos,
            voxel: { x: vx, y: vy, z: vz },
            dir: dir,
            lastDir: null,
            colorIndex: currentColorIndex,
            segmentCount: 0
        }

        occupyVoxel(vx, vy, vz)

        // Add start sphere
        addSphereInstance(startPos, color, pipeRadius * 1.5)

        // Show tip
        tipPosition = startPos
        tipColor = Qt.lighter(color, 1.3)
        showTip = true

        currentColorIndex++
    }

    function findValidDirection(vx, vy, vz, lastDir) {
        var directions = [
            Qt.vector3d(1, 0, 0), Qt.vector3d(-1, 0, 0),
            Qt.vector3d(0, 1, 0), Qt.vector3d(0, -1, 0),
            Qt.vector3d(0, 0, 1), Qt.vector3d(0, 0, -1)
        ]

        var valid = []
        for (var i = 0; i < directions.length; i++) {
            var d = directions[i]
            var nx = vx + d.x
            var ny = vy + d.y
            var nz = vz + d.z

            if (lastDir && d.x === -lastDir.x && d.y === -lastDir.y && d.z === -lastDir.z) {
                continue
            }

            if (!isVoxelBlocked(nx, ny, nz)) {
                valid.push(d)
            }
        }

        if (valid.length === 0) return null
        return valid[Math.floor(Math.random() * valid.length)]
    }

    function directionToRotation(dir) {
        if (Math.abs(dir.y) > 0.5) {
            return dir.y > 0 ? Qt.vector3d(180, 0, 0) : Qt.vector3d(0, 0, 0)
        } else if (Math.abs(dir.x) > 0.5) {
            return dir.x > 0 ? Qt.vector3d(0, 0, -90) : Qt.vector3d(0, 0, 90)
        } else {
            return dir.z > 0 ? Qt.vector3d(90, 0, 0) : Qt.vector3d(-90, 0, 0)
        }
    }

    function addCylinderInstance(position, rotation, color) {
        var entry = cylinderEntryComponent.createObject(null, {
            "position": position,
            "eulerRotation": rotation,
            "color": color
        })
        cylinderInstanceList.instances.push(entry)
        cylinderEntries.push(entry)
    }

    function addSphereInstance(position, color, radius) {
        var sc = radius
        var entry = sphereEntryComponent.createObject(null, {
            "position": position,
            "scale": Qt.vector3d(sc, sc, sc),
            "color": color
        })
        sphereInstanceList.instances.push(entry)
        sphereEntries.push(entry)
    }

    // Components for creating instance entries
    Component {
        id: cylinderEntryComponent
        InstanceListEntry {}
    }

    Component {
        id: sphereEntryComponent
        InstanceListEntry {}
    }

    function addPipeSegment() {
        if (!currentPipe || !root.running || !root.visible) return

        // 0.1% chance to clear everything
        if (Math.random() < 0.001) {
            initializePipes()
            return
        }

        var pipe = currentPipe
        var color = pipeColors[pipe.colorIndex % pipeColors.length]
        var vx = pipe.voxel.x
        var vy = pipe.voxel.y
        var vz = pipe.voxel.z

        var nx = vx + pipe.dir.x
        var ny = vy + pipe.dir.y
        var nz = vz + pipe.dir.z

        var canContinue = !isVoxelBlocked(nx, ny, nz)

        if (!canContinue) {
            var newDir = findValidDirection(vx, vy, vz, pipe.lastDir)
            if (!newDir) {
                finishCurrentPipe()
                return
            }

            // Add ball joint
            addSphereInstance(pipe.pos, color, pipeRadius)

            pipe.lastDir = pipe.dir
            pipe.dir = newDir

            nx = vx + pipe.dir.x
            ny = vy + pipe.dir.y
            nz = vz + pipe.dir.z

            if (isVoxelBlocked(nx, ny, nz)) {
                finishCurrentPipe()
                return
            }
        } else {
            // 30% chance to turn anyway
            if (Math.random() < 0.3 && pipe.segmentCount > 0) {
                var turnDir = findValidPerpendicularDirection(vx, vy, vz, pipe.dir, pipe.lastDir)
                if (turnDir) {
                    addSphereInstance(pipe.pos, color, pipeRadius)

                    pipe.lastDir = pipe.dir
                    pipe.dir = turnDir

                    nx = vx + pipe.dir.x
                    ny = vy + pipe.dir.y
                    nz = vz + pipe.dir.z
                }
            }
        }

        // Add cylinder segment
        var startPos = pipe.pos
        var endPos = voxelToWorld(nx, ny, nz)

        var midPoint = Qt.vector3d(
            (startPos.x + endPos.x) / 2,
            (startPos.y + endPos.y) / 2,
            (startPos.z + endPos.z) / 2
        )

        addCylinderInstance(midPoint, directionToRotation(pipe.dir), color)

        pipe.pos = endPos
        pipe.voxel = { x: nx, y: ny, z: nz }
        pipe.segmentCount++

        occupyVoxel(nx, ny, nz)

        // Update tip
        tipPosition = pipe.pos

        // 2% chance to end pipe
        if (Math.random() < 0.02 && pipe.segmentCount > 5) {
            finishCurrentPipe()
        }
    }

    function findValidPerpendicularDirection(vx, vy, vz, currentDir, lastDir) {
        var perpDirs = []

        if (Math.abs(currentDir.x) > 0.5) {
            perpDirs = [Qt.vector3d(0, 1, 0), Qt.vector3d(0, -1, 0),
                        Qt.vector3d(0, 0, 1), Qt.vector3d(0, 0, -1)]
        } else if (Math.abs(currentDir.y) > 0.5) {
            perpDirs = [Qt.vector3d(1, 0, 0), Qt.vector3d(-1, 0, 0),
                        Qt.vector3d(0, 0, 1), Qt.vector3d(0, 0, -1)]
        } else {
            perpDirs = [Qt.vector3d(1, 0, 0), Qt.vector3d(-1, 0, 0),
                        Qt.vector3d(0, 1, 0), Qt.vector3d(0, -1, 0)]
        }

        var valid = []
        for (var i = 0; i < perpDirs.length; i++) {
            var d = perpDirs[i]
            var nx = vx + d.x
            var ny = vy + d.y
            var nz = vz + d.z

            if (lastDir && d.x === -lastDir.x && d.y === -lastDir.y && d.z === -lastDir.z) {
                continue
            }

            if (!isVoxelBlocked(nx, ny, nz)) {
                valid.push(d)
            }
        }

        if (valid.length === 0) return null
        return valid[Math.floor(Math.random() * valid.length)]
    }

    function finishCurrentPipe() {
        if (!currentPipe || !root.running || !root.visible) return

        var color = pipeColors[currentPipe.colorIndex % pipeColors.length]

        // Add end sphere
        addSphereInstance(currentPipe.pos, Qt.darker(color, 1.3), pipeRadius * 1.4)

        showTip = false
        startNewPipe()
    }

    Timer {
        id: growTimer
        interval: 60 / speed
        running: root.running && root.visible
        repeat: true
        onTriggered: addPipeSegment()
    }

    property real cameraAngle: 0
    NumberAnimation on cameraAngle {
        from: 0
        to: 360
        duration: cameraRotationDuration
        loops: Animation.Infinite
        running: root.running && root.visible
    }

    View3D {
        id: view3d
        anchors.fill: parent
        camera: camera
        environment: sceneEnvironment

        SceneEnvironment {
            id: sceneEnvironment
            clearColor: "#000000"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            id: cameraOrbit
            eulerRotation.y: cameraAngle

            PerspectiveCamera {
                id: camera
                position: Qt.vector3d(0, 50, 450)
                eulerRotation.x: -5
                clipNear: 10
                clipFar: 2000
                fieldOfView: 60
            }

            DirectionalLight {
                eulerRotation.x: -20
                eulerRotation.y: 10
                brightness: 1.0
                ambientColor: Qt.rgba(0.3, 0.3, 0.3, 1.0)
                color: "white"
            }
        }

        Node {
            id: sceneRoot

            // Cylinders using InstanceList
            Model {
                geometry: PipeCylinderGeometry {
                    radius: pipeRadius
                    length: segmentLength
                    sides: 16
                }
                instancing: InstanceList {
                    id: cylinderInstanceList
                }
                materials: DefaultMaterial {
                    diffuseColor: "white"
                    specularAmount: 0.3
                    specularRoughness: 0.5
                }
            }

            // Spheres using InstanceList with custom geometry matching pipe resolution
            Model {
                geometry: PipeSphereGeometry {
                    radius: 1  // Unit sphere, scaled by instances
                    sides: 16
                }
                instancing: InstanceList {
                    id: sphereInstanceList
                }
                materials: DefaultMaterial {
                    diffuseColor: "white"
                    specularAmount: 0.4
                    specularRoughness: 0.4
                }
            }

            // Animated tip sphere
            Model {
                visible: showTip
                geometry: PipeSphereGeometry {
                    radius: pipeRadius * 1.3
                    sides: 16
                }
                position: tipPosition
                materials: DefaultMaterial {
                    diffuseColor: tipColor
                    specularAmount: 0.5
                    specularRoughness: 0.3
                }
            }
        }
    }

    function reset() {
        initializePipes()
    }
}

import QtQuick
import QtQuick3D
import Decenza

Item {
    id: globeRoot

    // Properties passed from ShotMapScreensaver
    property var shots: []
    property string mapTexture: "dark"
    property string textureSource: ""
    property real globeRotation: 0
    property real globeRadius: 150
    property bool testMode: false
    property real testLatitude: 0
    property real testLongitude: 0
    property bool widgetMode: false

    // Convert lat/lon to 3D sphere coordinates
    function latLonTo3D(lat, lon, radius) {
        var adjustedLat = lat - 22
        var latRad = adjustedLat * Math.PI / 180
        var lonRad = lon * Math.PI / 180
        var x = radius * Math.cos(latRad) * Math.sin(lonRad)
        var y = radius * Math.sin(latRad)
        var z = radius * Math.cos(latRad) * Math.cos(lonRad)
        return { x: x, y: y, z: z }
    }

    function getOpacityFromAge(ageHours) {
        return Math.max(0.2, 1 - (ageHours / 24))
    }

    View3D {
        id: view3d
        anchors.fill: parent
        camera: globeCamera
        environment: globeEnvironment

        SceneEnvironment {
            id: globeEnvironment
            clearColor: "#0a0a12"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        PerspectiveCamera {
            id: globeCamera
            position: Qt.vector3d(0, 0, 400)
            eulerRotation.x: 0
            clipNear: 10
            clipFar: 1000
            fieldOfView: 45
        }

        DirectionalLight {
            eulerRotation.x: -30
            eulerRotation.y: 30
            brightness: 0.8
            ambientColor: Qt.rgba(0.4, 0.4, 0.4, 1.0)
            color: Theme.primaryContrastColor
        }

        DirectionalLight {
            eulerRotation.x: 20
            eulerRotation.y: -60
            brightness: 0.3
            color: "#aabbcc"
        }

        Node {
            id: rotatingNode
            eulerRotation.y: -globeRoot.globeRotation

            Model {
                id: earthModel
                source: "#Sphere"
                scale: Qt.vector3d(globeRoot.globeRadius / 50, globeRoot.globeRadius / 50, globeRoot.globeRadius / 50)

                materials: DefaultMaterial {
                    diffuseMap: Texture {
                        source: globeRoot.textureSource
                    }
                    specularAmount: 0.1
                    specularRoughness: 0.8
                }
            }

            Repeater3D {
                model: globeRoot.shots

                Node {
                    id: globeShotMarker
                    property var pos3d: latLonTo3D(modelData.lat, modelData.lon, globeRoot.globeRadius + 3)
                    property real ageHours: modelData.age || 0
                    property real shotOpacity: getOpacityFromAge(ageHours)
                    property bool isNew: ageHours < 1
                    property real newness: isNew ? (1 - ageHours) : 0
                    position: Qt.vector3d(pos3d.x, pos3d.y, pos3d.z)

                    Model {
                        source: "#Sphere"
                        property real s: globeRoot.widgetMode ? 0.0375 : 0.1
                        scale: Qt.vector3d(s, s, s)
                        materials: DefaultMaterial {
                            diffuseColor: globeRoot.mapTexture === "bright" ? "#ff6b35" : "#4a9eff"
                            opacity: (0.3 + 0.4 * globeShotMarker.newness) * globeShotMarker.shotOpacity
                        }
                    }

                    Model {
                        source: "#Sphere"
                        property real s: globeRoot.widgetMode ? 0.015 : 0.04
                        scale: Qt.vector3d(s, s, s)
                        materials: DefaultMaterial {
                            diffuseColor: globeRoot.mapTexture === "bright" ? "#ff6b35" : "#4a9eff"
                            opacity: globeShotMarker.shotOpacity
                            specularAmount: 0.5
                        }
                    }
                }
            }

            Node {
                id: globeTestMarker
                visible: globeRoot.testMode && globeRoot.testLatitude !== 0 && globeRoot.testLongitude !== 0
                property var pos3d: latLonTo3D(globeRoot.testLatitude, globeRoot.testLongitude, globeRoot.globeRadius + 5)
                position: Qt.vector3d(pos3d.x, pos3d.y, pos3d.z)

                Model {
                    source: "#Sphere"
                    scale: Qt.vector3d(0.3, 0.3, 0.3)
                    materials: DefaultMaterial {
                        diffuseColor: "#ff6b35"
                        opacity: 0.4
                    }
                }

                Model {
                    source: "#Sphere"
                    scale: Qt.vector3d(0.15, 0.15, 0.15)
                    materials: DefaultMaterial {
                        diffuseColor: "#ff6b35"
                        specularAmount: 0.6
                    }
                }
            }
        }
    }
}

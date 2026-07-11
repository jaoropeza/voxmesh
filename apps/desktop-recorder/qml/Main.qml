import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
  id: root
  visible: true
  width: 480
  height: 320
  title: qsTr("VoxMesh Recorder")

  ColumnLayout {
    anchors.centerIn: parent
    spacing: 12

    Label {
      text: qsTr("VoxMesh Recorder")
      font.pixelSize: 24
      Layout.alignment: Qt.AlignHCenter
    }

    Label {
      text: qsTr("Version %1").arg(appVersion)
      opacity: 0.7
      Layout.alignment: Qt.AlignHCenter
    }

    Label {
      text: qsTr("Phase 0 shell — audio capture arrives in Phase 2")
      opacity: 0.5
      Layout.alignment: Qt.AlignHCenter
    }
  }
}

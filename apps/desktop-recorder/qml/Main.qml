import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
  id: root
  visible: true
  width: 560
  height: 440
  minimumWidth: 480
  minimumHeight: 400
  title: qsTr("VoxMesh Recorder")

  ColumnLayout {
    anchors.fill: parent
    anchors.margins: 16
    spacing: 12

    RowLayout {
      Layout.fillWidth: true
      spacing: 10

      Label {
        text: qsTr("VoxMesh Recorder")
        font.pixelSize: 22
        font.bold: true
      }
      Label {
        text: qsTr("v%1").arg(appVersion)
        opacity: 0.6
      }
      Item { Layout.fillWidth: true }

      // Recording indicator (master prompt §18: visible while capturing).
      Rectangle {
        width: 14
        height: 14
        radius: 7
        color: recorder.recording ? "#e53935" : recorder.paused ? "#fb8c00" : "#9e9e9e"
        SequentialAnimation on opacity {
          running: recorder.recording
          loops: Animation.Infinite
          NumberAnimation { to: 0.25; duration: 600 }
          NumberAnimation { to: 1.0; duration: 600 }
        }
        onVisibleChanged: opacity = 1.0
      }
      Label { text: recorder.sessionState }
    }

    GroupBox {
      title: qsTr("Devices")
      Layout.fillWidth: true
      enabled: recorder.idle

      GridLayout {
        anchors.fill: parent
        columns: 2
        columnSpacing: 10
        rowSpacing: 8

        Label { text: qsTr("Microphone") }
        ComboBox {
          Layout.fillWidth: true
          model: recorder.microphoneNames
          currentIndex: recorder.selectedMicrophone
          onActivated: index => recorder.selectedMicrophone = index
        }

        Label { text: qsTr("System output") }
        ComboBox {
          Layout.fillWidth: true
          model: recorder.systemOutputNames
          currentIndex: recorder.selectedSystemOutput
          onActivated: index => recorder.selectedSystemOutput = index
        }

        Button {
          text: qsTr("Refresh devices")
          Layout.columnSpan: 2
          onClicked: recorder.refreshDevices()
        }
      }
    }

    RowLayout {
      Layout.fillWidth: true
      spacing: 8

      Button {
        text: qsTr("Record")
        visible: recorder.idle
        highlighted: true
        onClicked: recorder.start()
      }
      Button {
        text: qsTr("Pause")
        visible: recorder.recording
        onClicked: recorder.pause()
      }
      Button {
        text: qsTr("Resume")
        visible: recorder.paused
        onClicked: recorder.resume()
      }
      Button {
        text: qsTr("Stop")
        visible: recorder.recording || recorder.paused
        onClicked: recorder.stop()
      }
      Item { Layout.fillWidth: true }
    }

    RowLayout {
      Layout.fillWidth: true
      spacing: 24

      ColumnLayout {
        Label { text: qsTr("Frames captured"); opacity: 0.6; font.pixelSize: 12 }
        Label { text: recorder.framesCaptured.toLocaleString(); font.pixelSize: 18 }
      }
      ColumnLayout {
        Label { text: qsTr("Frames dropped"); opacity: 0.6; font.pixelSize: 12 }
        Label {
          text: recorder.framesDropped.toLocaleString()
          font.pixelSize: 18
          color: recorder.framesDropped > 0 ? "#e53935" : palette.text
        }
      }
    }

    Label {
      text: recorder.lastError
      visible: recorder.lastError.length > 0
      color: "#e53935"
      wrapMode: Text.WordWrap
      Layout.fillWidth: true
    }

    Item { Layout.fillHeight: true }

    Label {
      text: qsTr("Capture only — recording to disk arrives with the writer slice.")
      opacity: 0.5
      font.pixelSize: 11
    }
  }

  Component.onCompleted: recorder.refreshDevices()
}

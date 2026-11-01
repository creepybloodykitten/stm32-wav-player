import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: defaultAllowedOrientations

    // Палитра
    readonly property color colBg: "#0B0D11"
    readonly property color colCard: "#13161C"
    readonly property color colCardHover: "#1B1F27"
    readonly property color colCardActive: "#171E2A"
    readonly property color colAccent: "#2B72EE"
    readonly property color colAccentGreen: "#10B981"
    readonly property color colTextMain: "#FFFFFF"
    readonly property color colTextSec: "#7E8B9E"
    readonly property color colBorder: "#1F242E"

    property bool isShuffleActive: false
    property bool isRepeatActive: false
    property bool isHandlingFinish: false // Защита от двойного срабатывания

    // флаг и id для очереди
    property bool isPlayingFromQueue: false
    property string currentQueueId: "" // уникальный id

    Rectangle {
        anchors.fill: parent
        color: colBg
    }

    Connections {
        target: audioController
        ignoreUnknownSignals: true

        onPositionChanged: {
            if (!scrubSlider.down) scrubSlider.value = audioController.positionSec;

            if (audioController.durationSec > 0 && audioController.positionSec >= audioController.durationSec) {
                handleTrackFinished();
            }
        }
        onVolumeChanged: {
            if (!volumeSlider.down) volumeSlider.value = audioController.volume;
        }

        // На случай, если в твоем C++ контроллере уже есть сигнал окончания трека
        onPlaybackFinished: handleTrackFinished()
        onTrackFinished: handleTrackFinished()
    }

    Timer {
        id: resetFinishTimer
        interval: 1000
        onTriggered: isHandlingFinish = false
    }

    function handleTrackFinished() {
        if (isHandlingFinish) return;
        isHandlingFinish = true;
        resetFinishTimer.restart();

        if (isRepeatActive) {
            // Повторяем ту же самую песню с начала
            audioController.seek(0);
            if (!audioController.isPlaying) audioController.togglePlayPause();
            logEvent("Планшет", "Повтор: " + audioController.currentTrackTitle, colAccent);
        } else {
            // Обычный следующий трек (из очереди или медиатеки)
            playNextTrack();
        }
    }

    // модели данных
    ListModel {
        id: libraryModel
        ListElement { trackId: "track1"; title: "track1"; artist: "unknown"; duration: "05:40"; durationSec: 340 }
        ListElement { trackId: "track2"; title: "track2"; artist: "unknown"; duration: "05:40"; durationSec: 340 }
        ListElement { trackId: "track3"; title: "track3"; artist: "unknown"; duration: "04:12"; durationSec: 252 }
        ListElement { trackId: "track4"; title: "track4"; artist: "unknown"; duration: "03:55"; durationSec: 235 }
        ListElement { trackId: "track5"; title: "track5"; artist: "unknown"; duration: "06:18"; durationSec: 378 }
    }

    ListModel {
        id: queueModel
        // Оставим пустой по умолчанию, чтобы добавлять уникальные id динамически
    }

    ListModel {
        id: eventLogModel
        ListElement { time: "14:23:10"; user: "Планшет"; action: "Подключен"; colorTag: "#10B981" }
    }

    function logEvent(user, action, tagColor) {
        var d = new Date();
        var timeStr = (d.getHours()<10?"0":"")+d.getHours() + ":" +
                      (d.getMinutes()<10?"0":"")+d.getMinutes() + ":" +
                      (d.getSeconds()<10?"0":"")+d.getSeconds();
        eventLogModel.insert(0, { time: timeStr, user: user, action: action, colorTag: tagColor || colAccent });
        if (eventLogModel.count > 30) eventLogModel.remove(30);
    }

    //-------------------------------------------------------
    // логика переключения
    function playNextTrack()
    {
        var t;
        if (isPlayingFromQueue && queueModel.count > 0)
        {
            // ищем по уникальному id
            var qIdx = -1;
            for (var i = 0; i < queueModel.count; ++i) {
                if (queueModel.get(i).queueId === currentQueueId) { qIdx = i; break; }
            }
            if (qIdx === -1) qIdx = 0; // если трек удалили из очереди

            var nextQIdx = (qIdx + 1) % queueModel.count;
            t = queueModel.get(nextQIdx);

            currentQueueId = t.queueId; // обновляем текущий id
            audioController.selectTrack(t.title, t.artist, t.durationSec);
            logEvent("Планшет", "Следующий из очереди: " + t.title, colAccent);
        }
        else
        {
            if (libraryModel.count === 0) return;
            var lIdx = 0;
            for (var j = 0; j < libraryModel.count; ++j)
            {
                if (libraryModel.get(j).title === audioController.currentTrackTitle) { lIdx = j; break; }
            }
            var nextLIdx = isShuffleActive ? Math.floor(Math.random() * libraryModel.count) : ((lIdx + 1) % libraryModel.count);
            t = libraryModel.get(nextLIdx);
            audioController.selectTrack(t.title, t.artist, t.durationSec);
            logEvent("Планшет", "Следующий: " + t.title, colAccent);
        }
    }

    function playPrevTrack()
    {
        var t;
        if (isPlayingFromQueue && queueModel.count > 0)
        {
            var qIdx = -1;
            for (var i = 0; i < queueModel.count; ++i)
            {
                if (queueModel.get(i).queueId === currentQueueId) { qIdx = i; break; }
            }
            if (qIdx === -1) qIdx = 0;

            var prevQIdx = (qIdx - 1 + queueModel.count) % queueModel.count;
            t = queueModel.get(prevQIdx);

            currentQueueId = t.queueId;
            audioController.selectTrack(t.title, t.artist, t.durationSec);
            logEvent("Планшет", "Предыдущий из очереди: " + t.title, colAccent);
        }
        else
        {
            if (libraryModel.count === 0) return;
            var lIdx = 0;
            for (var j = 0; j < libraryModel.count; ++j) {
                if (libraryModel.get(j).title === audioController.currentTrackTitle) { lIdx = j; break; }
            }
            var prevLIdx = isShuffleActive ? Math.floor(Math.random() * libraryModel.count) : ((lIdx - 1 + libraryModel.count) % libraryModel.count);
            t = libraryModel.get(prevLIdx);
            audioController.selectTrack(t.title, t.artist, t.durationSec);
            logEvent("Планшет", "Предыдущий: " + t.title, colAccent);
        }
    }

    //-------------------------------------------------------
    // верхний блок: медиатека + очередь + журнал
    Item {
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            bottom: bottomPlayerBar.top
            margins: 16
        }

        Item {
            id: topCards
            width: parent.width
            height: parent.height * 0.70 - 16
            anchors.top: parent.top

            //-------------------------------------------------------
            // медиатека
            Rectangle {
                width: parent.width / 2 - 8
                height: parent.height
                anchors.left: parent.left
                color: colCard
                radius: 12
                border.color: colBorder

                Label {
                    id: libTitle
                    text: "Медиатека SD-карты"
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                    color: colTextMain
                    anchors { top: parent.top; left: parent.left; margins: 16 }
                }

                Rectangle {
                    id: libLine
                    width: parent.width
                    height: 1
                    color: colBorder
                    anchors { top: libTitle.bottom; topMargin: 16 }
                }

                SilicaListView {
                    anchors { top: libLine.bottom; bottom: parent.bottom; left: parent.left; right: parent.right; margins: 16 }
                    clip: true
                    model: libraryModel
                    spacing: 8

                    delegate: BackgroundItem {
                        id: libItem
                        width: parent.width
                        height: 90
                        // В медиатеке подсвечиваем по названию, чтобы всегда видеть какой трек в целом играет
                        readonly property bool isCurrent: model.title === audioController.currentTrackTitle

                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            color: libItem.isCurrent ? colCardActive : (libItem.down ? colCardHover : "#181C23")
                            border.color: libItem.isCurrent ? colAccent : colBorder
                            border.width: libItem.isCurrent ? 2 : 1

                            Rectangle {
                                id: mIcon
                                width: 50; height: 50
                                radius: 10
                                color: libItem.isCurrent ? Theme.rgba(colAccent, 0.25) : "#222733"
                                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }

                                Image {
                                    source: "image://theme/icon-m-music"
                                    anchors.centerIn: parent
                                    opacity: libItem.isCurrent ? 1.0 : 0.6
                                }
                            }

                            IconButton {
                                id: btnAdd
                                icon.source: "image://theme/icon-m-add"
                                width: 40
                                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                                onClicked: {
                                    // Генерируем уникальный ID для каждого добавления
                                    var uniqueId = "q_" + Date.now() + "_" + Math.floor(Math.random() * 10000);
                                    queueModel.append({
                                        queueId: uniqueId,
                                        trackId: model.trackId,
                                        title: model.title,
                                        artist: model.artist,
                                        duration: model.duration,
                                        durationSec: model.durationSec
                                    });
                                    audioController.addToQueue(model.trackId, model.title, model.durationSec);
                                    logEvent("Планшет", "В очередь: " + model.title, colAccent);
                                }
                            }

                            Label {
                                id: libTime
                                text: model.duration
                                font.pixelSize: Theme.fontSizeLarge
                                font.bold: true
                                color: colTextMain
                                width: 70
                                horizontalAlignment: Text.AlignRight
                                anchors { right: btnAdd.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
                            }

                            Column {
                                anchors { left: mIcon.right; leftMargin: 16; right: libTime.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
                                Label { text: model.title; font.pixelSize: Theme.fontSizeLarge; font.bold: true; color: colTextMain; width: parent.width; truncationMode: TruncationMode.Fade }
                                Label { text: model.artist; font.pixelSize: Theme.fontSizeMedium; color: colTextSec; width: parent.width; truncationMode: TruncationMode.Fade }
                            }
                        }

                        onClicked: {
                            isPlayingFromQueue = false;
                            currentQueueId = ""; // Сбрасываем ID очереди
                            audioController.selectTrack(model.title, model.artist, model.durationSec);
                            logEvent("Планшет", "Включил: " + model.title, colAccentGreen);
                        }
                    }
                }
            }

            //-------------------------------------------------------
            // очередь воспроизведения
            Rectangle {
                width: parent.width / 2 - 8
                height: parent.height
                anchors.right: parent.right
                color: colCard
                radius: 12
                border.color: colBorder

                Label {
                    id: queueTitle
                    text: "Очередь"
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                    color: colTextMain
                    anchors { top: parent.top; left: parent.left; margins: 16 }
                }

                Button {
                    id: btnClearQueue
                    text: "Очистить"
                    width: 150
                    //height: 30
                    anchors {
                        right: parent.right; rightMargin: 16
                        top: parent.top; topMargin: 14
                        bottom: queueLine.top; bottomMargin: 14
                        //verticalCenter: queueTitle.verticalCenter // Ровно по центру слова "Очередь"
                    }
                    onClicked: {
                        queueModel.clear();
                        logEvent("Планшет", "Очистил очередь", "#EF4444");
                    }
                }


                Rectangle {
                    id: queueLine
                    width: parent.width
                    height: 1
                    color: colBorder
                    anchors { top: queueTitle.bottom; topMargin: 16 }
                }

                SilicaListView {
                    id: queueList
                    anchors { top: queueLine.bottom; bottom: parent.bottom; left: parent.left; right: parent.right; margins: 16; topMargin: 16 }
                    clip: true
                    model: queueModel
                    spacing: 8

                    delegate: Item {
                        id: queueDelegateRoot
                        width: ListView.view.width
                        height: 90

                        // Теперь проверяем конкретно уникальный ID! Дубликаты подсвечиваться не будут.
                        readonly property bool isCurrent: isPlayingFromQueue && (model.queueId === currentQueueId)

                        Rectangle {
                            id: queueItemRect
                            width: parent.width
                            height: parent.height
                            radius: 10

                            color: queueDelegateRoot.isCurrent ? colCardActive : "#181C23"
                            border.color: queueDelegateRoot.isCurrent ? colAccent : colBorder
                            border.width: queueDelegateRoot.isCurrent ? 2 : 1

                            Behavior on x { enabled: !ma.drag.active; NumberAnimation { duration: 150 } }
                            Behavior on y { enabled: !ma.drag.active; NumberAnimation { duration: 150 } }

                            Label {
                                id: qIdx
                                text: (index + 1).toString()
                                color: queueDelegateRoot.isCurrent ? colAccent : colTextSec
                                font.pixelSize: Theme.fontSizeLarge
                                font.bold: true
                                width: 30
                                horizontalAlignment: Text.AlignHCenter
                                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                            }

                            Label {
                                id: qTime
                                text: model.duration
                                font.pixelSize: Theme.fontSizeLarge
                                color: colTextSec
                                width: 70
                                horizontalAlignment: Text.AlignRight
                                anchors { right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter }
                            }

                            Column {
                                anchors { left: qIdx.right; leftMargin: 16; right: qTime.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
                                Label { text: model.title; font.pixelSize: Theme.fontSizeLarge; font.bold: true; color: colTextMain; width: parent.width; truncationMode: TruncationMode.Fade }
                                Label { text: model.artist; font.pixelSize: Theme.fontSizeMedium; color: colTextSec; width: parent.width; truncationMode: TruncationMode.Fade }
                            }
                        }

                        MouseArea {
                            id: ma
                            anchors.fill: parent
                            drag.target: queueItemRect
                            drag.axis: Drag.XAndYAxis

                            onClicked: {
                                isPlayingFromQueue = true;
                                currentQueueId = model.queueId; // Запоминаем конкретный ID
                                audioController.selectTrack(model.title, model.artist, model.durationSec);
                                logEvent("Планшет", "Запустил из очереди: " + model.title, colAccentGreen);
                            }

                            onReleased: {
                                if (Math.abs(queueItemRect.x) > width / 3) {
                                    queueModel.remove(index);
                                } else {
                                    queueItemRect.x = 0;
                                    queueItemRect.y = 0;
                                }
                            }

                            onPositionChanged: {
                                if (drag.active) {
                                    var newIdx = index + Math.floor((queueItemRect.y + height/2) / height);
                                    if (newIdx !== index && newIdx >= 0 && newIdx < queueModel.count) {
                                        queueModel.move(index, newIdx, 1);
                                        queueItemRect.y = 0;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        //-------------------------------------------------------
        // журнал событий
        Rectangle {
            width: parent.width
            height: parent.height * 0.30
            anchors.bottom: parent.bottom
            color: colCard
            radius: 12
            border.color: colBorder

            Label {
                id: logTitle
                text: "Журнал действий"
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: colTextSec
                anchors { top: parent.top; left: parent.left; margins: 16 }
            }

            Rectangle {
                id: logLine
                width: parent.width
                height: 1
                color: colBorder
                anchors { top: logTitle.bottom; topMargin: 12 }
            }

            SilicaListView {
                anchors { top: logLine.bottom; bottom: parent.bottom; left: parent.left; right: parent.right; margins: 16 }
                clip: true
                model: eventLogModel
                spacing: 8

                delegate: Item {
                    width: parent.width
                    height: 30

                    Label {
                        id: lTime
                        text: model.time
                        font.pixelSize: Theme.fontSizeMedium
                        color: colTextSec
                        width: 120
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    }

                    Label {
                        id: lUser
                        text: model.user
                        font.pixelSize: Theme.fontSizeMedium
                        font.bold: true
                        color: model.colorTag
                        width: 150
                        anchors { left: lTime.right; leftMargin: 16; verticalCenter: parent.verticalCenter }
                    }

                    Label {
                        text: model.action
                        font.pixelSize: Theme.fontSizeMedium
                        color: colTextMain
                        anchors { left: lUser.right; leftMargin: 16; right: parent.right; verticalCenter: parent.verticalCenter }
                        truncationMode: TruncationMode.Fade
                    }
                }
            }
        }
    }

    //-------------------------------------------------------
    // нижняя панель плеера
    Rectangle {
        id: bottomPlayerBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 400
        color: colCard
        border.color: colBorder
        border.width: 1

        //-------------------------------------------------------
        // верхняя половина панели (трек и таймлайн)
        Item {
            id: topHalfControls
            width: parent.width
            height: parent.height / 2
            anchors.top: parent.top

            Label {
                id: scrubTimeTotal
                text: formatTime(audioController.durationSec)
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: colTextSec
                width: 70
                horizontalAlignment: Text.AlignHCenter
                anchors { right: parent.right; rightMargin: 40; verticalCenter: parent.verticalCenter }
            }

            Slider {
                id: scrubSlider
                width: parent.width * 0.45
                leftMargin: 45
                rightMargin: 16
                anchors {
                    right: scrubTimeTotal.left; rightMargin: 8
                    verticalCenter: parent.verticalCenter
                }
                minimumValue: 0
                maximumValue: audioController.durationSec
                onValueChanged: { if (down) audioController.seek(Math.round(value)); }
            }

            Label {
                id: scrubTimeCurr
                text: formatTime(audioController.positionSec)
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: colTextSec
                width: 70
                horizontalAlignment: Text.AlignHCenter
                anchors { right: scrubSlider.left; rightMargin: -20; verticalCenter: parent.verticalCenter }
            }

            Column {
                id: trackInfo
                anchors {
                    left: parent.left; leftMargin: 40;
                    right: scrubTimeCurr.left; rightMargin: 20;
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    text: audioController.currentTrackTitle;
                    font.pixelSize: Theme.fontSizeHuge;
                    font.bold: true;
                    color: colTextMain;
                    width: parent.width;
                    truncationMode: TruncationMode.Fade;
                    horizontalAlignment: Text.AlignHCenter
                }
                Label {
                    text: audioController.currentArtist;
                    font.pixelSize: Theme.fontSizeLarge;
                    color: colTextSec;
                    width: parent.width;
                    truncationMode: TruncationMode.Fade;
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        //-------------------------------------------------------
        // нижняя половина панели (кнопки управления и громкость)
        Item {
            id: bottomHalfControls
            width: parent.width
            height: parent.height / 2
            anchors.bottom: parent.bottom

            Row {
                id: playControls
                anchors { left: parent.left; leftMargin: 40; verticalCenter: parent.verticalCenter }
                spacing: 30

                IconButton { icon.source: "image://theme/icon-l-shuffle"; icon.color: isShuffleActive ? colAccent : colTextSec; anchors.verticalCenter: parent.verticalCenter; onClicked: { isShuffleActive = !isShuffleActive; } }
                IconButton { icon.source: "image://theme/icon-l-previous"; anchors.verticalCenter: parent.verticalCenter; onClicked: playPrevTrack() }

                IconButton {
                    icon.source: audioController.isPlaying ? "image://theme/icon-l-pause" : "image://theme/icon-l-play"
                    icon.color: colAccent
                    anchors.verticalCenter: parent.verticalCenter
                    transform: Scale { xScale: 1.5; yScale: 1.5; origin.x: 32; origin.y: 32 }
                    onClicked: audioController.togglePlayPause()
                }

                IconButton { icon.source: "image://theme/icon-l-next"; anchors.verticalCenter: parent.verticalCenter; onClicked: playNextTrack() }
                IconButton { icon.source: "image://theme/icon-l-repeat"; icon.color: isRepeatActive ? colAccent : colTextSec; anchors.verticalCenter: parent.verticalCenter; onClicked: { isRepeatActive = !isRepeatActive; } }
            }

            Label {
                id: volLabel
                text: Math.round(volumeSlider.value) + "%"
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: colTextMain
                width: 70
                horizontalAlignment: Text.AlignRight
                anchors { right: parent.right; rightMargin: 40; verticalCenter: parent.verticalCenter }
            }

            Slider {
                id: volumeSlider
                width: 250
                height: 50
                leftMargin: 16
                rightMargin: 16
                anchors { right: volLabel.left; rightMargin: 16; verticalCenter: parent.verticalCenter; verticalCenterOffset: -12 }
                minimumValue: 0
                maximumValue: 100
                stepSize: 1
                value: audioController.volume
                onValueChanged: { if (down) audioController.setVolume(Math.round(value)); }
            }

            Image {
                id: volIcon
                source: "image://theme/icon-m-speaker"
                anchors { right: volumeSlider.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
            }
        }
    }

    function formatTime(sec) {
        var m = Math.floor(sec / 60);
        var s = Math.floor(sec % 60);
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }
}

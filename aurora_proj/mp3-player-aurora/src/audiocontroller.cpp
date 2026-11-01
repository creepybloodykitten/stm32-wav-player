#include "audiocontroller.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostAddress>
#include <QDebug>

AudioController::AudioController(QObject *parent)
    : QObject(parent)
    , m_client(new QMQTT::Client(QHostAddress::LocalHost, 1883, this))
    , m_interpolationTimer(new QTimer(this))
    , m_currentTitle("track1")
    , m_currentArtist("unknown")
    , m_basePositionSec(0)
    , m_displayPositionSec(0)
    , m_durationSec(340)
    , m_isPlaying(false)
    , m_volume(80)
    , m_lastSyncTimestamp(0)
{
    // Локальный таймер для плавного хода секунд в UI (раз в 500 мс)
    m_interpolationTimer->setInterval(500);
    connect(m_interpolationTimer, &QTimer::timeout, this, &AudioController::onInterpolationTick);

    // Подключение сигналов qmqtt
    connect(m_client, &QMQTT::Client::connected, this, &AudioController::onMqttConnected);
    connect(m_client, &QMQTT::Client::disconnected, this, &AudioController::onMqttDisconnected);
    connect(m_client, &QMQTT::Client::received, this, &AudioController::onMqttReceived);
}

void AudioController::connectToBroker(const QString &host, int port)
{
    qDebug() << "[MQTT] Подключение к брокеру:" << host << ":" << port;
    m_client->setHost(QHostAddress(host));
    m_client->setPort(port);
    m_client->setClientId("KVADRA_T_" + QString::number(QDateTime::currentMSecsSinceEpoch() % 10000));
    m_client->connectToHost();
}

void AudioController::disconnectFromBroker()
{
    if (m_client && m_client->isConnectedToHost()) {
        m_client->disconnectFromHost();
    }
}

void AudioController::onMqttConnected()
{
    qDebug() << "[MQTT] Успешно подключено к брокеру!";
    emit connectionChanged();

    // Подписка на топики статуса от STM32 / ESP32
    m_client->subscribe("device/stm32/player/status", 1);
    m_client->subscribe("device/stm32/player/heartbeat", 0);
    m_client->subscribe("device/stm32/queue/status", 1);
}

void AudioController::onMqttDisconnected()
{
    qDebug() << "[MQTT] Отключено от брокера";
    emit connectionChanged();
}

void AudioController::onMqttReceived(const QMQTT::Message &message)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(message.payload(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    QJsonObject obj = doc.object();
    QString topicName = message.topic();

    if (topicName == "device/stm32/player/status") {
        m_isPlaying = (obj["status"].toString() == "playing");
        m_basePositionSec = obj["position_sec"].toInt();
        m_displayPositionSec = m_basePositionSec;
        m_durationSec = obj["duration_sec"].toInt(340);
        if (obj.contains("title")) m_currentTitle = obj["title"].toString();
        if (obj.contains("artist")) m_currentArtist = obj["artist"].toString();

        m_lastSyncTimestamp = QDateTime::currentMSecsSinceEpoch();

        if (m_isPlaying) {
            m_interpolationTimer->start();
        } else {
            m_interpolationTimer->stop();
        }

        emit stateChanged();
        emit positionChanged();
    }
    else if (topicName == "device/stm32/player/heartbeat") {
        int reportedSec = obj["pos_sec"].toInt();
        if (qAbs(reportedSec - m_displayPositionSec) > 1) {
            m_basePositionSec = reportedSec;
            m_displayPositionSec = reportedSec;
            m_lastSyncTimestamp = QDateTime::currentMSecsSinceEpoch();
            emit positionChanged();
        }
    }
}

void AudioController::onInterpolationTick()
{
    if (!m_isPlaying) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsedSec = (now - m_lastSyncTimestamp) / 1000;
    int calculated = m_basePositionSec + elapsedSec;

    if (calculated >= m_durationSec) {
        calculated = m_durationSec;
        m_isPlaying = false;
        m_interpolationTimer->stop();
        emit stateChanged();
    }

    if (calculated != m_displayPositionSec) {
        m_displayPositionSec = calculated;
        emit positionChanged();
    }
}

// --- Управление (работает локально для UI + шлет MQTT, если подключено) ---

void AudioController::play()
{
    m_isPlaying = true;
    m_lastSyncTimestamp = QDateTime::currentMSecsSinceEpoch();
    m_interpolationTimer->start();
    emit stateChanged();

    sendMqttCommand("resume");
}

void AudioController::pause()
{
    m_isPlaying = false;
    m_interpolationTimer->stop();
    emit stateChanged();

    sendMqttCommand("pause");
}

void AudioController::togglePlayPause()
{
    if (m_isPlaying) pause(); else play();
}

void AudioController::seek(int seconds)
{
    m_basePositionSec = seconds;
    m_displayPositionSec = seconds;
    m_lastSyncTimestamp = QDateTime::currentMSecsSinceEpoch();
    emit positionChanged();

    QVariantMap params;
    params["position_sec"] = seconds;
    sendMqttCommand("seek", params);
}

void AudioController::selectTrack(const QString &title, const QString &artist, int durationSec)
{
    m_currentTitle = title;
    m_currentArtist = artist;
    m_durationSec = durationSec;
    m_basePositionSec = 0;
    m_displayPositionSec = 0;
    emit stateChanged();
    emit positionChanged();

    play(); // сразу запускаем воспроизведение выбранного трека
}

void AudioController::nextTrack()
{
    sendMqttCommand("next");
}

void AudioController::prevTrack()
{
    sendMqttCommand("prev");
}

void AudioController::setVolume(int volume)
{
    m_volume = qBound(0, volume, 100);
    emit volumeChanged();

    QVariantMap params;
    params["volume"] = m_volume;
    sendMqttCommand("set_volume", params);
}

void AudioController::addToQueue(const QString &trackId, const QString &title, int duration)
{
    QVariantMap item;
    item["trackId"] = trackId;
    item["title"] = title;
    item["duration"] = duration;
    m_queue.append(item);
    emit queueChanged();

    sendMqttCommand("queue_add", item);
}

void AudioController::removeFromQueue(int index)
{
    if (index >= 0 && index < m_queue.size()) {
        m_queue.removeAt(index);
        emit queueChanged();
        QVariantMap p;
        p["index"] = index;
        sendMqttCommand("queue_remove", p);
    }
}

void AudioController::moveQueueItem(int fromIndex, int toIndex)
{
    if (fromIndex >= 0 && toIndex >= 0 && fromIndex < m_queue.size() && toIndex < m_queue.size()) {
        m_queue.move(fromIndex, toIndex);
        emit queueChanged();
        QVariantMap p;
        p["from"] = fromIndex;
        p["to"] = toIndex;
        sendMqttCommand("queue_reorder", p);
    }
}

void AudioController::sendMqttCommand(const QString &cmd, const QVariantMap &params)
{
    // Если брокер не подключен — просто ничего не шлем по сети (UI продолжает работать)
    if (!isConnected()) return;

    QJsonObject obj;
    obj["cmd"] = cmd;
    obj["client"] = "KVADRA_T";
    obj["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        obj[it.key()] = QJsonValue::fromVariant(it.value());
    }

    QJsonDocument doc(obj);
    QMQTT::Message msg;
    msg.setTopic("device/stm32/player/cmd");
    msg.setPayload(doc.toJson(QJsonDocument::Compact));
    msg.setQos(1);

    m_client->publish(msg);
}

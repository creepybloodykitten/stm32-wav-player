#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <QDateTime>
#include <qmqtt.h> // Подключаем библиотеку qmqtt

class AudioController : public QObject
{
    Q_OBJECT

    // Q_PROPERTY для связывания с QML
    Q_PROPERTY(QString currentTrackTitle READ currentTrackTitle NOTIFY stateChanged)
    Q_PROPERTY(QString currentArtist READ currentArtist NOTIFY stateChanged)
    Q_PROPERTY(int positionSec READ positionSec WRITE seek NOTIFY positionChanged)
    Q_PROPERTY(int durationSec READ durationSec NOTIFY stateChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY stateChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(QVariantList queueList READ queueList NOTIFY queueChanged)

public:
    explicit AudioController(QObject *parent = nullptr);

    QString currentTrackTitle() const { return m_currentTitle; }
    QString currentArtist() const { return m_currentArtist; }
    int positionSec() const { return m_displayPositionSec; }
    int durationSec() const { return m_durationSec; }
    bool isPlaying() const { return m_isPlaying; }
    int volume() const { return m_volume; }
    // Проверка соединения через QMQTT::Client
    bool isConnected() const { return m_client && m_client->isConnectedToHost(); }
    QVariantList queueList() const { return m_queue; }

public slots:
    // Методы, доступные из QML
    Q_INVOKABLE void connectToBroker(const QString &host = "192.168.1.100", int port = 1883);
    Q_INVOKABLE void disconnectFromBroker();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void seek(int seconds);
    Q_INVOKABLE void nextTrack();
    Q_INVOKABLE void prevTrack();
    Q_INVOKABLE void selectTrack(const QString &title, const QString &artist, int durationSec);
    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void addToQueue(const QString &trackId, const QString &title, int duration);
    Q_INVOKABLE void removeFromQueue(int index);
    Q_INVOKABLE void moveQueueItem(int fromIndex, int toIndex);

signals:
    void stateChanged();
    void positionChanged();
    void volumeChanged();
    void connectionChanged();
    void queueChanged();
    void errorOccurred(const QString &message);

private slots:
    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttReceived(const QMQTT::Message &message); // Слот для qmqtt
    void onInterpolationTick();

private:
    void sendMqttCommand(const QString &cmd, const QVariantMap &params = QVariantMap());

    QMQTT::Client *m_client;
    QTimer *m_interpolationTimer;

    // Состояние плеера
    QString m_currentTitle;
    QString m_currentArtist;
    int m_basePositionSec;
    int m_displayPositionSec;
    int m_durationSec;
    bool m_isPlaying;
    int m_volume;
    qint64 m_lastSyncTimestamp;
    QVariantList m_queue;
};

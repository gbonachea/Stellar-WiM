#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include <functional>

class QLocalSocket;

/*!
 * Cliente del protocolo IPC de Stellar WiM.
 *
 * Usa el mismo socket Unix, tramas y reglas que stellar-wimctl. Se comparte
 * entre Stellar Settings, Stellar Panel y stellar-wim-settings para que no
 * exista lógica de protocolo duplicada.
 */
class StellarIpcClient : public QObject
{
    Q_OBJECT

public:
    struct Result {
        bool ok = false;
        QJsonValue value;
        QString error;
    };
    using ResultCallback = std::function<void(const Result &)>;
    using EventCallback = std::function<void(const QJsonObject &)>;

    explicit StellarIpcClient(QObject *parent = nullptr);
    ~StellarIpcClient() override;

    static QString socketPath();
    static bool available();

    /* Petición puntual. Nunca ejecuta comandos de shell. */
    void request(const QString &command, ResultCallback callback);

    /* Suscripción de larga duración a EVENT <json>. */
    void subscribe(EventCallback callback);
    void unsubscribe();
    bool subscribed() const;

signals:
    void eventReceived(const QJsonObject &event);
    void connectionError(const QString &message);

private slots:
    void onConnected();
    void onReadyRead();
    void onError();

private:
    void connectSubscription();
    void parseSubscriptionFrames();

    QLocalSocket *m_socket = nullptr;
    QByteArray m_buffer;
    EventCallback m_eventCallback;
    bool m_subscribed = false;
    bool m_subscribeSent = false;
    QString m_lastError;
};

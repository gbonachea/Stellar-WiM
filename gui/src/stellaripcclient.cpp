#include "stellaripcclient.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QLocalSocket>
#include <QTimer>

#include <cstring>

namespace
{
constexpr quint32 kMagic = 0x5357494d; // "SWIM"
constexpr int kHeaderSize = 8;
constexpr int kMaxFrame = 65536;

void putLe32(char *dst, quint32 value)
{
    dst[0] = static_cast<char>(value & 0xff);
    dst[1] = static_cast<char>((value >> 8) & 0xff);
    dst[2] = static_cast<char>((value >> 16) & 0xff);
    dst[3] = static_cast<char>((value >> 24) & 0xff);
}

quint32 getLe32(const char *src)
{
    return static_cast<quint32>(static_cast<quint8>(src[0]))
        | (static_cast<quint32>(static_cast<quint8>(src[1])) << 8)
        | (static_cast<quint32>(static_cast<quint8>(src[2])) << 16)
        | (static_cast<quint32>(static_cast<quint8>(src[3])) << 24);
}

QByteArray frame(const QString &payload)
{
    const QByteArray bytes = payload.toUtf8();
    QByteArray out(kHeaderSize, Qt::Uninitialized);
    putLe32(out.data(), kMagic);
    putLe32(out.data() + 4, static_cast<quint32>(bytes.size()));
    out.append(bytes);
    return out;
}

/*! PeticiónIPC desechable: un socket por consulta, sin bloquear la interfaz. */
class StellarIpcRequest final : public QObject
{
    Q_OBJECT
public:
    StellarIpcRequest(const QString &path, const QString &command,
                      StellarIpcClient::ResultCallback callback, QObject *parent)
        : QObject(parent), m_callback(std::move(callback))
    {
        m_socket = new QLocalSocket(this);
        connect(m_socket, &QLocalSocket::connected, this, [this, command] {
            const QByteArray data = frame(command);
            m_socket->write(data.constData(), data.size());
            m_socket->flush();
        });
        connect(m_socket, &QLocalSocket::readyRead, this, [this] { read(); });
        connect(m_socket, &QLocalSocket::errorOccurred, this, [this] {
            finish(false, QJsonValue(), m_socket->errorString());
        });
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, [this] {
            finish(false, QJsonValue(), QStringLiteral("Stellar WiM IPC timed out"));
        });
        m_timer->start(3000);
        m_socket->connectToServer(path);
    }

private:
    void read()
    {
        m_buffer.append(m_socket->readAll());
        while (m_buffer.size() >= kHeaderSize) {
            if (getLe32(m_buffer.constData()) != kMagic) {
                finish(false, QJsonValue(), QStringLiteral("invalid IPC frame"));
                return;
            }
            const quint32 len = getLe32(m_buffer.constData() + 4);
            if (len == 0 || len > kMaxFrame) {
                finish(false, QJsonValue(), QStringLiteral("invalid IPC frame length"));
                return;
            }
            if (m_buffer.size() < kHeaderSize + static_cast<int>(len)) {
                return;
            }
            const QByteArray payload = m_buffer.mid(kHeaderSize, len);
            m_buffer.clear();
            const QString text = QString::fromUtf8(payload);
            if (text.startsWith(QStringLiteral("OK "))) {
                const QByteArray payloadJson = text.mid(3).toUtf8();
                const QJsonDocument doc = QJsonDocument::fromJson(payloadJson);
                QJsonValue value;
                if (doc.isArray()) {
                    value = QJsonValue(doc.array());
                } else if (doc.isObject()) {
                    value = QJsonValue(doc.object());
                } else {
                    QJsonParseError error;
                    const QJsonDocument wrapped = QJsonDocument::fromJson(
                        QByteArray("[") + payloadJson + "]", &error);
                    if (error.error != QJsonParseError::NoError ||
                        !wrapped.isArray() || wrapped.array().size() != 1) {
                        finish(false, QJsonValue(), QStringLiteral("invalid JSON reply"));
                        return;
                    }
                    value = wrapped.array().at(0);
                }
                finish(true, value, QString());
            } else if (text.startsWith(QStringLiteral("ERR "))) {
                finish(false, QJsonValue(), text.mid(4));
            } else if (text.startsWith(QStringLiteral("EVENT "))) {
                // Events are handled by the subscription socket; ignore here.
                continue;
            } else {
                finish(false, QJsonValue(), QStringLiteral("malformed IPC reply"));
            }
            return;
        }
    }

    void finish(bool ok, const QJsonValue &value, const QString &error)
    {
        if (m_done) {
            return;
        }
        m_done = true;
        m_timer->stop();
        if (m_callback) {
            StellarIpcClient::Result result;
            result.ok = ok;
            result.value = value;
            result.error = error;
            m_callback(result);
        }
        deleteLater();
    }

    QLocalSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    QByteArray m_buffer;
    StellarIpcClient::ResultCallback m_callback;
    bool m_done = false;
};
} // namespace

QString StellarIpcClient::socketPath()
{
    const QByteArray override = qgetenv("STELLAR_WIM_IPC_SOCKET");
    if (!override.isEmpty()) {
        return QString::fromLocal8Bit(override);
    }
    const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
    if (runtime.isEmpty()) {
        return {};
    }
    QString display = QString::fromLocal8Bit(qgetenv("WAYLAND_DISPLAY"));
    QString clean;
    for (const QChar c : display) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) {
            clean.append(c);
        }
    }
    if (clean.isEmpty()) {
        clean = QStringLiteral("default");
    }
    return QString::fromLocal8Bit(runtime)
        + QStringLiteral("/stellar-wim-") + clean + QStringLiteral(".sock");
}

bool StellarIpcClient::available()
{
    const QString path = socketPath();
    return !path.isEmpty() && QFileInfo::exists(path);
}

StellarIpcClient::StellarIpcClient(QObject *parent)
    : QObject(parent)
{
}

StellarIpcClient::~StellarIpcClient()
{
    unsubscribe();
}

void StellarIpcClient::request(const QString &command, ResultCallback callback)
{
    const QString path = socketPath();
    if (path.isEmpty()) {
        Result result;
        result.error = QStringLiteral("XDG_RUNTIME_DIR is not set");
        if (callback) {
            callback(result);
        }
        return;
    }
    new StellarIpcRequest(path, command, std::move(callback), this);
}

void StellarIpcClient::subscribe(EventCallback callback)
{
    m_eventCallback = std::move(callback);
    if (m_subscribed || m_socket) {
        return;
    }
    connectSubscription();
}

void StellarIpcClient::connectSubscription()
{
    const QString path = socketPath();
    if (path.isEmpty()) {
        emit connectionError(QStringLiteral("XDG_RUNTIME_DIR is not set"));
        return;
    }
    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::connected, this, [this] {
        const QByteArray data = frame(QStringLiteral("EVENTS SUBSCRIBE"));
        m_socket->write(data.constData(), data.size());
        m_socket->flush();
        m_subscribeSent = true;
        m_subscribed = true;
        m_lastError.clear();
    });
    connect(m_socket, &QLocalSocket::readyRead, this, &StellarIpcClient::onReadyRead);
    connect(m_socket, &QLocalSocket::errorOccurred, this, &StellarIpcClient::onError);
    m_socket->connectToServer(path);
}

void StellarIpcClient::unsubscribe()
{
    if (m_socket) {
        m_socket->disconnectFromServer();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_subscribed = false;
    m_subscribeSent = false;
    m_eventCallback = nullptr;
    m_lastError.clear();
    m_buffer.clear();
}

bool StellarIpcClient::subscribed() const
{
    return m_subscribed;
}

void StellarIpcClient::onConnected()
{
    // Subscription frames are written in the connected lambda.
}

void StellarIpcClient::onReadyRead()
{
    parseSubscriptionFrames();
}

void StellarIpcClient::onError()
{
    const QString message = m_socket ? m_socket->errorString()
                                     : QStringLiteral("IPC error");
    if (!m_subscribed && message != m_lastError) {
        emit connectionError(message);
        m_lastError = message;
    }
    m_subscribed = false;
    m_subscribeSent = false;
    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    // The compositor may start after the panel/Settings, or IPC may be
    // re-enabled later. Keep retrying while a subscriber is still interested.
    if (m_eventCallback) {
        QTimer::singleShot(1000, this, [this] {
            if (m_eventCallback && !m_subscribed && !m_socket) {
                connectSubscription();
            }
        });
    }
}

void StellarIpcClient::parseSubscriptionFrames()
{
    if (!m_socket) {
        return;
    }
    m_buffer.append(m_socket->readAll());
    while (m_buffer.size() >= kHeaderSize) {
        if (getLe32(m_buffer.constData()) != kMagic) {
            m_buffer.clear();
            return;
        }
        const quint32 len = getLe32(m_buffer.constData() + 4);
        if (len == 0 || len > kMaxFrame) {
            m_buffer.clear();
            return;
        }
        if (m_buffer.size() < kHeaderSize + static_cast<int>(len)) {
            return;
        }
        const QByteArray payload = m_buffer.mid(kHeaderSize, len);
        m_buffer.remove(0, kHeaderSize + static_cast<int>(len));
        const QString text = QString::fromUtf8(payload);
        if (!text.startsWith(QStringLiteral("EVENT "))) {
            continue; // subscribed acknowledgement
        }
        const QJsonDocument doc = QJsonDocument::fromJson(text.mid(6).toUtf8());
        if (!doc.isObject()) {
            continue;
        }
        const QJsonObject event = doc.object();
        emit eventReceived(event);
        if (m_eventCallback) {
            m_eventCallback(event);
        }
    }
}

#include "stellaripcclient.moc"

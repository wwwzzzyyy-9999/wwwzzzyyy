#include "redisclient.h"

#include <QDebug>



#include <arpa/inet.h>

#include <cerrno>

#include <cstring>

#include <fcntl.h>

#include <netdb.h>

#include <poll.h>

#include <sys/socket.h>

#include <unistd.h>



namespace {



bool setSocketTimeout(int fd, int timeoutMs)

{

    if (timeoutMs <= 0)

        return true;

    timeval tv;

    tv.tv_sec  = timeoutMs / 1000;

    tv.tv_usec = (timeoutMs % 1000) * 1000;

    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0

        && setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;

}



} // namespace



RedisClient::RedisClient() = default;



RedisClient::~RedisClient()

{

    close();

}



void RedisClient::resetSocket()

{

    if (m_fd >= 0) {

        ::close(m_fd);

        m_fd = -1;

    }

    m_ready = false;

}



void RedisClient::close()

{

    QMutexLocker lock(&m_mutex);

    resetSocket();

}



bool RedisClient::isOpen() const

{

    QMutexLocker lock(&m_mutex);

    return m_ready && m_fd >= 0;

}



bool RedisClient::open(const Config &cfg)

{

    QMutexLocker lock(&m_mutex);

    m_cfg = cfg;

    resetSocket();

    return ensureConnected();

}



bool RedisClient::ensureConnected()

{

    if (m_ready && m_fd >= 0)

        return true;



    resetSocket();



    addrinfo hints;

    std::memset(&hints, 0, sizeof(hints));

    hints.ai_family   = AF_UNSPEC;

    hints.ai_socktype = SOCK_STREAM;



    const QByteArray host = m_cfg.host.toUtf8();

    const QByteArray port = QByteArray::number(m_cfg.port);

    addrinfo *result = nullptr;

    if (getaddrinfo(host.constData(), port.constData(), &hints, &result) != 0 || !result) {

        qDebug() << "Redis 解析主机失败：" << m_cfg.host;

        return false;

    }



    int fd = -1;

    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {

        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (fd < 0)

            continue;



        const int flags = fcntl(fd, F_GETFL, 0);

        if (flags >= 0)

            fcntl(fd, F_SETFL, flags | O_NONBLOCK);



        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {

            break;

        }

        if (errno == EINPROGRESS) {

            pollfd pfd;

            pfd.fd = fd;

            pfd.events = POLLOUT;

            const int pollMs = qMax(1, m_cfg.connectTimeoutMs);

            if (poll(&pfd, 1, pollMs) > 0) {

                int err = 0;

                socklen_t len = sizeof(err);

                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0)

                    break;

            }

        }

        ::close(fd);

        fd = -1;

    }

    freeaddrinfo(result);



    if (fd < 0) {

        qDebug() << "Redis 连接失败：" << m_cfg.host << ":" << m_cfg.port;

        return false;

    }



    const int blockFlags = fcntl(fd, F_GETFL, 0);

    if (blockFlags >= 0)

        fcntl(fd, F_SETFL, blockFlags & ~O_NONBLOCK);



    setSocketTimeout(fd, m_cfg.commandTimeoutMs);

    m_fd = fd;



    if (!authenticate() || !selectDatabase()) {

        resetSocket();

        return false;

    }



    m_ready = true;

    return true;

}



bool RedisClient::authenticate()

{

    if (m_cfg.password.isEmpty())

        return true;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("AUTH"),

        m_cfg.password.toUtf8()

    });

    if (!writeAll(cmd))

        return false;



    Reply reply;

    if (!readReply(&reply) || !reply.ok())

        return false;

    return true;

}



bool RedisClient::selectDatabase()

{

    if (m_cfg.database <= 0)

        return true;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("SELECT"),

        QByteArray::number(m_cfg.database)

    });

    if (!writeAll(cmd))

        return false;



    Reply reply;

    return readReply(&reply) && reply.ok();

}



QByteArray RedisClient::encodeCommand(const QList<QByteArray> &args) const

{

    QByteArray out;

    out.append('*');

    out.append(QByteArray::number(args.size()));

    out.append("\r\n");

    for (const QByteArray &arg : args) {

        out.append('$');

        out.append(QByteArray::number(arg.size()));

        out.append("\r\n");

        out.append(arg);

        out.append("\r\n");

    }

    return out;

}



bool RedisClient::writeAll(const QByteArray &data)

{

    if (m_fd < 0)

        return false;



    qint64 written = 0;

    while (written < data.size()) {

        const ssize_t n = ::send(m_fd, data.constData() + written,

                                 static_cast<size_t>(data.size() - written), MSG_NOSIGNAL);

        if (n < 0) {

            if (errno == EINTR)

                continue;

            return false;

        }

        written += n;

    }

    return true;

}



bool RedisClient::waitForReadable(int timeoutMs)

{

    if (m_fd < 0)

        return false;



    pollfd pfd;

    pfd.fd = m_fd;

    pfd.events = POLLIN;

    return poll(&pfd, 1, qMax(1, timeoutMs)) > 0 && (pfd.revents & POLLIN);

}



bool RedisClient::readLine(QByteArray *line)

{

    if (!line || m_fd < 0)

        return false;



    line->clear();

    char ch = 0;

    while (true) {

        const ssize_t n = ::recv(m_fd, &ch, 1, 0);

        if (n < 0) {

            if (errno == EINTR)

                continue;

            return false;

        }

        if (n == 0)

            return false;

        line->append(ch);

        if (line->size() >= 2
            && line->at(line->size() - 2) == '\r'
            && line->at(line->size() - 1) == '\n') {
            return true;
        }

    }

}



bool RedisClient::readReply(Reply *out)

{

    if (!out || m_fd < 0)

        return false;



    if (!waitForReadable(m_cfg.commandTimeoutMs))

        return false;



    char prefix = 0;

    const ssize_t n = ::recv(m_fd, &prefix, 1, MSG_PEEK);

    if (n <= 0)

        return false;



    out->type = ReplyNone;



    if (prefix == '+') {

        QByteArray line;

        if (!readLine(&line))

            return false;

        out->type = ReplyStatus;

        return true;

    }

    if (prefix == '-') {

        QByteArray line;

        if (!readLine(&line))

            return false;

        out->bulk = line;

        out->type = ReplyError;

        return false;

    }

    if (prefix == ':') {

        QByteArray line;

        if (!readLine(&line))

            return false;

        out->integer = line.mid(1).trimmed().toLongLong();

        out->type = ReplyInt;

        return true;

    }

    if (prefix == '$') {

        QByteArray line;

        if (!readLine(&line))

            return false;

        const int len = line.mid(1).trimmed().toInt();

        if (len < 0) {

            out->type = ReplyBulk;

            return true;

        }

        out->bulk.resize(len);

        qint64 got = 0;

        while (got < len) {

            if (!waitForReadable(m_cfg.commandTimeoutMs))

                return false;

            const ssize_t r = ::recv(m_fd, out->bulk.data() + got,

                                     static_cast<size_t>(len - got), 0);

            if (r <= 0)

                return false;

            got += r;

        }

        char crlf[2];

        if (::recv(m_fd, crlf, 2, 0) != 2)

            return false;

        out->type = ReplyBulk;

        return true;

    }

    if (prefix == '*') {

        QByteArray line;

        if (!readLine(&line))

            return false;

        const int count = line.mid(1).trimmed().toInt();

        out->type = ReplyArray;

        if (count < 0)

            return true;

        out->array.resize(count);

        for (int i = 0; i < count; ++i) {

            if (!readReply(&out->array[i]))

                return false;

        }

        return true;

    }

    return false;

}



bool RedisClient::ping()

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected())

        return false;

    const QByteArray cmd = encodeCommand({QByteArrayLiteral("PING")});

    if (!writeAll(cmd))

        return false;

    Reply reply;

    return readReply(&reply) && reply.ok();

}



bool RedisClient::set(const QString &key, const QString &value, int ttlSec)

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected())

        return false;



    QList<QByteArray> args;

    if (ttlSec > 0) {

        args << QByteArrayLiteral("SETEX")

             << key.toUtf8()

             << QByteArray::number(ttlSec)

             << value.toUtf8();

    } else {

        args << QByteArrayLiteral("SET")

             << key.toUtf8()

             << value.toUtf8();

    }



    if (!writeAll(encodeCommand(args)))

        return false;

    Reply reply;

    return readReply(&reply) && reply.ok();

}



bool RedisClient::get(const QString &key, QString *out)

{

    QMutexLocker lock(&m_mutex);

    if (!out || !ensureConnected())

        return false;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("GET"),

        key.toUtf8()

    });

    if (!writeAll(cmd))

        return false;



    Reply reply;

    if (!readReply(&reply) || !reply.ok())

        return false;

    if (reply.type == ReplyBulk && reply.bulk.isEmpty())

        return false;

    *out = QString::fromUtf8(reply.bulk);

    return true;

}



bool RedisClient::del(const QString &key)

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected())

        return false;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("DEL"),

        key.toUtf8()

    });

    if (!writeAll(cmd))

        return false;

    Reply reply;

    return readReply(&reply) && reply.ok();

}



bool RedisClient::hset(const QString &key, const QString &field, const QString &value)

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected())

        return false;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("HSET"),

        key.toUtf8(),

        field.toUtf8(),

        value.toUtf8()

    });

    if (!writeAll(cmd))

        return false;

    Reply reply;

    return readReply(&reply) && reply.ok();

}



bool RedisClient::hmset(const QString &key, const QMap<QString, QString> &fields)

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected() || fields.isEmpty())

        return false;



    QList<QByteArray> args;

    args << QByteArrayLiteral("HMSET") << key.toUtf8();

    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {

        args << it.key().toUtf8() << it.value().toUtf8();

    }

    if (!writeAll(encodeCommand(args)))

        return false;

    Reply reply;

    return readReply(&reply) && reply.ok();

}



bool RedisClient::hgetall(const QString &key, QMap<QString, QString> *out)

{

    QMutexLocker lock(&m_mutex);

    if (!out || !ensureConnected())

        return false;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("HGETALL"),

        key.toUtf8()

    });

    if (!writeAll(cmd))

        return false;



    Reply reply;

    if (!readReply(&reply) || !reply.ok())

        return false;

    if (reply.type != ReplyArray)

        return false;



    out->clear();

    for (int i = 0; i + 1 < reply.array.size(); i += 2) {

        const QString field = QString::fromUtf8(reply.array.at(i).bulk);

        const QString value = QString::fromUtf8(reply.array.at(i + 1).bulk);

        out->insert(field, value);

    }

    return true;

}



bool RedisClient::expire(const QString &key, int ttlSec)

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected() || ttlSec <= 0)

        return false;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("EXPIRE"),

        key.toUtf8(),

        QByteArray::number(ttlSec)

    });

    if (!writeAll(cmd))

        return false;

    Reply reply;

    return readReply(&reply) && reply.ok();

}



bool RedisClient::sadd(const QString &key, const QString &member)

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected())

        return false;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("SADD"),

        key.toUtf8(),

        member.toUtf8()

    });

    if (!writeAll(cmd))

        return false;

    Reply reply;

    return readReply(&reply) && reply.ok();

}



int RedisClient::scard(const QString &key)

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected())

        return -1;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("SCARD"),

        key.toUtf8()

    });

    if (!writeAll(cmd))

        return -1;

    Reply reply;

    if (!readReply(&reply) || !reply.ok())

        return -1;

    return static_cast<int>(reply.integer);

}



bool RedisClient::smembers(const QString &key, QStringList *out)

{

    QMutexLocker lock(&m_mutex);

    if (!out || !ensureConnected())

        return false;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("SMEMBERS"),

        key.toUtf8()

    });

    if (!writeAll(cmd))

        return false;



    Reply reply;

    if (!readReply(&reply) || !reply.ok() || reply.type != ReplyArray)

        return false;



    out->clear();

    for (const Reply &item : reply.array)

        out->append(QString::fromUtf8(item.bulk));

    return true;

}



bool RedisClient::srem(const QString &key, const QString &member)

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected())

        return false;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("SREM"),

        key.toUtf8(),

        member.toUtf8()

    });

    if (!writeAll(cmd))

        return false;

    Reply reply;

    return readReply(&reply) && reply.ok();

}



qint64 RedisClient::incr(const QString &key)

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected())

        return 0;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("INCR"),

        key.toUtf8()

    });

    if (!writeAll(cmd))

        return 0;

    Reply reply;

    if (!readReply(&reply) || !reply.ok())

        return 0;

    return reply.integer;

}



bool RedisClient::decrBy(const QString &key, qint64 delta)

{

    QMutexLocker lock(&m_mutex);

    if (!ensureConnected())

        return false;



    const QByteArray cmd = encodeCommand({

        QByteArrayLiteral("DECRBY"),

        key.toUtf8(),

        QByteArray::number(delta)

    });

    if (!writeAll(cmd))

        return false;

    Reply reply;

    return readReply(&reply) && reply.ok();

}


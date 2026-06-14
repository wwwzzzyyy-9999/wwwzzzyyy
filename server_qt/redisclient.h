#ifndef REDISCLIENT_H

#define REDISCLIENT_H



#include <QByteArray>

#include <QMap>

#include <QMutex>

#include <QString>

#include <QStringList>

#include <QVector>



// 使用 POSIX socket，避免 QTcpSocket 在线程池中的跨线程崩溃

class RedisClient

{

public:

    struct Config {

        QString host;

        int port = 6379;

        QString password;

        int database = 0;

        int connectTimeoutMs = 2000;

        int commandTimeoutMs = 3000;

    };



    RedisClient();

    ~RedisClient();



    bool open(const Config &cfg);

    void close();

    bool isOpen() const;



    bool ping();



    bool set(const QString &key, const QString &value, int ttlSec = 0);

    bool get(const QString &key, QString *out);

    bool del(const QString &key);



    bool hset(const QString &key, const QString &field, const QString &value);

    bool hmset(const QString &key, const QMap<QString, QString> &fields);

    bool hgetall(const QString &key, QMap<QString, QString> *out);



    bool expire(const QString &key, int ttlSec);



    bool sadd(const QString &key, const QString &member);

    int scard(const QString &key);

    bool smembers(const QString &key, QStringList *out);

    bool srem(const QString &key, const QString &member);



    qint64 incr(const QString &key);

    bool decrBy(const QString &key, qint64 delta);



private:

    enum ReplyType { ReplyNone, ReplyStatus, ReplyError, ReplyInt, ReplyBulk, ReplyArray };



    struct Reply {

        ReplyType type = ReplyNone;

        qint64 integer = 0;

        QByteArray bulk;

        QVector<Reply> array;

        bool ok() const { return type != ReplyNone && type != ReplyError; }

    };



    void resetSocket();

    bool ensureConnected();

    bool authenticate();

    bool selectDatabase();

    QByteArray encodeCommand(const QList<QByteArray> &args) const;

    bool writeAll(const QByteArray &data);

    bool readReply(Reply *out);

    bool readLine(QByteArray *line);

    bool waitForReadable(int timeoutMs);



    Config m_cfg;

    int m_fd = -1;

    mutable QMutex m_mutex;

    bool m_ready = false;

};



#endif // REDISCLIENT_H


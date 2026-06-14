#include "redisservice.h"
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>

RedisService::RedisService() = default;

bool RedisService::init(const RedisConfig &cfg)
{
    m_cfg = cfg;
    m_available = false;

    if (!cfg.enabled) {
        qDebug() << "Redis 已禁用（config.ini Redis/Enabled=false）";
        return false;
    }

    RedisClient::Config clientCfg;
    clientCfg.host = cfg.host;
    clientCfg.port = cfg.port;
    clientCfg.password = cfg.password;
    clientCfg.database = cfg.database;
    clientCfg.connectTimeoutMs = cfg.connectTimeoutMs;
    clientCfg.commandTimeoutMs = cfg.commandTimeoutMs;

    if (!m_client.open(clientCfg) || !m_client.ping()) {
        qDebug() << "Redis 不可用，将降级为无缓存模式";
        m_client.close();
        return false;
    }

    m_available = true;
    qDebug() << "Redis 已连接：" << cfg.host << ":" << cfg.port << "db=" << cfg.database;
    return true;
}

QString RedisService::sessionKey(const QString &token) const
{
    return QStringLiteral("session:token:%1").arg(token);
}

QString RedisService::uploadMetaKey(const QString &uploadId) const
{
    return QStringLiteral("upload:meta:%1").arg(uploadId);
}

QString RedisService::uploadChunksKey(const QString &uploadId) const
{
    return QStringLiteral("upload:chunks:%1").arg(uploadId);
}

bool RedisService::createSession(const QString &token, int userId,
                                 const QString &username, const QString &clientIp)
{
    if (!m_available)
        return false;

    QMap<QString, QString> fields;
    fields.insert(QStringLiteral("userId"), QString::number(userId));
    fields.insert(QStringLiteral("username"), username);
    fields.insert(QStringLiteral("clientIp"), clientIp);
    fields.insert(QStringLiteral("loginAt"),
                  QString::number(QDateTime::currentMSecsSinceEpoch()));

    const QString key = sessionKey(token);
    if (!m_client.hmset(key, fields))
        return false;
    m_client.expire(key, m_cfg.sessionTtlSec);
    m_client.sadd(QStringLiteral("session:user:%1").arg(userId), token);
    m_client.expire(QStringLiteral("session:user:%1").arg(userId), m_cfg.sessionTtlSec);
    return true;
}

bool RedisService::validateSession(const QString &token, int *userId, QString *username)
{
    if (!m_available || token.isEmpty())
        return false;

    QMap<QString, QString> fields;
    if (!m_client.hgetall(sessionKey(token), &fields) || fields.isEmpty())
        return false;

    if (userId)
        *userId = fields.value(QStringLiteral("userId")).toInt();
    if (username)
        *username = fields.value(QStringLiteral("username"));
    return fields.contains(QStringLiteral("username"));
}

bool RedisService::saveUploadMeta(const QString &uploadId, const QJsonObject &meta)
{
    if (!m_available)
        return false;
    const QByteArray json = QJsonDocument(meta).toJson(QJsonDocument::Compact);
    return m_client.set(uploadMetaKey(uploadId), QString::fromUtf8(json), m_cfg.uploadTtlSec);
}

bool RedisService::loadUploadMeta(const QString &uploadId, QJsonObject *meta)
{
    if (!m_available || !meta)
        return false;
    QString raw;
    if (!m_client.get(uploadMetaKey(uploadId), &raw))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject())
        return false;
    *meta = doc.object();
    return true;
}

bool RedisService::addUploadChunk(const QString &uploadId, int chunkIndex)
{
    if (!m_available)
        return false;
    const bool ok = m_client.sadd(uploadChunksKey(uploadId), QString::number(chunkIndex));
    if (ok)
        m_client.expire(uploadChunksKey(uploadId), m_cfg.uploadTtlSec);
    return ok;
}

bool RedisService::uploadChunksComplete(const QString &uploadId, int totalChunks,
                                        QSet<int> *received)
{
    if (!m_available || totalChunks <= 0)
        return false;

    QStringList members;
    if (!m_client.smembers(uploadChunksKey(uploadId), &members))
        return false;

    if (received) {
        received->clear();
        for (const QString &m : members) {
            bool ok = false;
            const int idx = m.toInt(&ok);
            if (ok && idx >= 0 && idx < totalChunks)
                received->insert(idx);
        }
    }
    return members.size() == totalChunks;
}

void RedisService::clearUploadSession(const QString &uploadId)
{
    if (!m_available)
        return;
    m_client.del(uploadMetaKey(uploadId));
    m_client.del(uploadChunksKey(uploadId));
}

bool RedisService::getCache(const QString &key, QByteArray *value)
{
    if (!m_available || !value)
        return false;
    QString raw;
    if (!m_client.get(key, &raw))
        return false;
    *value = raw.toUtf8();
    return !value->isEmpty();
}

bool RedisService::setCache(const QString &key, const QByteArray &value, int ttlSec)
{
    if (!m_available)
        return false;
    return m_client.set(key, QString::fromUtf8(value), ttlSec);
}

void RedisService::invalidateVideoCaches(int videoId)
{
    if (!m_available)
        return;

    if (videoId > 0)
        m_client.del(QStringLiteral("cache:video:%1").arg(videoId));

    for (int offset = 0; offset < 200; offset += 20) {
        for (int limit : {10, 20, 50}) {
            m_client.del(QStringLiteral("cache:video:list:%1:%2").arg(offset).arg(limit));
            m_client.del(QStringLiteral("cache:feed:%1:%2").arg(offset).arg(limit));
        }
    }
}

bool RedisService::getCachedSalt(const QString &username, QString *salt)
{
    if (!m_available || !salt)
        return false;
    return m_client.get(QStringLiteral("auth:salt:%1").arg(username), salt);
}

void RedisService::setCachedSalt(const QString &username, const QString &salt)
{
    if (!m_available)
        return;
    m_client.set(QStringLiteral("auth:salt:%1").arg(username), salt, m_cfg.saltTtlSec);
}

void RedisService::invalidateSalt(const QString &username)
{
    if (!m_available)
        return;
    m_client.del(QStringLiteral("auth:salt:%1").arg(username));
}

bool RedisService::incrementPlayCounter(int videoId)
{
    if (!m_available || videoId <= 0)
        return false;

    const QString counterKey = QStringLiteral("counter:play:%1").arg(videoId);
    const QString pendingKey = QStringLiteral("counter:play:pending");
    if (m_client.incr(counterKey) <= 0)
        return false;
    m_client.sadd(pendingKey, QString::number(videoId));
    return true;
}

QVector<QPair<int, int>> RedisService::drainPlayCounters()
{
    QVector<QPair<int, int>> deltas;
    if (!m_available)
        return deltas;

    const QString pendingKey = QStringLiteral("counter:play:pending");
    QStringList videoIds;
    if (!m_client.smembers(pendingKey, &videoIds))
        return deltas;

    for (const QString &idStr : videoIds) {
        const int videoId = idStr.toInt();
        if (videoId <= 0)
            continue;

        const QString counterKey = QStringLiteral("counter:play:%1").arg(videoId);
        QString value;
        if (!m_client.get(counterKey, &value))
            continue;

        bool ok = false;
        const int delta = value.toInt(&ok);
        if (!ok || delta <= 0)
            continue;

        deltas.append(qMakePair(videoId, delta));
        m_client.del(counterKey);
        m_client.srem(pendingKey, idStr);
    }
    return deltas;
}

bool RedisService::allowRequest(const QString &bucketKey, int limit, int windowSec)
{
    if (!m_available || limit <= 0 || windowSec <= 0)
        return true;

    const qint64 count = m_client.incr(bucketKey);
    if (count == 1)
        m_client.expire(bucketKey, windowSec);
    return count <= limit;
}

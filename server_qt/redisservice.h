#ifndef REDISSERVICE_H
#define REDISSERVICE_H

#include "redisclient.h"
#include "redisconfig.h"
#include <QJsonObject>
#include <QPair>
#include <QSet>
#include <QVector>

class RedisService
{
public:
    RedisService();

    bool init(const RedisConfig &cfg);
    bool available() const { return m_available; }
    const RedisConfig &config() const { return m_cfg; }

    bool createSession(const QString &token, int userId,
                       const QString &username, const QString &clientIp);
    bool validateSession(const QString &token, int *userId, QString *username);

    bool saveUploadMeta(const QString &uploadId, const QJsonObject &meta);
    bool loadUploadMeta(const QString &uploadId, QJsonObject *meta);
    bool addUploadChunk(const QString &uploadId, int chunkIndex);
    bool uploadChunksComplete(const QString &uploadId, int totalChunks, QSet<int> *received);
    void clearUploadSession(const QString &uploadId);

    bool getCache(const QString &key, QByteArray *value);
    bool setCache(const QString &key, const QByteArray &value, int ttlSec);
    void invalidateVideoCaches(int videoId = -1);

    bool getCachedSalt(const QString &username, QString *salt);
    void setCachedSalt(const QString &username, const QString &salt);
    void invalidateSalt(const QString &username);

    bool incrementPlayCounter(int videoId);
    QVector<QPair<int, int>> drainPlayCounters();

    bool allowRequest(const QString &bucketKey, int limit, int windowSec);

private:
    QString sessionKey(const QString &token) const;
    QString uploadMetaKey(const QString &uploadId) const;
    QString uploadChunksKey(const QString &uploadId) const;

    RedisConfig m_cfg;
    RedisClient m_client;
    bool m_available = false;
};

#endif // REDISSERVICE_H

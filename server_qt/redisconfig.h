#ifndef REDISCONFIG_H
#define REDISCONFIG_H

#include <QString>
#include <QCoreApplication>
#include <QSettings>
#include <QFile>

struct RedisConfig {
    QString host = QStringLiteral("127.0.0.1");
    int port = 6379;
    QString password;
    int database = 0;
    int poolSize = 4;
    int connectTimeoutMs = 2000;
    int commandTimeoutMs = 3000;
    bool enabled = true;

    int sessionTtlSec = 7 * 24 * 3600;
    int uploadTtlSec = 24 * 3600;
    int videoListTtlSec = 30;
    int feedTtlSec = 60;
    int videoInfoTtlSec = 300;
    int searchTtlSec = 60;
    int saltTtlSec = 3600;
    int counterFlushSec = 30;

    int loginRateLimit = 10;
    int loginRateWindowSec = 60;
    int uploadRateLimit = 20;
    int uploadRateWindowSec = 3600;
    int playRateLimit = 5;
    int playRateWindowSec = 10;
};

inline RedisConfig loadRedisConfig()
{
    RedisConfig cfg;
    QString configPath = QCoreApplication::applicationDirPath() + QStringLiteral("/config.ini");
    if (!QFile::exists(configPath))
        return cfg;

    QSettings settings(configPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Redis"));
    cfg.enabled = settings.value(QStringLiteral("Enabled"), true).toBool();
    cfg.host = settings.value(QStringLiteral("Host"), cfg.host).toString();
    cfg.port = settings.value(QStringLiteral("Port"), cfg.port).toInt();
    cfg.password = settings.value(QStringLiteral("Password"), cfg.password).toString();
    cfg.database = settings.value(QStringLiteral("Database"), cfg.database).toInt();
    cfg.poolSize = settings.value(QStringLiteral("PoolSize"), cfg.poolSize).toInt();
    cfg.connectTimeoutMs = settings.value(QStringLiteral("ConnectTimeoutMs"), cfg.connectTimeoutMs).toInt();
    cfg.commandTimeoutMs = settings.value(QStringLiteral("CommandTimeoutMs"), cfg.commandTimeoutMs).toInt();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("RedisCache"));
    cfg.sessionTtlSec = settings.value(QStringLiteral("SessionTTL"), cfg.sessionTtlSec).toInt();
    cfg.uploadTtlSec = settings.value(QStringLiteral("UploadTTL"), cfg.uploadTtlSec).toInt();
    cfg.videoListTtlSec = settings.value(QStringLiteral("VideoListTTL"), cfg.videoListTtlSec).toInt();
    cfg.feedTtlSec = settings.value(QStringLiteral("FeedTTL"), cfg.feedTtlSec).toInt();
    cfg.videoInfoTtlSec = settings.value(QStringLiteral("VideoInfoTTL"), cfg.videoInfoTtlSec).toInt();
    cfg.searchTtlSec = settings.value(QStringLiteral("SearchTTL"), cfg.searchTtlSec).toInt();
    cfg.saltTtlSec = settings.value(QStringLiteral("SaltTTL"), cfg.saltTtlSec).toInt();
    cfg.counterFlushSec = settings.value(QStringLiteral("CounterFlushSec"), cfg.counterFlushSec).toInt();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("RedisRateLimit"));
    cfg.loginRateLimit = settings.value(QStringLiteral("LoginLimit"), cfg.loginRateLimit).toInt();
    cfg.loginRateWindowSec = settings.value(QStringLiteral("LoginWindowSec"), cfg.loginRateWindowSec).toInt();
    cfg.uploadRateLimit = settings.value(QStringLiteral("UploadLimit"), cfg.uploadRateLimit).toInt();
    cfg.uploadRateWindowSec = settings.value(QStringLiteral("UploadWindowSec"), cfg.uploadRateWindowSec).toInt();
    cfg.playRateLimit = settings.value(QStringLiteral("PlayLimit"), cfg.playRateLimit).toInt();
    cfg.playRateWindowSec = settings.value(QStringLiteral("PlayWindowSec"), cfg.playRateWindowSec).toInt();
    settings.endGroup();

    return cfg;
}

#endif // REDISCONFIG_H

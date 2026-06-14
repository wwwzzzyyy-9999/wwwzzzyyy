#ifndef SERVER_H
#define SERVER_H

#include <QObject>
#include <QMap>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>
#include <functional>
#include <memory>
#include "database.h"
#include "protocol.h"
#include "epollserver.h"
#include "threadpool.h"
#include "redisservice.h"
#include "redisconfig.h"

class RedisService;

// ============================================================
// 客户端连接处理类
// 每个客户端连接对应一个 ClientHandler 实例
// 业务逻辑在线程池中执行，响应通过 EpollServer 异步写出
// ============================================================
class ClientHandler {
public:
    using SendFunc = std::function<void(const QByteArray &)>;

    ClientHandler(int fd, const QString &peerAddr, SendFunc sendFunc,
                  Database *db, RedisService *redis,
                  const QString &hlsIp, int hlsPort,
                  const QString &hlsOutputDir);
    ~ClientHandler();

    void processMessage(quint32 type, const QByteArray &jsonData);

    QString peerAddress() const { return m_peerAddr; }

private:
    bool resolveSession(const QJsonObject &json);
    bool ensureAuth(const QJsonObject &json, const QString &action);

    QJsonArray videosToJsonArray(const QVariantList &list) const;

    void handleRegister(const QJsonObject &json);
    void handleLogin(const QJsonObject &json);
    void handleGetPasswordSalt(const QJsonObject &json);

    void handleGetVideoList(const QJsonObject &json);
    void handleGetRecommendFeed(const QJsonObject &json);
    void handleSearchVideos(const QJsonObject &json);
    void handleGetVideoInfo(const QJsonObject &json);
    void handleIncrementPlay(const QJsonObject &json);
    void handleGetLikeStatus(const QJsonObject &json);
    void handleToggleLike(const QJsonObject &json);

    void handleUploadStart(const QJsonObject &json);
    void handleUploadChunk(const QJsonObject &json);
    void handleUploadEnd(const QJsonObject &json);
    void handleUploadResume(const QJsonObject &json);
    void handleDownloadStart(const QJsonObject &json);
    void handleDownloadChunk(const QJsonObject &json);

    void handleGetMyVideos(const QJsonObject &json);
    void handleDeleteVideo(const QJsonObject &json);

    void sendResponse(int code, const QString &message)
    { sendResponse(code, message, QString(), QJsonObject()); }
    void sendResponse(int code, const QString &message,
                      const QString &action, const QJsonObject &data = QJsonObject());

    int         m_fd;
    QString     m_peerAddr;
    SendFunc    m_sendFunc;
    Database   *m_db;
    RedisService *m_redis;
    QString     m_username;
    int         m_userId = 0;
    QString     m_hlsIp;
    int         m_hlsPort;
    QString     m_hlsOutputDir;
};

class Server : public QObject
{
    Q_OBJECT

public:
    explicit Server(const QString &hlsIp, int hlsPort,
                    const QString &hlsOutputDir,
                    const RedisConfig &redisCfg = RedisConfig(),
                    QObject *parent = nullptr);
    ~Server();

    bool start(quint16 port);
    void stop();

private slots:
    void onCleanupTimer();
    void onCounterFlushTimer();

private:
    std::shared_ptr<ClientHandler> createClientHandler(int fd, const QString &peerAddr);
    void onClientDisconnected(int fd, const std::shared_ptr<ClientHandler> &handler);

    ThreadPool    m_threadPool;
    EpollServer   m_epollServer;
    Database      m_database;
    RedisService  m_redis;
    RedisConfig   m_redisCfg;
    QMap<int, std::shared_ptr<ClientHandler>> m_clients;
    QTimer        m_cleanupTimer;
    QTimer        m_counterFlushTimer;
    QString       m_hlsIp;
    int           m_hlsPort;
    QString       m_hlsOutputDir;
};

#endif // SERVER_H

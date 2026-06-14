#ifndef EPOLLSERVER_H
#define EPOLLSERVER_H

#include <QByteArray>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "threadpool.h"

class ClientHandler;

// epoll I/O 多路复用 + 线程池模型：
// - 专用 I/O 线程负责 accept / read / write
// - 完整消息帧解析后投递到 ThreadPool 执行业务逻辑
// - 每连接串行处理消息，保证会话状态一致
class EpollServer {
public:
    using NewConnectionCallback = std::function<std::shared_ptr<ClientHandler>(int fd, const QString &peerAddr)>;
    using DisconnectedCallback  = std::function<void(int fd, const std::shared_ptr<ClientHandler> &handler)>;

    explicit EpollServer(ThreadPool *pool);
    ~EpollServer();

    bool listen(quint16 port, int backlog = 128);
    void stop();

    void setNewConnectionCallback(NewConnectionCallback cb) { m_onNewConnection = std::move(cb); }
    void setDisconnectedCallback(DisconnectedCallback cb) { m_onDisconnected = std::move(cb); }

    // 线程安全：将响应数据加入发送队列，由 I/O 线程在 EPOLLOUT 时写出
    void enqueueSend(int fd, const QByteArray &data);

    bool isListening() const { return m_listenFd >= 0; }

private:
    struct Connection;

    void ioLoop();
    void handleAccept();
    void handleRead(int fd);
    void handleWrite(int fd);
    void closeConnection(int fd);
    void submitNextMessage(Connection &conn);
    void wakeIoThread();
    void drainWakePipe();
    void applyWriteInterest();
    void scanPendingMessages();

    static bool tryParseFrame(QByteArray &buffer, quint32 &type, QByteArray &payload);

    ThreadPool *m_pool;
    int m_listenFd;
    int m_epollFd;
    int m_wakePipe[2];
    std::atomic<bool> m_running;
    std::thread m_ioThread;

    std::mutex m_connMutex;
    std::unordered_map<int, std::unique_ptr<Connection>> m_connections;

    NewConnectionCallback m_onNewConnection;
    DisconnectedCallback m_onDisconnected;
};

#endif // EPOLLSERVER_H

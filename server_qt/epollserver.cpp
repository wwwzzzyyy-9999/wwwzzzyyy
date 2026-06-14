#define _GNU_SOURCE
#include "epollserver.h"
#include "server.h"

#include <QDebug>
#include <QtEndian>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr int kMaxEpollEvents = 128;
constexpr int kReadChunkSize  = 8192;

bool setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool setCloseOnExec(int fd)
{
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0)
        return false;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

QString peerAddress(int fd)
{
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getpeername(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
        return QStringLiteral("unknown");

    char host[INET6_ADDRSTRLEN] = {};
    quint16 port = 0;
    if (addr.ss_family == AF_INET) {
        const auto *in = reinterpret_cast<const sockaddr_in *>(&addr);
        inet_ntop(AF_INET, &in->sin_addr, host, sizeof(host));
        port = ntohs(in->sin_port);
    } else if (addr.ss_family == AF_INET6) {
        const auto *in6 = reinterpret_cast<const sockaddr_in6 *>(&addr);
        inet_ntop(AF_INET6, &in6->sin6_addr, host, sizeof(host));
        port = ntohs(in6->sin6_port);
    }
    return QStringLiteral("%1:%2").arg(QString::fromLatin1(host)).arg(port);
}

} // namespace

struct EpollServer::Connection {
    int fd = -1;
    QString peerAddr;
    QByteArray readBuf;
    QByteArray sendBuf;
    std::mutex sendMutex;
    bool wantWrite = false;
    bool processing = false;
    bool closed = false;
    std::shared_ptr<ClientHandler> handler;
};

EpollServer::EpollServer(ThreadPool *pool)
    : m_pool(pool), m_listenFd(-1), m_epollFd(-1), m_wakePipe{-1, -1}, m_running(false)
{
}

EpollServer::~EpollServer()
{
    stop();
}

bool EpollServer::listen(quint16 port, int backlog)
{
    if (m_listenFd >= 0)
        return true;

    m_listenFd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (m_listenFd < 0) {
        qDebug() << "创建监听套接字失败：" << strerror(errno);
        return false;
    }

    int reuse = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (!setNonBlocking(m_listenFd)) {
        qDebug() << "设置监听套接字非阻塞失败";
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(m_listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        qDebug() << "TCP 端口" << port << "绑定失败：" << strerror(errno);
        qDebug() << "常见原因：已有 VideoRecorderServer 在运行，或端口被占用。";
        qDebug() << "可执行：pkill VideoRecorderServer  或  fuser -k" << port << "/tcp";
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    if (::listen(m_listenFd, backlog) != 0) {
        qDebug() << "listen 失败：" << strerror(errno);
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    m_epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0) {
        qDebug() << "epoll_create1 失败：" << strerror(errno);
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    if (pipe(m_wakePipe) != 0) {
        qDebug() << "创建唤醒管道失败：" << strerror(errno);
        ::close(m_epollFd);
        m_epollFd = -1;
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    setNonBlocking(m_wakePipe[0]);
    setNonBlocking(m_wakePipe[1]);
    setCloseOnExec(m_wakePipe[0]);
    setCloseOnExec(m_wakePipe[1]);

    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = m_listenFd;
    if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_listenFd, &ev) != 0) {
        qDebug() << "注册监听 fd 到 epoll 失败：" << strerror(errno);
        stop();
        return false;
    }

    ev.data.fd = m_wakePipe[0];
    if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_wakePipe[0], &ev) != 0) {
        qDebug() << "注册唤醒管道到 epoll 失败：" << strerror(errno);
        stop();
        return false;
    }

    m_running = true;
    m_ioThread = std::thread([this]() { ioLoop(); });
    return true;
}

void EpollServer::stop()
{
    m_running = false;
    wakeIoThread();

    if (m_ioThread.joinable())
        m_ioThread.join();

    {
        std::lock_guard<std::mutex> lock(m_connMutex);
        m_connections.clear();
    }

    if (m_epollFd >= 0) {
        ::close(m_epollFd);
        m_epollFd = -1;
    }
    if (m_listenFd >= 0) {
        ::close(m_listenFd);
        m_listenFd = -1;
    }
    if (m_wakePipe[0] >= 0) {
        ::close(m_wakePipe[0]);
        m_wakePipe[0] = -1;
    }
    if (m_wakePipe[1] >= 0) {
        ::close(m_wakePipe[1]);
        m_wakePipe[1] = -1;
    }
}

void EpollServer::enqueueSend(int fd, const QByteArray &data)
{
    bool needWake = false;
    {
        std::lock_guard<std::mutex> lock(m_connMutex);
        auto it = m_connections.find(fd);
        if (it == m_connections.end() || it->second->closed)
            return;

        Connection &conn = *it->second;
        std::lock_guard<std::mutex> sendLock(conn.sendMutex);
        conn.sendBuf.append(data);
        if (!conn.wantWrite) {
            conn.wantWrite = true;
            needWake = true;
        }
    }

    if (needWake)
        wakeIoThread();
}

bool EpollServer::tryParseFrame(QByteArray &buffer, quint32 &type, QByteArray &payload)
{
    if (buffer.size() < 8)
        return false;

    quint32 netType = 0;
    quint32 netLen  = 0;
    std::memcpy(&netType, buffer.constData(), 4);
    std::memcpy(&netLen, buffer.constData() + 4, 4);
    type = qFromBigEndian(netType);
    const quint32 length = qFromBigEndian(netLen);

    if (static_cast<quint32>(buffer.size()) < 8 + length)
        return false;

    payload = buffer.mid(8, static_cast<int>(length));
    buffer.remove(0, 8 + static_cast<int>(length));
    return true;
}

void EpollServer::wakeIoThread()
{
    if (m_wakePipe[1] < 0)
        return;
    const char ch = 1;
    ssize_t n = ::write(m_wakePipe[1], &ch, 1);
    (void)n;
}

void EpollServer::drainWakePipe()
{
    char buf[64];
    while (::read(m_wakePipe[0], buf, sizeof(buf)) > 0) {}
}

void EpollServer::applyWriteInterest()
{
    std::lock_guard<std::mutex> lock(m_connMutex);
    for (const auto &pair : m_connections) {
        const Connection &conn = *pair.second;
        if (conn.closed || !conn.wantWrite)
            continue;

        epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.fd = conn.fd;
        epoll_ctl(m_epollFd, EPOLL_CTL_MOD, conn.fd, &ev);
    }
}

void EpollServer::submitNextMessage(Connection &conn)
{
    if (conn.processing || conn.closed || !conn.handler)
        return;

    quint32 type = 0;
    QByteArray payload;
    if (!tryParseFrame(conn.readBuf, type, payload))
        return;

    conn.processing = true;
    const int fd = conn.fd;
    std::shared_ptr<ClientHandler> handler = conn.handler;

    m_pool->submit([this, handler, type, payload, fd]() {
        if (handler)
            handler->processMessage(type, payload);

        {
            std::lock_guard<std::mutex> lock(m_connMutex);
            auto it = m_connections.find(fd);
            if (it != m_connections.end())
                it->second->processing = false;
        }
        wakeIoThread();
    });
}

void EpollServer::scanPendingMessages()
{
    std::lock_guard<std::mutex> lock(m_connMutex);
    for (auto &pair : m_connections) {
        if (pair.second)
            submitNextMessage(*pair.second);
    }
}

void EpollServer::handleAccept()
{
    while (m_running) {
        const int clientFd = ::accept4(m_listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            qDebug() << "accept 失败：" << strerror(errno);
            break;
        }

        const QString peer = peerAddress(clientFd);
        std::shared_ptr<ClientHandler> handler;
        if (m_onNewConnection)
            handler = m_onNewConnection(clientFd, peer);
        if (!handler) {
            ::close(clientFd);
            continue;
        }

        auto conn = std::unique_ptr<Connection>(new Connection);
        conn->fd = clientFd;
        conn->peerAddr = peer;
        conn->handler = handler;

        epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = clientFd;
        if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, clientFd, &ev) != 0) {
            qDebug() << "注册客户端 fd 到 epoll 失败：" << strerror(errno);
            ::close(clientFd);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_connMutex);
            m_connections[clientFd] = std::move(conn);
        }

        qDebug() << "新客户端连接：" << peer;
    }
}

void EpollServer::handleRead(int fd)
{
    Connection *conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_connMutex);
        auto it = m_connections.find(fd);
        if (it == m_connections.end() || it->second->closed)
            return;
        conn = it->second.get();
    }

    char buf[kReadChunkSize];
    while (m_running) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            conn->readBuf.append(buf, static_cast<int>(n));
        } else if (n == 0) {
            closeConnection(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            qDebug() << "读取客户端数据失败：" << strerror(errno);
            closeConnection(fd);
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_connMutex);
        auto it = m_connections.find(fd);
        if (it != m_connections.end() && !it->second->closed)
            submitNextMessage(*it->second);
    }
}

void EpollServer::handleWrite(int fd)
{
    Connection *conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_connMutex);
        auto it = m_connections.find(fd);
        if (it == m_connections.end() || it->second->closed)
            return;
        conn = it->second.get();
    }

    std::lock_guard<std::mutex> sendLock(conn->sendMutex);
    while (!conn->sendBuf.isEmpty()) {
        const ssize_t n = ::send(fd, conn->sendBuf.constData(),
                                 static_cast<size_t>(conn->sendBuf.size()), MSG_NOSIGNAL);
        if (n > 0) {
            conn->sendBuf.remove(0, static_cast<int>(n));
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            qDebug() << "发送数据失败：" << strerror(errno);
            closeConnection(fd);
            return;
        }
    }

    if (conn->sendBuf.isEmpty() && conn->wantWrite) {
        conn->wantWrite = false;
        epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fd;
        epoll_ctl(m_epollFd, EPOLL_CTL_MOD, fd, &ev);
    }
}

void EpollServer::closeConnection(int fd)
{
    std::shared_ptr<ClientHandler> handler;
    {
        std::lock_guard<std::mutex> lock(m_connMutex);
        auto it = m_connections.find(fd);
        if (it == m_connections.end())
            return;
        it->second->closed = true;
        handler = it->second->handler;
        m_connections.erase(it);
    }

    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);

    if (handler) {
        qDebug() << "客户端断开：" << handler->peerAddress();
        if (m_onDisconnected)
            m_onDisconnected(fd, handler);
    }
}

void EpollServer::ioLoop()
{
    epoll_event events[kMaxEpollEvents];
    while (m_running) {
        const int n = epoll_wait(m_epollFd, events, kMaxEpollEvents, 500);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (!m_running)
                break;
            qDebug() << "epoll_wait 失败：" << strerror(errno);
            break;
        }

        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;

            if (fd == m_listenFd) {
                if (events[i].events & EPOLLIN)
                    handleAccept();
                continue;
            }

            if (fd == m_wakePipe[0]) {
                if (events[i].events & EPOLLIN) {
                    drainWakePipe();
                    applyWriteInterest();
                    scanPendingMessages();
                }
                continue;
            }

            bool connAlive = false;
            {
                std::lock_guard<std::mutex> lock(m_connMutex);
                auto it = m_connections.find(fd);
                connAlive = (it != m_connections.end() && !it->second->closed);
            }
            if (!connAlive)
                continue;

            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                closeConnection(fd);
                continue;
            }
            if (events[i].events & EPOLLIN)
                handleRead(fd);
            if (events[i].events & EPOLLOUT)
                handleWrite(fd);
        }
    }
}

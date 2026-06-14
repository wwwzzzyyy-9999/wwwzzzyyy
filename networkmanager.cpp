#include "networkmanager.h"
#include "auth_util.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QCoreApplication>
#include <QFile>          // 文件操作
#include <QFileInfo>      // 文件信息
#include <QDir>           // 目录操作
#include <QNetworkProxy>
#include <QtEndian>       // 字节序转换（qFromBigEndian）

// 兼容旧服务端：message 为 "密码错误|login" 且无 action 字段
static void normalizeResponseMessage(NetworkMessage &msg)
{
    if (msg.action.isEmpty() && msg.data.contains(QLatin1Char('|'))) {
        const int sep = msg.data.lastIndexOf(QLatin1Char('|'));
        const QString tail = msg.data.mid(sep + 1).trimmed();
        static const QStringList knownActions = {
            QStringLiteral("login"), QStringLiteral("register"),
            QStringLiteral("upload_start"), QStringLiteral("upload_chunk"),
            QStringLiteral("upload_end"), QStringLiteral("upload_resume"),
            QStringLiteral("video_list"), QStringLiteral("recommend_feed"),
            QStringLiteral("video_info"),
            QStringLiteral("download_start"), QStringLiteral("download_chunk"),
            QStringLiteral("my_videos"), QStringLiteral("delete_video"),
            QStringLiteral("get_like_status"), QStringLiteral("toggle_like"),
            QStringLiteral("search_videos"),
            QStringLiteral("password_salt")
        };
        if (knownActions.contains(tail)) {
            msg.action = tail;
            msg.data = msg.data.left(sep);
        }
    }

    if (!msg.action.isEmpty())
        return;

    if (msg.code == RESP_PASSWORD_ERROR || msg.code == RESP_USER_NOT_FOUND)
        msg.action = QStringLiteral("login");
    else if (msg.code == RESP_USER_EXISTS)
        msg.action = QStringLiteral("register");
    else if (msg.code == RESP_SUCCESS && msg.respData.contains(QStringLiteral("token")))
        msg.action = QStringLiteral("login");
}

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_serverPort(8888)
    , m_isLoggedIn(false)
    , m_token("")
    , m_loggedInUsername("")
    // 【P1修复】初始化进度跟踪变量
    , m_currentUploadId("")
    , m_currentUploadFileSize(0)
    , m_currentUploadChunkSize(DEFAULT_CHUNK_SIZE)
    , m_currentUploadTotalChunks(0)
    , m_currentUploadSentChunks(0)
    , m_currentVideoId(-1)
    , m_currentDownloadFileSize(0)
    , m_currentDownloadTotalChunks(0)
    , m_currentDownloadReceivedChunks(0)
    , m_requestSerial(0)
{
    // TCP 直连不走代理；系统代理会导致 "The proxy type is invalid for this operation"
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    m_socket = new QTcpSocket(this);
    
    connect(m_socket, &QTcpSocket::connected, this, &NetworkManager::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &NetworkManager::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &NetworkManager::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &NetworkManager::onError);
    
    // 加载配置文件
    loadConfig();
}

NetworkManager::~NetworkManager()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }
}

void NetworkManager::loadConfig()
{
    // 优先查找当前工作目录（方便开发调试），其次查找可执行文件目录
    QString configPath = QDir::currentPath() + "/config.ini";
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    }
    
    // 如果配置文件不存在，创建一个默认的
    if (!QFile::exists(configPath)) {
        qDebug() << "配置文件不存在，创建默认配置：" << configPath;
        QSettings settings(configPath, QSettings::IniFormat);
        settings.beginGroup("Server");
        settings.setValue("IP", "127.0.0.1");
        settings.setValue("Port", 8888);
        settings.endGroup();
        settings.sync();
    }
    
    QSettings settings(configPath, QSettings::IniFormat);
    settings.beginGroup("Server");
    m_serverIp = settings.value("IP", "127.0.0.1").toString();
    m_serverPort = static_cast<quint16>(settings.value("Port", 8888).toUInt());
    settings.endGroup();
    
    qDebug() << "加载服务器配置（" << configPath << "）：" << m_serverIp << ":" << m_serverPort;
}

void NetworkManager::connectToServer()
{
    // 如果已经连接或正在连接，不要重复调用
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        emit SIG_connected();
        return;
    }
    if (m_socket->state() == QAbstractSocket::ConnectingState) {
        qDebug() << "连接正在进行中...";
        return;
    }

    qDebug() << "正在连接服务器：" << m_serverIp << ":" << m_serverPort;
    m_socket->connectToHost(m_serverIp, m_serverPort);
}

QString NetworkManager::getServerIp() const
{
    return m_serverIp;
}

quint16 NetworkManager::getServerPort() const
{
    return m_serverPort;
}

bool NetworkManager::isLoggedIn() const
{
    return m_isLoggedIn;
}

bool NetworkManager::isSocketConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void NetworkManager::setToken(const QString& token)
{
    m_token = token;
    m_isLoggedIn = !token.isEmpty() || !m_loggedInUsername.isEmpty();
}

QString NetworkManager::getToken() const
{
    return m_token;
}

void NetworkManager::sendRegister(const QString& username, const QString& password)
{
    const QString salt = generatePasswordSalt();
    QJsonObject json;
    json["username"] = username;
    json["salt"] = salt;
    json["password"] = authPasswordHash(password, salt);

    NetworkMessage msg;
    msg.type = MSG_REGISTER;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

void NetworkManager::sendGetPasswordSalt(const QString& username)
{
    QJsonObject json;
    json["username"] = username;

    NetworkMessage msg;
    msg.type = MSG_GET_PASSWORD_SALT;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

void NetworkManager::sendLoginHash(const QString& username, const QString& passwordHash)
{
    QJsonObject json;
    json["username"] = username;
    json["password"] = passwordHash;

    NetworkMessage msg;
    msg.type = MSG_LOGIN;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

void NetworkManager::sendLogin(const QString& username, const QString& password)
{
    m_pendingLoginUsername = username;
    m_pendingLoginPlainPassword = password;
    sendGetPasswordSalt(username);
}

void NetworkManager::sendUploadStart(const QString& fileName, qint64 fileSize, int totalChunks)
{
    // 【P1修复】记录上传信息用于进度跟踪
    m_currentUploadFileSize  = fileSize;
    m_currentUploadTotalChunks = totalChunks;
    m_currentUploadChunkSize  = DEFAULT_CHUNK_SIZE;
    m_currentUploadSentChunks = 0;

    QJsonObject json;
    json["fileName"]   = fileName;
    json["fileSize"]   = fileSize;
    json["totalChunks"] = totalChunks;

    NetworkMessage msg;
    msg.type = MSG_UPLOAD_START;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

// ============================================================
// 分块上传：发送一个数据块
// JSON: {"uploadId": 1, "chunkIndex": 0, "totalChunks": 10, "chunkData": "<base64>"}
// ============================================================
void NetworkManager::sendUploadChunk(const QString& uploadId, int chunkIndex, int totalChunks, const QByteArray& chunkData)
{
    QString chunkBase64 = QString::fromLatin1(chunkData.toBase64());

    QJsonObject json;
    json["uploadId"]    = uploadId;
    json["chunkIndex"]  = chunkIndex;
    json["totalChunks"] = totalChunks;
    json["chunkData"]   = chunkBase64;

    NetworkMessage msg;
    msg.type = MSG_UPLOAD_CHUNK;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

// ============================================================
// 分块上传：通知服务端上传完成，可以合并块
// JSON: {"uploadId": 1, "title": "...", "coverUrl": "...", "duration": 120, "fileHash": "..."}
// ============================================================
void NetworkManager::sendUploadEnd(const QString& uploadId, const QString& fileHash)
{
    QJsonObject json;
    json["uploadId"] = uploadId;
    json["fileHash"] = fileHash;  // 【P2修复】发送文件 SHA-256 哈希

    NetworkMessage msg;
    msg.type = MSG_UPLOAD_END;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

// ============================================================
// 分块上传：请求续传（查询已接收的块）
// JSON: {"uploadId": 1}
// ============================================================
void NetworkManager::sendUploadResume(const QString& uploadId)
{
    QJsonObject json;
    json["uploadId"] = uploadId;

    NetworkMessage msg;
    msg.type = MSG_UPLOAD_RESUME;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

// ============================================================
// 【P2修复】静态辅助函数：计算文件的 SHA-256 哈希
// 参数：filePath = 文件路径
// 返回：SHA-256 哈希字符串（十六进制），失败返回空字符串
// ============================================================
QString NetworkManager::calculateFileHash(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "无法打开文件计算哈希：" << filePath;
        return "";
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        qDebug() << "计算文件哈希失败：" << filePath;
        file.close();
        return "";
    }

    file.close();
    return QString(hash.result().toHex());
}

// ============================================================
// 分块下载：请求开始下载
// JSON: {"videoId": 1, "chunkSize": 32768, "startChunkIndex": 0}
// ============================================================
void NetworkManager::sendDownloadStart(int videoId, int startChunkIndex)
{
    QJsonObject json;
    json["videoId"]         = videoId;
    json["chunkSize"]        = DEFAULT_CHUNK_SIZE;  // 【P0修复】每块 32KB
    json["startChunkIndex"] = startChunkIndex;   // 【P1修复】支持断点续传

    NetworkMessage msg;
    msg.type = MSG_DOWNLOAD_START;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

// ============================================================
// 分块下载：确认收到某块（请求下一块）
// JSON: {"videoId": 1, "chunkIndex": 0}
// ============================================================
void NetworkManager::sendDownloadChunkAck(int videoId, int chunkIndex)
{
    QJsonObject json;
    json["videoId"]    = videoId;
    json["chunkIndex"] = chunkIndex;

    NetworkMessage msg;
    msg.type = MSG_DOWNLOAD_CHUNK;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

quint64 NetworkManager::nextListRequestId(QQueue<quint64> &queue)
{
    const quint64 id = ++m_requestSerial;
    queue.enqueue(id);
    return id;
}

quint64 NetworkManager::sendGetVideoList(int offset, int limit)
{
    QJsonObject json;
    json["offset"] = offset;
    json["limit"]  = limit;

    NetworkMessage msg;
    msg.type = MSG_GET_VIDEO_LIST;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    const quint64 requestId = nextListRequestId(m_videoListRequestQueue);
    sendMessage(msg);
    return requestId;
}

quint64 NetworkManager::sendGetRecommendFeed(int offset, int limit)
{
    QJsonObject json;
    json["offset"] = offset;
    json["limit"]  = limit;

    NetworkMessage msg;
    msg.type = MSG_GET_RECOMMEND_FEED;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    const quint64 requestId = nextListRequestId(m_recommendFeedRequestQueue);
    sendMessage(msg);
    return requestId;
}

quint64 NetworkManager::sendSearchVideos(const QString &keyword, int offset, int limit)
{
    QJsonObject json;
    json["keyword"] = keyword;
    json["offset"] = offset;
    json["limit"]  = limit;

    NetworkMessage msg;
    msg.type = MSG_SEARCH_VIDEOS;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    const quint64 requestId = nextListRequestId(m_searchVideosRequestQueue);
    sendMessage(msg);
    return requestId;
}

void NetworkManager::sendGetVideoInfo(int videoId)
{
    QJsonObject json;
    json["videoId"] = videoId;

    NetworkMessage msg;
    msg.type = MSG_GET_VIDEO_INFO;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

void NetworkManager::sendIncrementPlay(int videoId)
{
    QJsonObject json;
    json["videoId"] = videoId;

    NetworkMessage msg;
    msg.type = MSG_INCREMENT_PLAY;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);

    sendMessage(msg);
}

void NetworkManager::sendGetLikeStatus(const QList<int> &videoIds)
{
    QJsonArray arr;
    for (int id : videoIds)
        arr.append(id);

    QJsonObject json;
    json["videoIds"] = arr;

    NetworkMessage msg;
    msg.type = MSG_GET_LIKE_STATUS;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    sendMessage(msg);
}

void NetworkManager::sendToggleLike(int videoId)
{
    QJsonObject json;
    json["videoId"] = videoId;

    NetworkMessage msg;
    msg.type = MSG_TOGGLE_LIKE;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    sendMessage(msg);
}

void NetworkManager::sendGetMyVideos()
{
    NetworkMessage msg;
    msg.type = MSG_GET_MY_VIDEOS;
    msg.data = "{}";
    sendMessage(msg);
}

void NetworkManager::sendDeleteVideo(int videoId)
{
    QJsonObject json;
    json["videoId"] = videoId;
    NetworkMessage msg;
    msg.type = MSG_DELETE_VIDEO;
    msg.data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    sendMessage(msg);
}

void NetworkManager::sendMessage(const NetworkMessage& msg)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        emit SIG_error("未连接到服务器");
        return;
    }

    // 构造消息：[type 4B 网络字节序][length 4B 网络字节序][JSON 数据]
    // 发送前必须转成网络字节序，服务端用 qFromBigEndian 解析
    QByteArray data = msg.data.toUtf8();

    if (m_isLoggedIn) {
        switch (msg.type) {
        case MSG_REGISTER:
        case MSG_LOGIN:
        case MSG_GET_PASSWORD_SALT:
            break;
        default: {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(data, &err);
            QJsonObject obj = (err.error == QJsonParseError::NoError && doc.isObject())
                ? doc.object() : QJsonObject();
            if (!m_token.isEmpty() && !obj.contains(QStringLiteral("token")))
                obj[QStringLiteral("token")] = m_token;
            if (!m_loggedInUsername.isEmpty() && !obj.contains(QStringLiteral("username")))
                obj[QStringLiteral("username")] = m_loggedInUsername;
            data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
            break;
        }
        }
    }
    quint32 netType   = qToBigEndian(static_cast<quint32>(msg.type));
    quint32 netLength = qToBigEndian(static_cast<quint32>(data.size()));

    QByteArray message;
    message.append(reinterpret_cast<const char*>(&netType),   sizeof(quint32));
    message.append(reinterpret_cast<const char*>(&netLength), sizeof(quint32));
    message.append(data);

    m_socket->write(message);
}

NetworkMessage NetworkManager::parseMessage(const QByteArray& data)
{
    NetworkMessage msg;

    if (data.size() < 8) {
        return msg;
    }

    // 解析消息头和消息体（注意：服务端发送的是网络字节序，需要转换）
    quint32 type;
    quint32 length;
    memcpy(&type,   data.constData(),             sizeof(quint32));
    memcpy(&length, data.constData() + sizeof(quint32), sizeof(quint32));
    type   = qFromBigEndian(type);    // ← 网络字节序转主机字节序
    length = qFromBigEndian(length);

    msg.type = static_cast<MsgType>(type);

    if (data.size() >= static_cast<int>(8 + length)) {
        QByteArray jsonData = data.mid(8, length);
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);

        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            msg.code = static_cast<ResponseCode>(obj["code"].toInt());
            msg.data = obj["message"].toString();
            msg.action = obj["action"].toString();

            // 提取响应的 data 字段（可能包含 videoData 等）
            if (obj.contains("data") && obj["data"].isObject()) {
                msg.respData = obj["data"].toObject();
            }
        }
    }

    normalizeResponseMessage(msg);
    return msg;
}

void NetworkManager::dispatchResponse(const NetworkMessage& msg)
{
    const bool success = (msg.code == RESP_SUCCESS);
    const QString action = msg.action;
    const QString cleanData = msg.data;

    if (action == "register") {
        emit SIG_registerResult(success, cleanData);
    } else if (action == "password_salt") {
        if (m_pendingLoginPlainPassword.isEmpty())
            return;
        const QString username = m_pendingLoginUsername;
        const QString plainPassword = m_pendingLoginPlainPassword;
        m_pendingLoginPlainPassword.clear();
        m_pendingLoginUsername.clear();

        if (!success) {
            emit SIG_loginResult(false, cleanData, QString());
            return;
        }

        const QString salt = msg.respData.value(QStringLiteral("salt")).toString();
        const QString passwordHash = salt.isEmpty()
            ? authLegacyPasswordHash(plainPassword)
            : authPasswordHash(plainPassword, salt);
        sendLoginHash(username, passwordHash);
    } else if (action == "login") {
        QString token;
        if (success && msg.respData.contains("token"))
            token = msg.respData["token"].toString();
        if (success) {
            m_isLoggedIn = true;
            m_token = token;
            m_loggedInUsername = msg.respData.value(QStringLiteral("username")).toString();
        } else {
            m_isLoggedIn = false;
            m_token.clear();
            m_loggedInUsername.clear();
        }
        emit SIG_loginResult(success, cleanData, token);
    } else if (action == "video_list") {
        QJsonArray videos;
        if (success && msg.respData.contains("videos"))
            videos = msg.respData["videos"].toArray();
        quint64 requestId = 0;
        if (!m_videoListRequestQueue.isEmpty())
            requestId = m_videoListRequestQueue.dequeue();
        emit SIG_videoListResult(success, msg.data, videos, requestId);
    } else if (action == "recommend_feed") {
        QJsonArray videos;
        if (success && msg.respData.contains("videos"))
            videos = msg.respData["videos"].toArray();
        quint64 requestId = 0;
        if (!m_recommendFeedRequestQueue.isEmpty())
            requestId = m_recommendFeedRequestQueue.dequeue();
        emit SIG_recommendFeedResult(success, msg.data, videos, requestId);
    } else if (action == "search_videos") {
        QJsonArray videos;
        if (success && msg.respData.contains("videos"))
            videos = msg.respData["videos"].toArray();
        const QString keyword = msg.respData.value("keyword").toString();
        quint64 requestId = 0;
        if (!m_searchVideosRequestQueue.isEmpty())
            requestId = m_searchVideosRequestQueue.dequeue();
        emit SIG_searchVideosResult(success, msg.data, videos, keyword, requestId);
    } else if (action == "video_info") {
        QJsonObject info = success ? msg.respData : QJsonObject();
        emit SIG_videoInfoResult(success, msg.data, info);
    } else if (action == "increment_play") {
        emit SIG_incrementPlayResult(success, msg.data);
    } else if (action == "get_like_status") {
        QList<int> likedIds;
        if (success && msg.respData.contains("likedIds")) {
            const QJsonArray arr = msg.respData["likedIds"].toArray();
            for (const QJsonValue &v : arr)
                likedIds.append(v.toInt());
        }
        emit SIG_getLikeStatusResult(success, msg.data, likedIds);
    } else if (action == "toggle_like") {
        const int videoId = msg.respData.value("videoId").toInt(-1);
        const bool liked = msg.respData.value("liked").toBool();
        const int likesCount = msg.respData.value("likesCount").toInt(0);
        emit SIG_toggleLikeResult(success, msg.data, videoId, liked, likesCount);
    } else if (action == "upload_start") {
        QString uploadId;
        if (msg.respData.contains("uploadId"))
            uploadId = msg.respData["uploadId"].toVariant().toString();
        if (success)
            m_currentUploadId = uploadId;
        emit SIG_uploadStartResult(success, cleanData, uploadId);
    } else if (action == "upload_chunk") {
        int chunkIndex = msg.respData.value("chunkIndex").toInt(-1);
        if (success) {
            m_currentUploadSentChunks++;
            qint64 bytesSent = m_currentUploadSentChunks * m_currentUploadChunkSize;
            if (bytesSent > m_currentUploadFileSize)
                bytesSent = m_currentUploadFileSize;
            emit SIG_uploadProgress(m_currentUploadId, bytesSent, m_currentUploadFileSize);
        }
        emit SIG_uploadChunkResult(success, cleanData, chunkIndex);
    } else if (action == "upload_end") {
        bool hlsOk = msg.respData.value("hlsOk").toBool(true);
        QString endMsg = cleanData;
        if (!hlsOk)
            endMsg = QStringLiteral("上传完成|hls_failed");
        emit SIG_uploadEndResult(success, endMsg);
    } else if (action == "upload_resume") {
        emit SIG_uploadResumeResult(success, cleanData,
            msg.respData.value("uploadId").toVariant().toString(),
            msg.respData.value("nextChunkIndex").toInt(0));
    } else if (action == "download_start") {
        int videoId = msg.respData.value("videoId").toInt(-1);
        qint64 fileSize = msg.respData.value("fileSize").toVariant().toLongLong();
        int totalChunks = msg.respData.value("totalChunks").toInt(0);
        if (success) {
            m_currentVideoId = videoId;
            m_currentDownloadFileSize = fileSize;
            m_currentDownloadTotalChunks = totalChunks;
            m_currentDownloadReceivedChunks = 0;
        }
        emit SIG_downloadStartResult(success, cleanData, videoId, fileSize, totalChunks);
    } else if (action == "download_chunk") {
        int chunkIndex = msg.respData.value("chunkIndex").toInt(-1);
        QByteArray chunkData;
        if (msg.respData.contains("chunkData"))
            chunkData = QByteArray::fromBase64(msg.respData["chunkData"].toString().toLatin1());
        if (success) {
            m_currentDownloadReceivedChunks++;
            qint64 bytesReceived = m_currentDownloadReceivedChunks * DEFAULT_CHUNK_SIZE;
            if (bytesReceived > m_currentDownloadFileSize)
                bytesReceived = m_currentDownloadFileSize;
            emit SIG_downloadProgress(m_currentVideoId, bytesReceived, m_currentDownloadFileSize);
        }
        emit SIG_downloadChunkResult(success, cleanData, chunkIndex, chunkData);
    } else if (action == "my_videos") {
        QJsonArray videos;
        if (success && msg.respData.contains("videos"))
            videos = msg.respData["videos"].toArray();
        emit SIG_myVideosResult(success, cleanData, videos);
    } else if (action == "delete_video") {
        emit SIG_deleteVideoResult(success, cleanData);
    } else if (!action.isEmpty()) {
        qDebug() << "未知响应 action:" << action << cleanData;
    }
}

void NetworkManager::onConnected()
{
    qDebug() << "已连接到服务器";
    emit SIG_connected();
}

void NetworkManager::onDisconnected()
{
    qDebug() << "与服务器断开连接";
    m_isLoggedIn = false;
    m_token.clear();
    m_loggedInUsername.clear();
    emit SIG_disconnected();
}

void NetworkManager::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    
    // 循环处理完整的消息
    while (m_buffer.size() >= 8) {
        quint32 length;
        memcpy(&length, m_buffer.constData() + sizeof(quint32), sizeof(quint32));
        length = qFromBigEndian(length);   // ← 网络字节序转主机字节序

        if (static_cast<quint32>(m_buffer.size()) >= 8 + length) {
            NetworkMessage msg = parseMessage(m_buffer.left(8 + length));
            m_buffer.remove(0, 8 + length);
            
            if (msg.type == MSG_RESPONSE) {
                if (msg.action.isEmpty()) {
                    qDebug() << "响应缺少 action 字段:" << msg.data << "code=" << msg.code;
                    if (msg.code == RESP_PASSWORD_ERROR || msg.code == RESP_USER_NOT_FOUND)
                        emit SIG_loginResult(false, msg.data, QString());
                    else if (msg.code == RESP_USER_EXISTS)
                        emit SIG_registerResult(false, msg.data);
                } else {
                    dispatchResponse(msg);
                }
            }
        } else {
            break;
        }
    }
}

void NetworkManager::onError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    emit SIG_error(m_socket->errorString());
}

#include "server.h"
#include "auth_util.h"
#include "recommendengine.h"
#include <algorithm>
#include <thread>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QLockFile>    // 文件锁（防止元数据竞争条件）
#include <QCryptographicHash>  // 【P2修复】SHA-256 哈希
#include <QtEndian>   // Qt 5.9 字节序转换函数（qFromBigEndian / qToBigEndian）
#include <QMutexLocker>
#include <QProcess>
#include <QRegExp>
#include <QSet>

namespace {

bool isValidUploadId(const QString &uploadId)
{
    if (uploadId.isEmpty() || uploadId.length() > 64)
        return false;
    static const QRegExp pattern(QStringLiteral("^[0-9]+_[0-9]+$"));
    return pattern.exactMatch(uploadId);
}

QString resolveUploadDir(const QString &uploadId)
{
    if (!isValidUploadId(uploadId))
        return QString();
    const QString base = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + QStringLiteral("/uploads"))
        + QLatin1Char('/');
    const QString resolved = QDir::cleanPath(base + uploadId);
    if (!resolved.startsWith(base))
        return QString();
    return resolved;
}

bool isChunksComplete(const QJsonArray &received, int totalChunks)
{
    if (totalChunks <= 0)
        return false;
    QSet<int> indices;
    for (const QJsonValue &v : received) {
        const int idx = v.toInt(-1);
        if (idx < 0 || idx >= totalChunks)
            return false;
        indices.insert(idx);
    }
    return indices.size() == static_cast<int>(totalChunks);
}

bool isUploadOwnedBy(int metaUserId, int sessionUserId)
{
    return metaUserId > 0 && sessionUserId > 0 && metaUserId == sessionUserId;
}

// 数据库中的 hls/cover URL 可能仍是旧 IP/端口，返回客户端前按当前 config 重写
QString rewriteHlsHttpUrl(const QString &storedUrl, const QString &ip, int port)
{
    if (storedUrl.isEmpty() || port <= 0)
        return storedUrl;
    const QString marker = QStringLiteral("/hls/");
    const int idx = storedUrl.indexOf(marker);
    if (idx < 0)
        return storedUrl;
    const QString relativePath = storedUrl.mid(idx + marker.size());
    if (relativePath.isEmpty())
        return storedUrl;
    return QStringLiteral("http://%1:%2/hls/%3").arg(ip).arg(port).arg(relativePath);
}

QJsonObject rewriteVideoJsonUrls(const QJsonObject &obj, const QString &ip, int port)
{
    QJsonObject out = obj;
    if (out.contains(QStringLiteral("coverUrl")))
        out[QStringLiteral("coverUrl")] = rewriteHlsHttpUrl(out[QStringLiteral("coverUrl")].toString(), ip, port);
    if (out.contains(QStringLiteral("hlsUrl")))
        out[QStringLiteral("hlsUrl")] = rewriteHlsHttpUrl(out[QStringLiteral("hlsUrl")].toString(), ip, port);
    return out;
}

QJsonObject rewriteCachedPayload(const QJsonObject &data, const QString &ip, int port)
{
    QJsonObject out = data;
    if (out.contains(QStringLiteral("videos")) && out[QStringLiteral("videos")].isArray()) {
        QJsonArray videos = out[QStringLiteral("videos")].toArray();
        QJsonArray rewritten;
        for (const QJsonValue &v : videos) {
            if (v.isObject())
                rewritten.append(rewriteVideoJsonUrls(v.toObject(), ip, port));
            else
                rewritten.append(v);
        }
        out[QStringLiteral("videos")] = rewritten;
    }
    return rewriteVideoJsonUrls(out, ip, port);
}

bool runFFmpeg(const QStringList &args, int timeoutMs = 3600000)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    qDebug() << "ffmpeg" << args.join(' ');
    proc.start("ffmpeg", args);
    if (!proc.waitForStarted(10000)) {
        qDebug() << "ffmpeg 启动失败：" << proc.errorString();
        return false;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        qDebug() << "ffmpeg 超时";
        proc.kill();
        return false;
    }
    const QByteArray output = proc.readAllStandardOutput();
    if (proc.exitCode() != 0) {
        qDebug() << "ffmpeg 失败，exit=" << proc.exitCode() << output;
        return false;
    }
    return true;
}

QString probeVideoCodec(const QString &videoPath)
{
    QProcess proc;
    proc.start("ffprobe", QStringList()
               << "-v" << "error"
               << "-select_streams" << "v:0"
               << "-show_entries" << "stream=codec_name"
               << "-of" << "csv=p=0"
               << videoPath);
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0)
        return QString();
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

bool probeHasAudioStream(const QString &videoPath)
{
    QProcess proc;
    proc.start("ffprobe", QStringList()
               << "-v" << "error"
               << "-select_streams" << "a:0"
               << "-show_entries" << "stream=index"
               << "-of" << "csv=p=0"
               << videoPath);
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0)
        return false;
    return !QString::fromUtf8(proc.readAllStandardOutput()).trimmed().isEmpty();
}

void appendStreamMaps(QStringList &args, const QString &videoPath)
{
    // FFmpeg 3.0.x 不支持 -map 0:a:0? 可选流语法
    args << "-map" << "0:v:0";
    if (probeHasAudioStream(videoPath))
        args << "-map" << "0:a:0";
}

// HLS 复用参数：兼容 Ubuntu 16.04 / FFmpeg 3.0.x（勿使用 3.2+ 的 -hls_segment_type）
static void appendHlsMuxArgs(QStringList &args,
                             const QString &segPattern,
                             const QString &m3u8Path)
{
    args << "-start_number" << "0"
         << "-hls_time" << "10"
         << "-hls_list_size" << "0"
         << "-hls_segment_filename" << segPattern
         << "-f" << "hls" << m3u8Path;
}

bool remuxToHls(const QString &videoPath, const QString &m3u8Path)
{
    QFileInfo m3u8Info(m3u8Path);
    QDir().mkpath(m3u8Info.absolutePath());
    const QString segPattern = QDir::toNativeSeparators(
        m3u8Info.absolutePath() + "/segment_%03d.ts");

    const QString vCodec = probeVideoCodec(videoPath);
    qDebug() << "视频编码：" << (vCodec.isEmpty() ? "unknown" : vCodec);

    // MP4/H.264 优先走 copy + 比特流滤镜（解决 annexb / ADTS 问题）
    // 注意：FFmpeg 3.x 不支持 -max_muxing_queue_size（3.4+ 才引入），
    //       且不支持逗号分隔的多个 bsf，需要分两次指定
    if (vCodec.isEmpty() || vCodec == "h264") {
        QStringList args;
        args << "-y"
             << "-fflags" << "+genpts"
             << "-i" << videoPath;
        appendStreamMaps(args, videoPath);
        args << "-c:v" << "copy"
             << "-bsf:v" << "dump_extra"
             << "-bsf:v" << "h264_mp4toannexb"
             << "-c:a" << "copy"
             << "-bsf:a" << "aac_adtstoasc";
        appendHlsMuxArgs(args, segPattern, m3u8Path);
        if (runFFmpeg(args))
            return true;
        qDebug() << "HLS copy+bsf 模式失败，尝试转码...";
    } else {
        qDebug() << "非 H.264 编码，跳过 copy 模式";
    }

    QStringList args;
    args << "-y"
         << "-i" << videoPath;
    appendStreamMaps(args, videoPath);
    args << "-c:v" << "libx264"
         << "-preset" << "veryfast"
         << "-crf" << "23"
         << "-pix_fmt" << "yuv420p"
         << "-c:a" << "aac"
         << "-b:a" << "128k"
         << "-ac" << "2";
    appendHlsMuxArgs(args, segPattern, m3u8Path);
    return runFFmpeg(args);
}

// 发现页网格用 16:9 封面，黑边填充（避免旧版黄条 pad 在卡片顶部露出）
bool extractCompositeCoverImage(const QString &videoPath, const QString &coverPath,
                                const QString &title)
{
    Q_UNUSED(title);
    const int coverW = 640;
    const int coverH = 360;

    QStringList args;
    args << "-y" << "-ss" << "1" << "-i" << videoPath
         << "-vf" << QStringLiteral(
             "scale=%1:%2:force_original_aspect_ratio=decrease,"
             "pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black")
                .arg(coverW).arg(coverH)
         << "-vframes" << "1" << "-q:v" << "2" << coverPath;
    if (runFFmpeg(args, 120000))
        return true;

    qDebug() << "16:9 封面生成失败，回退为单帧截取";
    args.clear();
    args << "-y" << "-ss" << "1" << "-i" << videoPath
         << "-vframes" << "1" << "-q:v" << "2" << coverPath;
    return runFFmpeg(args, 120000);
}

bool appendFileStream(QFile *dest, const QString &srcPath)
{
    QFile src(srcPath);
    if (!src.open(QIODevice::ReadOnly))
        return false;
    const qint64 bufSize = 1024 * 1024;
    while (!src.atEnd()) {
        QByteArray buf = src.read(bufSize);
        if (buf.isEmpty())
            break;
        if (dest->write(buf) != buf.size())
            return false;
    }
    return true;
}

} // namespace

// ============================================================
// ClientHandler 构造函数
// ============================================================
ClientHandler::ClientHandler(int fd, const QString &peerAddr, SendFunc sendFunc,
                             Database *db, RedisService *redis,
                             const QString &hlsIp, int hlsPort,
                             const QString &hlsOutputDir)
    : m_fd(fd), m_peerAddr(peerAddr), m_sendFunc(std::move(sendFunc)), m_db(db),
      m_redis(redis), m_hlsIp(hlsIp), m_hlsPort(hlsPort), m_hlsOutputDir(hlsOutputDir)
{
}

ClientHandler::~ClientHandler()
{
}

bool ClientHandler::resolveSession(const QJsonObject &json)
{
    const QString token = json.value(QStringLiteral("token")).toString();
    if (m_redis && m_redis->available() && !token.isEmpty()) {
        int userId = 0;
        QString username;
        if (m_redis->validateSession(token, &userId, &username)) {
            m_userId = userId;
            m_username = username;
            return true;
        }
        // token 在 Redis 中不存在（会话未写入或已过期）时，回退同连接登录态
    }

    const QString reqUsername = json.value(QStringLiteral("username")).toString();
    if (!reqUsername.isEmpty()) {
        if (m_username.isEmpty())
            m_username = reqUsername;
        if (m_username == reqUsername) {
            if (m_userId <= 0)
                m_userId = m_db->getUserId(m_username);
            if (m_userId > 0)
                return true;
        }
    }

    if (!m_username.isEmpty()) {
        if (m_userId <= 0)
            m_userId = m_db->getUserId(m_username);
        return m_userId > 0;
    }
    return false;
}

bool ClientHandler::ensureAuth(const QJsonObject &json, const QString &action)
{
    if (resolveSession(json))
        return true;
    sendResponse(RESP_SERVER_ERROR, QStringLiteral("请先登录"), action);
    return false;
}

QJsonArray ClientHandler::videosToJsonArray(const QVariantList &list) const
{
    QJsonArray videoArray;
    for (const QVariant &v : list) {
        const QVariantMap video = v.toMap();
        QJsonObject obj;
        obj["id"]         = video["id"].toInt();
        obj["title"]      = video["title"].toString();
        obj["coverUrl"]   = rewriteHlsHttpUrl(video["coverUrl"].toString(), m_hlsIp, m_hlsPort);
        obj["playCount"]  = video["playCount"].toInt();
        obj["likesCount"] = video["likesCount"].toInt();
        obj["createdAt"]  = video["createdAt"].toString();
        obj["author"]     = video["author"].toString();
        obj["hlsUrl"]     = rewriteHlsHttpUrl(video["hlsUrl"].toString(), m_hlsIp, m_hlsPort);
        if (video.contains("hotScore"))
            obj["hotScore"] = video["hotScore"].toDouble();
        videoArray.append(obj);
    }
    return videoArray;
}

// ============================================================
// 处理完整消息帧（在线程池中调用）
// 消息格式：[type 4B][length 4B][data]
// ============================================================
void ClientHandler::processMessage(quint32 type, const QByteArray &jsonData)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &error);

    if (error.error != QJsonParseError::NoError) {
        qDebug() << "JSON 解析失败：" << error.errorString();
        sendResponse(RESP_SERVER_ERROR, "JSON 格式错误");
        return;
    }

    QJsonObject json = doc.object();

    switch (type) {
        case MSG_REGISTER:
            handleRegister(json);
            break;
        case MSG_LOGIN:
            handleLogin(json);
            break;
        case MSG_GET_PASSWORD_SALT:
            handleGetPasswordSalt(json);
            break;
        case MSG_GET_VIDEO_LIST:
            handleGetVideoList(json);
            break;
        case MSG_GET_RECOMMEND_FEED:
            handleGetRecommendFeed(json);
            break;
        case MSG_SEARCH_VIDEOS:
            handleSearchVideos(json);
            break;
        case MSG_GET_VIDEO_INFO:
            handleGetVideoInfo(json);
            break;
        case MSG_INCREMENT_PLAY:
            handleIncrementPlay(json);
            break;
        case MSG_GET_LIKE_STATUS:
            handleGetLikeStatus(json);
            break;
        case MSG_TOGGLE_LIKE:
            handleToggleLike(json);
            break;
        case MSG_UPLOAD_START:
            handleUploadStart(json);
            break;
        case MSG_UPLOAD_CHUNK:
            handleUploadChunk(json);
            break;
        case MSG_UPLOAD_END:
            handleUploadEnd(json);
            break;
        case MSG_UPLOAD_RESUME:
            handleUploadResume(json);
            break;
        case MSG_DOWNLOAD_START:
            handleDownloadStart(json);
            break;
        case MSG_DOWNLOAD_CHUNK:
            handleDownloadChunk(json);
            break;
        case MSG_GET_MY_VIDEOS:
            handleGetMyVideos(json);
            break;
        case MSG_DELETE_VIDEO:
            handleDeleteVideo(json);
            break;
        default:
            qDebug() << "未知消息类型：" << type;
            sendResponse(RESP_SERVER_ERROR, "未知消息类型");
            break;
    }
}

// ============================================================
// 处理注册请求
// 参数：json = 请求数据（包含 username, password）
// ============================================================
void ClientHandler::handleRegister(const QJsonObject &json)
{
    QString username = json["username"].toString();
    QString salt = json["salt"].toString();
    QString passwordHash = json["password"].toString();

    qDebug() << "收到注册请求：用户名 =" << username;

    if (username.isEmpty() || salt.isEmpty() || passwordHash.isEmpty()) {
        sendResponse(RESP_SERVER_ERROR, "用户名或密码不能为空", "register");
        return;
    }
    if (!isValidPasswordSalt(salt) || !isSha256Hex(passwordHash)) {
        sendResponse(RESP_SERVER_ERROR, "密码格式无效", "register");
        return;
    }

    if (m_db->registerUser(username, salt, passwordHash)) {
        if (m_redis && m_redis->available())
            m_redis->setCachedSalt(username, salt);
        sendResponse(RESP_SUCCESS, "注册成功", "register");
    } else {
        // 判断是用户已存在还是数据库错误
        if (m_db->userExists(username)) {
            sendResponse(RESP_USER_EXISTS, "用户已存在", "register");
        } else {
            sendResponse(RESP_SERVER_ERROR, "注册失败：数据库错误", "register");
        }
    }
}

// ============================================================
// 处理登录请求
// 参数：json = 请求数据（包含 username, password）
// ============================================================
void ClientHandler::handleLogin(const QJsonObject &json)
{
    QString username = json["username"].toString();
    QString password = json["password"].toString();

    qDebug() << "收到登录请求：用户名 =" << username;

    if (username.isEmpty() || password.isEmpty()) {
        sendResponse(RESP_SERVER_ERROR, "用户名或密码不能为空", "login");
        return;
    }

    if (m_redis && m_redis->available()) {
        const QString rateKey = QStringLiteral("ratelimit:login:%1").arg(m_peerAddr);
        if (!m_redis->allowRequest(rateKey,
                                   m_redis->config().loginRateLimit,
                                   m_redis->config().loginRateWindowSec)) {
            sendResponse(RESP_SERVER_ERROR, QStringLiteral("登录过于频繁，请稍后再试"), "login");
            return;
        }
    }

    if (m_db->loginUser(username, password)) {
        m_username = username;
        m_userId = m_db->getUserId(username);

        QString tokenRaw = username + "_"
            + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_"
            + QString::number(qrand());
        const QString token = QString(
            QCryptographicHash::hash(tokenRaw.toUtf8(), QCryptographicHash::Sha256).toHex());

        if (m_redis && m_redis->available())
            m_redis->createSession(token, m_userId, username, m_peerAddr);

        QJsonObject data;
        data["token"] = token;
        data["username"] = username;
        sendResponse(RESP_SUCCESS, "登录成功", "login", data);
    } else {
        // 判断是用户不存在还是密码错误
        if (!m_db->userExists(username)) {
            sendResponse(RESP_USER_NOT_FOUND, "用户不存在", "login");
        } else {
            sendResponse(RESP_PASSWORD_ERROR, "密码错误", "login");
        }
    }
}

void ClientHandler::handleGetPasswordSalt(const QJsonObject &json)
{
    QString username = json["username"].toString();
    if (username.isEmpty()) {
        sendResponse(RESP_SERVER_ERROR, "用户名不能为空", "password_salt");
        return;
    }

    if (m_redis && m_redis->available()) {
        const QString rateKey = QStringLiteral("ratelimit:login:%1").arg(m_peerAddr);
        if (!m_redis->allowRequest(rateKey,
                                   m_redis->config().loginRateLimit,
                                   m_redis->config().loginRateWindowSec)) {
            sendResponse(RESP_SERVER_ERROR, QStringLiteral("请求过于频繁，请稍后再试"), "password_salt");
            return;
        }
    }

    QString salt;
    if (m_redis && m_redis->available() && m_redis->getCachedSalt(username, &salt)) {
        QJsonObject data;
        data["salt"] = salt;
        sendResponse(RESP_SUCCESS, "获取密码盐成功", "password_salt", data);
        return;
    }

    bool found = false;
    salt = m_db->getPasswordSalt(username, &found);
    if (!found) {
        sendResponse(RESP_USER_NOT_FOUND, "用户不存在", "password_salt");
        return;
    }

    if (m_redis && m_redis->available())
        m_redis->setCachedSalt(username, salt);

    QJsonObject data;
    data["salt"] = salt;
    sendResponse(RESP_SUCCESS, "获取密码盐成功", "password_salt", data);
}

// ============================================================
// 发送响应
// 参数：code = 响应状态码, message = 响应消息
//
// 响应格式：[type=3(4B)][length(4B)][JSON]
// JSON 格式：{"code": xxx, "message": "xxx", "action": "xxx", "data": {...}}
// ============================================================

void ClientHandler::sendResponse(int code, const QString &message, const QString &action, const QJsonObject &data)
{
    // 构造 JSON 响应
    QJsonObject json;
    json["code"] = code;
    json["message"] = message;
    if (!action.isEmpty()) {
        json["action"] = action;
    }
    if (!data.isEmpty()) {
        json["data"] = data;
    }

    QByteArray jsonData = QJsonDocument(json).toJson(QJsonDocument::Compact);

    // 打包响应帧
    QByteArray response;

    // 消息类型（3 = 响应）
    quint32 type = qToBigEndian(static_cast<quint32>(MSG_RESPONSE));
    response.append(reinterpret_cast<const char*>(&type), 4);

    // 数据长度
    quint32 length = qToBigEndian(static_cast<quint32>(jsonData.size()));
    response.append(reinterpret_cast<const char*>(&length), 4);

    // JSON 数据
    response.append(jsonData);

    if (m_sendFunc)
        m_sendFunc(response);

    qDebug() << "发送响应：code =" << code << ", message =" << message
             << ", action =" << (action.isEmpty() ? QStringLiteral("(无)") : action);
}

// ============================================================
// 获取视频列表
// JSON: {"offset": 0, "limit": 20}
// 【Phase1】DB 查询在 QThreadPool 中执行
// ============================================================
void ClientHandler::handleGetVideoList(const QJsonObject &json)
{
    int offset = json["offset"].toInt(0);
    int limit  = json["limit"].toInt(20);

    const QString cacheKey = QStringLiteral("cache:video:list:%1:%2").arg(offset).arg(limit);
    if (m_redis && m_redis->available()) {
        QByteArray cached;
        if (m_redis->getCache(cacheKey, &cached)) {
            const QJsonDocument doc = QJsonDocument::fromJson(cached);
            if (doc.isObject()) {
                sendResponse(RESP_SUCCESS, "获取视频列表成功", "video_list",
                              rewriteCachedPayload(doc.object(), m_hlsIp, m_hlsPort));
                return;
            }
        }
    }

    const QVariantList list = m_db->getVideoList(offset, limit);
    const QJsonArray videoArray = videosToJsonArray(list);

    QJsonObject data;
    data["videos"] = videoArray;
    data["total"]  = videoArray.size();

    if (m_redis && m_redis->available()) {
        m_redis->setCache(cacheKey,
                          QJsonDocument(data).toJson(QJsonDocument::Compact),
                          m_redis->config().videoListTtlSec);
    }

    sendResponse(RESP_SUCCESS, "获取视频列表成功", "video_list", data);
}

void ClientHandler::handleGetRecommendFeed(const QJsonObject &json)
{
    int offset = json["offset"].toInt(0);
    int limit  = qMin(json["limit"].toInt(20), 50);

    const QString cacheKey = QStringLiteral("cache:feed:%1:%2").arg(offset).arg(limit);
    if (m_redis && m_redis->available()) {
        QByteArray cached;
        if (m_redis->getCache(cacheKey, &cached)) {
            const QJsonDocument doc = QJsonDocument::fromJson(cached);
            if (doc.isObject()) {
                sendResponse(RESP_SUCCESS, "获取推荐列表成功", "recommend_feed",
                              rewriteCachedPayload(doc.object(), m_hlsIp, m_hlsPort));
                return;
            }
        }
    }

    RecommendEngine engine(m_db);
    const QVariantList list = engine.getFeed(offset, limit);
    const QJsonArray videoArray = videosToJsonArray(list);

    QJsonObject data;
    data["videos"] = videoArray;
    data["total"]  = videoArray.size();
    data["strategy"] = QStringLiteral("hot_v1");

    if (m_redis && m_redis->available()) {
        m_redis->setCache(cacheKey,
                          QJsonDocument(data).toJson(QJsonDocument::Compact),
                          m_redis->config().feedTtlSec);
    }

    sendResponse(RESP_SUCCESS, "获取推荐列表成功", "recommend_feed", data);
}

void ClientHandler::handleSearchVideos(const QJsonObject &json)
{
    QString keyword = json["keyword"].toString().trimmed();
    int offset = json["offset"].toInt(0);
    int limit  = qMin(json["limit"].toInt(20), 50);

    if (keyword.isEmpty()) {
        sendResponse(RESP_SERVER_ERROR, "搜索关键词不能为空", "search_videos");
        return;
    }

    const QString cacheKey = QStringLiteral("cache:search:%1:%2:%3")
        .arg(QString(QCryptographicHash::hash(keyword.toUtf8(), QCryptographicHash::Md5).toHex()))
        .arg(offset).arg(limit);
    if (m_redis && m_redis->available()) {
        QByteArray cached;
        if (m_redis->getCache(cacheKey, &cached)) {
            const QJsonDocument doc = QJsonDocument::fromJson(cached);
            if (doc.isObject()) {
                sendResponse(RESP_SUCCESS, "搜索成功", "search_videos",
                              rewriteCachedPayload(doc.object(), m_hlsIp, m_hlsPort));
                return;
            }
        }
    }

    const QVariantList list = m_db->searchVideos(keyword, offset, limit);
    const QJsonArray videoArray = videosToJsonArray(list);

    QJsonObject data;
    data["videos"] = videoArray;
    data["total"]  = videoArray.size();
    data["keyword"] = keyword;

    if (m_redis && m_redis->available()) {
        m_redis->setCache(cacheKey,
                          QJsonDocument(data).toJson(QJsonDocument::Compact),
                          m_redis->config().searchTtlSec);
    }

    sendResponse(RESP_SUCCESS, "搜索成功", "search_videos", data);
}

// ============================================================
// 获取视频详情
// JSON: {"videoId": 1}
// 【Phase1】DB 查询在 QThreadPool 中执行
// ============================================================
void ClientHandler::handleGetVideoInfo(const QJsonObject &json)
{
    int videoId = json["videoId"].toInt(-1);
    if (videoId < 0) {
        sendResponse(RESP_SERVER_ERROR, "视频ID无效");
        return;
    }

    const QString cacheKey = QStringLiteral("cache:video:%1").arg(videoId);
    if (m_redis && m_redis->available()) {
        QByteArray cached;
        if (m_redis->getCache(cacheKey, &cached)) {
            const QJsonDocument doc = QJsonDocument::fromJson(cached);
            if (doc.isObject()) {
                sendResponse(RESP_SUCCESS, "获取视频详情成功", "video_info",
                              rewriteCachedPayload(doc.object(), m_hlsIp, m_hlsPort));
                return;
            }
        }
    }

    QVariantMap video = m_db->getVideoById(videoId);
    if (video.isEmpty()) {
        sendResponse(RESP_USER_NOT_FOUND, "视频不存在");
        return;
    }

    QJsonObject data;
    data["id"]          = video["id"].toInt();
    data["title"]       = video["title"].toString();
    data["description"] = video["description"].toString();
    data["coverUrl"]    = rewriteHlsHttpUrl(video["coverUrl"].toString(), m_hlsIp, m_hlsPort);
    data["videoUrl"]    = video["videoUrl"].toString();
    data["hlsUrl"]      = rewriteHlsHttpUrl(video["hlsUrl"].toString(), m_hlsIp, m_hlsPort);
    data["duration"]    = video["duration"].toInt();
    data["playCount"]   = video["playCount"].toInt();
    data["likesCount"]  = video["likesCount"].toInt();
    data["author"]      = video["author"].toString();

    if (m_redis && m_redis->available()) {
        m_redis->setCache(cacheKey,
                          QJsonDocument(data).toJson(QJsonDocument::Compact),
                          m_redis->config().videoInfoTtlSec);
    }

    sendResponse(RESP_SUCCESS, "获取视频详情成功", "video_info", data);
}

// ============================================================
// 增加播放量
// JSON: {"videoId": 1}
// 【Phase1】DB 更新在 QThreadPool 中执行
// ============================================================
void ClientHandler::handleIncrementPlay(const QJsonObject &json)
{
    int videoId = json["videoId"].toInt(-1);
    if (videoId < 0) {
        sendResponse(RESP_SERVER_ERROR, "视频ID无效");
        return;
    }

    if (m_redis && m_redis->available()) {
        const QString rateKey = QStringLiteral("ratelimit:play:%1:%2")
            .arg(m_peerAddr).arg(videoId);
        if (!m_redis->allowRequest(rateKey,
                                   m_redis->config().playRateLimit,
                                   m_redis->config().playRateWindowSec)) {
            sendResponse(RESP_SUCCESS, "播放量+1", "increment_play");
            return;
        }
        if (m_redis->incrementPlayCounter(videoId)) {
            sendResponse(RESP_SUCCESS, "播放量+1", "increment_play");
            return;
        }
    }

    const bool ok = m_db->incrementPlayCount(videoId);
    if (ok)
        sendResponse(RESP_SUCCESS, "播放量+1", "increment_play");
    else
        sendResponse(RESP_SERVER_ERROR, "增加播放量失败", "increment_play");
}

void ClientHandler::handleGetLikeStatus(const QJsonObject &json)
{
    if (!ensureAuth(json, QStringLiteral("get_like_status")))
        return;

    QList<int> videoIds;
    const QJsonArray arr = json["videoIds"].toArray();
    for (const QJsonValue &v : arr) {
        const int id = v.toInt(-1);
        if (id > 0)
            videoIds.append(id);
    }
    if (videoIds.isEmpty()) {
        QJsonObject data;
        data["likedIds"] = QJsonArray();
        sendResponse(RESP_SUCCESS, "获取点赞状态成功", "get_like_status", data);
        return;
    }

    const int userId = m_userId > 0 ? m_userId : m_db->getUserId(m_username);
    const QList<int> likedIds = m_db->getLikedVideoIds(userId, videoIds);
    QJsonArray likedArray;
    for (int id : likedIds)
        likedArray.append(id);

    QJsonObject data;
    data["likedIds"] = likedArray;
    sendResponse(RESP_SUCCESS, "获取点赞状态成功", "get_like_status", data);
}

void ClientHandler::handleToggleLike(const QJsonObject &json)
{
    if (!ensureAuth(json, QStringLiteral("toggle_like")))
        return;

    const int videoId = json["videoId"].toInt(-1);
    if (videoId < 0) {
        sendResponse(RESP_SERVER_ERROR, "视频ID无效", "toggle_like");
        return;
    }

    const int userId = m_userId > 0 ? m_userId : m_db->getUserId(m_username);
    const QVariantMap result = m_db->toggleVideoLike(videoId, userId);
    if (!result.value("success").toBool()) {
        sendResponse(RESP_SERVER_ERROR,
                     result.value("message").toString(), "toggle_like");
        return;
    }

    if (m_redis && m_redis->available())
        m_redis->invalidateVideoCaches(videoId);

    QJsonObject data;
    data["videoId"] = result.value("videoId").toInt();
    data["liked"] = result.value("liked").toBool();
    data["likesCount"] = result.value("likesCount").toInt();
    sendResponse(RESP_SUCCESS, "点赞状态已更新", "toggle_like", data);
}

// ============================================================
// 分块上传：开始上传
// JSON: {"fileName": "...", "fileSize": 12345, "totalChunks": 10}
// 响应：{"code": 200, "message": "上传开始成功", "data": {"uploadId": "..."}}
// ============================================================
void ClientHandler::handleUploadStart(const QJsonObject &json)
{
    if (!ensureAuth(json, QStringLiteral("upload_start")))
        return;

    if (m_redis && m_redis->available()) {
        const QString rateKey = QStringLiteral("ratelimit:upload:%1").arg(m_peerAddr);
        if (!m_redis->allowRequest(rateKey,
                                   m_redis->config().uploadRateLimit,
                                   m_redis->config().uploadRateWindowSec)) {
            sendResponse(RESP_SERVER_ERROR, QStringLiteral("上传过于频繁，请稍后再试"), "upload_start");
            return;
        }
    }

    QString fileName    = json["fileName"].toString();
    qint64  fileSize    = json["fileSize"].toVariant().toLongLong();
    int      totalChunks = json["totalChunks"].toInt();

    // 【P0修复】路径遍历检查：移除路径分隔符
    fileName = fileName.remove(QRegExp("[\\\\/]"));
    if (fileName.isEmpty()) {
        sendResponse(RESP_SERVER_ERROR, "文件名无效");
        return;
    }

    // 【P0修复】文件大小限制检查
    if (fileSize <= 0 || fileSize > MAX_FILE_SIZE) {
        sendResponse(RESP_SERVER_ERROR, "文件大小无效或超出限制（最大1GB）");
        return;
    }

    if (totalChunks <= 0) {
        sendResponse(RESP_SERVER_ERROR, "参数错误");
        return;
    }

    // 生成唯一 uploadId（时间戳 + 随机数，防碰撞）
    QString uploadId = QString::number(QDateTime::currentMSecsSinceEpoch())
                     + "_" + QString::number(qrand() % 10000);

    // 创建上传目录：uploads/{uploadId}/
    QString uploadDir = QCoreApplication::applicationDirPath() + "/uploads/" + uploadId;
    QDir dir;
    if (!dir.exists(uploadDir)) {
        dir.mkpath(uploadDir);
    }

    const int sessionUserId = m_userId > 0 ? m_userId : m_db->getUserId(m_username);

    // 保存元数据到 meta.json
    QJsonObject meta;
    meta["uploadId"]      = uploadId;
    meta["userId"]        = sessionUserId;
    meta["fileName"]      = fileName;
    meta["fileSize"]      = fileSize;
    meta["totalChunks"]   = totalChunks;
    meta["receivedChunks"] = QJsonArray();  // 已接收的块索引
    meta["status"]         = "uploading";
    meta["createTime"]     = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile metaFile(uploadDir + "/meta.json");
    if (!metaFile.open(QIODevice::WriteOnly)) {
        QDir(uploadDir).removeRecursively();
        sendResponse(RESP_SERVER_ERROR, "无法创建上传会话");
        return;
    }
    metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
    metaFile.close();

    if (m_redis && m_redis->available())
        m_redis->saveUploadMeta(uploadId, meta);

    qDebug() << "开始上传：" << uploadId << "," << fileName;

    // 返回 uploadId 给客户端
    QJsonObject data;
    data["uploadId"] = uploadId;
    sendResponse(RESP_SUCCESS, "上传开始成功", "upload_start", data);
}

// ============================================================
// 分块上传：接收数据块
// JSON: {"uploadId": "...", "chunkIndex": 0, "chunkData": "<base64>"}
// 【Phase1】文件写入在 QThreadPool 中异步执行，不阻塞事件循环
// ============================================================
void ClientHandler::handleUploadChunk(const QJsonObject &json)
{
    if (!ensureAuth(json, QStringLiteral("upload_chunk")))
        return;

    QString uploadId = json["uploadId"].toVariant().toString();
    int     chunkIndex = json["chunkIndex"].toInt(-1);

    if (!isValidUploadId(uploadId) || chunkIndex < 0) {
        qDebug() << "上传块参数错误：uploadId=" << uploadId << "chunkIndex=" << chunkIndex;
        sendResponse(RESP_SERVER_ERROR, "参数错误", "upload_chunk");
        return;
    }

    const QString uploadDir = resolveUploadDir(uploadId);
    if (uploadDir.isEmpty()) {
        sendResponse(RESP_SERVER_ERROR, "上传路径无效", "upload_chunk");
        return;
    }

    const QString metaPath = uploadDir + QStringLiteral("/meta.json");
    QJsonObject meta;
    if (m_redis && m_redis->available() && m_redis->loadUploadMeta(uploadId, &meta)) {
        // Redis 元数据优先
    } else {
        QFile metaFile(metaPath);
        if (!metaFile.exists() || !metaFile.open(QIODevice::ReadOnly)) {
            sendResponse(RESP_SERVER_ERROR, "上传会话不存在", "upload_chunk");
            return;
        }
        const QJsonDocument metaDoc = QJsonDocument::fromJson(metaFile.readAll());
        metaFile.close();
        if (!metaDoc.isObject()) {
            sendResponse(RESP_SERVER_ERROR, "元数据损坏", "upload_chunk");
            return;
        }
        meta = metaDoc.object();
    }

    const int sessionUserId = m_userId > 0 ? m_userId : m_db->getUserId(m_username);
    const int metaUserId = meta["userId"].toInt(-1);
    const int totalChunks = meta["totalChunks"].toInt();
    if (!isUploadOwnedBy(metaUserId, sessionUserId)) {
        sendResponse(RESP_SERVER_ERROR, "无权访问该上传会话", "upload_chunk");
        return;
    }
    if (chunkIndex >= totalChunks) {
        sendResponse(RESP_SERVER_ERROR, "块索引越界", "upload_chunk");
        return;
    }

    QString chunkBase64 = json["chunkData"].toString();
    if (chunkBase64.isEmpty()) {
        sendResponse(RESP_SERVER_ERROR, "块数据为空", "upload_chunk");
        return;
    }

    QByteArray chunkData = QByteArray::fromBase64(chunkBase64.toLatin1());
    if (chunkData.size() > MAX_CHUNK_SIZE) {
        sendResponse(RESP_SERVER_ERROR, "块数据过大", "upload_chunk");
        return;
    }

    QString chunkPath = uploadDir + QLatin1Char('/') + QString::number(chunkIndex) + QStringLiteral(".part");
    QString lockPath  = metaPath + QStringLiteral(".lock");

    QFile chunkFile(chunkPath);
    if (!chunkFile.open(QIODevice::WriteOnly)) {
        sendResponse(RESP_SERVER_ERROR, "保存块失败");
        return;
    }
    chunkFile.write(chunkData);
    chunkFile.close();

    bool metaUpdated = false;
    if (m_redis && m_redis->available()) {
        metaUpdated = m_redis->addUploadChunk(uploadId, chunkIndex);
        QJsonArray received = meta["receivedChunks"].toArray();
        bool already = false;
        for (const QJsonValue &v : received) {
            if (v.toInt() == chunkIndex) { already = true; break; }
        }
        if (!already) {
            received.append(chunkIndex);
            meta["receivedChunks"] = received;
            m_redis->saveUploadMeta(uploadId, meta);
        }
    }

    QLockFile lock(lockPath);
    if (!metaUpdated) {
        if (lock.tryLock(1000)) {
            QFile metaRw(metaPath);
            if (metaRw.exists() && metaRw.open(QIODevice::ReadWrite)) {
                QByteArray raw = metaRw.readAll();
                QJsonDocument doc = QJsonDocument::fromJson(raw);
                if (doc.isObject()) {
                    QJsonObject metaObj = doc.object();
                    QJsonArray received = metaObj["receivedChunks"].toArray();
                    bool already = false;
                    for (const QJsonValue &v : received) {
                        if (v.toInt() == chunkIndex) { already = true; break; }
                    }
                    if (!already) {
                        received.append(chunkIndex);
                        metaObj["receivedChunks"] = received;
                        metaRw.resize(0);
                        metaRw.write(QJsonDocument(metaObj).toJson(QJsonDocument::Compact));
                        metaUpdated = true;
                    } else {
                        metaUpdated = true;
                    }
                }
                metaRw.close();
            }
            lock.unlock();
        }
    } else {
        QLockFile lockLocal(lockPath);
        if (lockLocal.tryLock(1000)) {
            QFile metaRw(metaPath);
            if (metaRw.open(QIODevice::WriteOnly)) {
                metaRw.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
                metaRw.close();
            }
            lockLocal.unlock();
        }
    }

    if (!metaUpdated) {
        QFile::remove(chunkPath);
        sendResponse(RESP_SERVER_ERROR, "更新上传进度失败", "upload_chunk");
        return;
    }

    qDebug() << "接收块：" << uploadId
             << ", chunk" << chunkIndex
             << "(" << chunkData.size() << "bytes)";

    QJsonObject data;
    data["chunkIndex"] = chunkIndex;
    sendResponse(RESP_SUCCESS, "上传块成功", "upload_chunk", data);
}

// ============================================================
// 分块上传：上传完成，合并所有块
// JSON: {"uploadId": "...", "title": "...", "coverUrl": "...", "duration": 120, "fileHash": "..."}
// 【Phase1】文件合并 + 哈希校验 + ffmpeg 转封装全部在 QThreadPool 中执行
// ============================================================
void ClientHandler::handleUploadEnd(const QJsonObject &json)
{
    if (!ensureAuth(json, QStringLiteral("upload_end")))
        return;

    QString uploadId = json["uploadId"].toVariant().toString();
    QString clientFileHash = json["fileHash"].toString();

    if (!isValidUploadId(uploadId)) {
        sendResponse(RESP_SERVER_ERROR, "参数错误", "upload_end");
        return;
    }

    const QString uploadDir = resolveUploadDir(uploadId);
    if (uploadDir.isEmpty()) {
        sendResponse(RESP_SERVER_ERROR, "上传路径无效", "upload_end");
        return;
    }

    QString appDir     = QCoreApplication::applicationDirPath();
    QString metaPath   = uploadDir + QStringLiteral("/meta.json");
    QString videosDir  = appDir + "/videos";
    int     userId     = m_userId > 0 ? m_userId : m_db->getUserId(m_username);
    if (userId <= 0) {
        sendResponse(RESP_SERVER_ERROR, "用户无效", "upload_end");
        return;
    }
    QString coverUrl   = json["coverUrl"].toString();
    int     duration   = json["duration"].toInt();
    QString hlsIp      = m_hlsIp;
    int     hlsPort    = m_hlsPort;
    QString hlsOutputDir = m_hlsOutputDir;

    QJsonObject meta;
    if (m_redis && m_redis->available() && m_redis->loadUploadMeta(uploadId, &meta)) {
        // Redis 元数据优先
    } else {
        QFile mf(metaPath);
        if (!mf.exists() || !mf.open(QIODevice::ReadOnly)) {
            sendResponse(RESP_SERVER_ERROR, "上传会话不存在");
            return;
        }
        const QJsonDocument metaDoc = QJsonDocument::fromJson(mf.readAll());
        mf.close();
        if (!metaDoc.isObject()) {
            sendResponse(RESP_SERVER_ERROR, "元数据损坏");
            return;
        }
        meta = metaDoc.object();
    }
    const int metaUserId = meta["userId"].toInt(-1);
    if (!isUploadOwnedBy(metaUserId, userId)) {
        sendResponse(RESP_SERVER_ERROR, "无权完成该上传", "upload_end");
        return;
    }

    QString fileName    = meta["fileName"].toString();
    QString title       = json["title"].toString(fileName);
    int     totalChunks = meta["totalChunks"].toInt();
    QJsonArray received = meta["receivedChunks"].toArray();

    if (m_redis && m_redis->available()) {
        QSet<int> redisChunks;
        m_redis->uploadChunksComplete(uploadId, totalChunks, &redisChunks);
        QJsonArray redisReceived;
        for (int idx : redisChunks)
            redisReceived.append(idx);
        if (isChunksComplete(redisReceived, totalChunks))
            received = redisReceived;
    }

    if (!isChunksComplete(received, totalChunks)) {
        sendResponse(RESP_SERVER_ERROR,
                     QString("块不完整：有效块 %1/%2").arg(received.size()).arg(totalChunks),
                     "upload_end");
        return;
    }

    QDir().mkpath(videosDir);
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString finalPath = videosDir + "/" + timestamp + "_" + fileName;

    QFile finalFile(finalPath);
    if (!finalFile.open(QIODevice::WriteOnly)) {
        sendResponse(RESP_SERVER_ERROR, "无法创建最终文件");
        return;
    }

    for (int i = 0; i < totalChunks; ++i) {
        QString chunkPath = uploadDir + "/" + QString::number(i) + ".part";
        if (!appendFileStream(&finalFile, chunkPath)) {
            finalFile.close();
            finalFile.remove();
            sendResponse(RESP_SERVER_ERROR,
                       QString("块 %1 缺失或合并失败").arg(i));
            return;
        }
    }
    finalFile.close();
    qDebug() << "文件合并完成：" << finalPath;

    QString serverFileHash;
    {
        QFile hashFile(finalPath);
        if (hashFile.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (hash.addData(&hashFile)) {
                serverFileHash = QString(hash.result().toHex());
            }
            hashFile.close();
        }
    }

    if (!clientFileHash.isEmpty() && !serverFileHash.isEmpty()
        && clientFileHash.toLower() != serverFileHash.toLower()) {
        qDebug() << "哈希校验失败！文件可能损坏";
        QFile::remove(finalPath);
        sendResponse(RESP_SERVER_ERROR, "数据完整性校验失败（SHA-256 不匹配）");
        return;
    }

    int videoId = m_db->insertVideo(userId, title, coverUrl, finalPath, duration, "");
    if (videoId <= 0) {
        QFile::remove(finalPath);
        sendResponse(RESP_SERVER_ERROR, "数据库写入失败");
        return;
    }

    meta["status"]    = "completed";
    meta["finalPath"] = finalPath;
    {
        QFile mf2(metaPath);
        if (mf2.open(QIODevice::WriteOnly)) {
            mf2.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
            mf2.close();
        }
    }

    QString md5Str;
    {
        QFile md5File(finalPath);
        QCryptographicHash md5Hash(QCryptographicHash::Md5);
        if (md5File.open(QIODevice::ReadOnly)) {
            md5Hash.addData(&md5File);
            md5Str = QString(md5Hash.result().toHex());
            md5File.close();
        }
    }
    if (md5Str.isEmpty())
        md5Str = QString::number(videoId);

    QString mediaDir = hlsOutputDir + "/" + md5Str + "/";
    QDir().mkpath(mediaDir);

    {
        QDir mediaDirObj(mediaDir);
        const QFileInfoList stale = mediaDirObj.entryInfoList(
            QStringList() << QStringLiteral("segment_*.ts")
                          << QStringLiteral("output.m3u8"),
            QDir::Files);
        for (const QFileInfo &fi : stale)
            QFile::remove(fi.absoluteFilePath());
    }

    bool hlsOk = false;
    QString outputM3U8 = mediaDir + "output.m3u8";
    if (remuxToHls(finalPath, outputM3U8) && QFile::exists(outputM3U8)) {
        const QString httpBase = QString("http://%1:%2/hls/%3/")
            .arg(hlsIp).arg(hlsPort).arg(md5Str);
        // 保留 ffmpeg 输出的相对分片路径（segment_000.ts），避免 IP 变更后 m3u8 内旧绝对 URL 失效
        hlsOk = true;
        const QString hlsUrl = httpBase + QStringLiteral("output.m3u8");
        m_db->updateVideoHlsUrl(videoId, hlsUrl);
        qDebug() << "HLS 已生成：" << hlsUrl;
    } else {
        qDebug() << "HLS 转封装失败，视频文件仍保存在：" << finalPath;
    }

    QString coverPath = mediaDir + "cover.jpg";
    if (extractCompositeCoverImage(finalPath, coverPath, title)) {
        QString coverUrlHttp = QString("http://%1:%2/hls/%3/cover.jpg")
            .arg(hlsIp).arg(hlsPort).arg(md5Str);
        m_db->updateVideoCover(videoId, coverUrlHttp);
        qDebug() << "封面已生成：" << coverPath;
    } else {
        qDebug() << "封面生成失败：" << coverPath;
    }

    if (m_redis && m_redis->available()) {
        m_redis->clearUploadSession(uploadId);
        m_redis->invalidateVideoCaches();
        m_redis->invalidateVideoCaches(videoId);
    }

    QJsonObject respData;
    respData["videoId"] = videoId;
    respData["hlsOk"] = hlsOk;
    respData["coverOk"] = QFile::exists(coverPath);
    sendResponse(RESP_SUCCESS, "上传完成", "upload_end", respData);
}

// ============================================================
// 分块上传：请求续传（查询已接收的块）
// JSON: {"uploadId": "..."}
// 响应：{"code": 200, "message": "续传查询成功", "data": {"uploadId": "...", "nextChunkIndex": 5}}
// ============================================================
void ClientHandler::handleUploadResume(const QJsonObject &json)
{
    if (!ensureAuth(json, QStringLiteral("upload_resume")))
        return;

    QString uploadId = json["uploadId"].toVariant().toString();

    if (!isValidUploadId(uploadId)) {
        sendResponse(RESP_SERVER_ERROR, "参数错误", "upload_resume");
        return;
    }

    const QString uploadDir = resolveUploadDir(uploadId);
    if (uploadDir.isEmpty()) {
        sendResponse(RESP_SERVER_ERROR, "上传路径无效", "upload_resume");
        return;
    }

    QString metaPath = uploadDir + QStringLiteral("/meta.json");
    QJsonObject meta;
    if (m_redis && m_redis->available() && m_redis->loadUploadMeta(uploadId, &meta)) {
        // Redis 元数据优先
    } else {
        QFile metaFile(metaPath);
        if (!metaFile.exists() || !metaFile.open(QIODevice::ReadOnly)) {
            sendResponse(RESP_SERVER_ERROR, "上传会话不存在", "upload_resume");
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        metaFile.close();
        if (!doc.isObject()) {
            sendResponse(RESP_SERVER_ERROR, "元数据损坏", "upload_resume");
            return;
        }
        meta = doc.object();
    }

    const int sessionUserId = m_userId > 0 ? m_userId : m_db->getUserId(m_username);
    if (!isUploadOwnedBy(meta["userId"].toInt(-1), sessionUserId)) {
        sendResponse(RESP_SERVER_ERROR, "无权访问该上传会话", "upload_resume");
        return;
    }

    QJsonArray received = meta["receivedChunks"].toArray();
    int         totalChunks = meta["totalChunks"].toInt();

    QSet<int> receivedSet;
    if (m_redis && m_redis->available()) {
        m_redis->uploadChunksComplete(uploadId, totalChunks, &receivedSet);
    }
    if (receivedSet.isEmpty()) {
        for (const QJsonValue &v : received) {
            const int idx = v.toInt(-1);
            if (idx >= 0 && idx < totalChunks)
                receivedSet.insert(idx);
        }
    }

    int nextChunkIndex = totalChunks;
    for (int i = 0; i < totalChunks; ++i) {
        if (!receivedSet.contains(i)) {
            nextChunkIndex = i;
            break;
        }
    }

    qDebug() << "续传查询：" << uploadId << ", nextChunk =" << nextChunkIndex;

    QJsonObject data;
    data["uploadId"]      = uploadId;
    data["nextChunkIndex"] = nextChunkIndex;
    data["receivedCount"]  = receivedSet.size();
    data["totalChunks"]    = totalChunks;

    sendResponse(RESP_SUCCESS, "续传查询成功", "upload_resume", data);
}

// ============================================================
// 分块下载：开始下载
// JSON: {"videoId": 1, "chunkSize": 65536}
// 响应：{"code": 200, "message": "下载开始成功", "data": {"videoId": 1, "fileSize": ..., "totalChunks": ...}}
// ============================================================
void ClientHandler::handleDownloadStart(const QJsonObject &json)
{
    int videoId = json["videoId"].toInt(-1);
    int chunkSize = json["chunkSize"].toInt(DEFAULT_CHUNK_SIZE);  // 【P0修复】使用常量
    int startChunkIndex = json["startChunkIndex"].toInt(0);      // 【P1修复】支持断点续传

    if (videoId < 0 || chunkSize <= 0) {
        sendResponse(RESP_SERVER_ERROR, "参数错误");
        return;
    }

    QVariantMap video = m_db->getVideoById(videoId);
    if (video.isEmpty()) {
        sendResponse(RESP_USER_NOT_FOUND, "视频不存在");
        return;
    }

    QString videoPath = video["videoUrl"].toString();
    QFile   file(videoPath);

    if (!file.exists()) {
        sendResponse(RESP_SERVER_ERROR, "视频文件不存在");
        return;
    }

    qint64 fileSize   = file.size();
    int    totalChunks = (fileSize + chunkSize - 1) / chunkSize;  // 向上取整

    qDebug() << "开始下载：" << videoId << ", 大小：" << fileSize
             << "字节, 共" << totalChunks << "块, 从第"
             << startChunkIndex << "块开始";

    QJsonObject data;
    data["videoId"]         = videoId;
    data["fileSize"]        = fileSize;
    data["totalChunks"]    = totalChunks;
    data["chunkSize"]       = chunkSize;
    data["startChunkIndex"] = startChunkIndex;   // 【P1修复】告知客户端从哪个块开始
    data["fileName"]        = QFileInfo(videoPath).fileName();

    sendResponse(RESP_SUCCESS, "下载开始成功", "download_start", data);
}

// ============================================================
// 分块下载：发送数据块
// JSON: {"videoId": 1, "chunkIndex": 0, "chunkSize": 65536}
// 响应：{"code": 200, "message": "下载块成功", "data": {"chunkIndex": 0, "chunkData": "<base64>"}}
// 【Phase1】文件读取在 QThreadPool 中异步执行
// ============================================================
void ClientHandler::handleDownloadChunk(const QJsonObject &json)
{
    int videoId    = json["videoId"].toInt(-1);
    int chunkIndex  = json["chunkIndex"].toInt(-1);
    int chunkSize   = json["chunkSize"].toInt(DEFAULT_CHUNK_SIZE);

    if (videoId < 0 || chunkIndex < 0 || chunkSize <= 0) {
        sendResponse(RESP_SERVER_ERROR, "参数错误");
        return;
    }

    // 查询视频路径（DB 查询在主线程，受 QMutex 保护）
    QVariantMap video = m_db->getVideoById(videoId);
    if (video.isEmpty()) {
        sendResponse(RESP_USER_NOT_FOUND, "视频不存在");
        return;
    }

    QString videoPath = video["videoUrl"].toString();

    QFile file(videoPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        sendResponse(RESP_SERVER_ERROR, "无法读取视频文件");
        return;
    }

    if (!file.seek(static_cast<qint64>(chunkIndex) * chunkSize)) {
        file.close();
        sendResponse(RESP_SERVER_ERROR, "文件定位失败");
        return;
    }

    QByteArray chunkData = file.read(chunkSize);
    file.close();

    if (chunkData.isEmpty()) {
        sendResponse(RESP_SERVER_ERROR, "块数据为空（已到文件末尾）");
        return;
    }

    QString chunkBase64 = QString::fromLatin1(chunkData.toBase64());

    qDebug() << "发送块：" << videoId << ", chunk" << chunkIndex
             << ", 大小：" << chunkData.size() << "字节";

    QJsonObject data;
    data["chunkIndex"] = chunkIndex;
    data["chunkData"]  = chunkBase64;
    data["chunkSize"]  = chunkData.size();

    sendResponse(RESP_SUCCESS, "下载块成功", "download_chunk", data);
}

// ============================================================
// 个人中心：获取我上传的视频列表
// JSON: {}
// 响应：{"code": 200, "message": "我的视频列表", "data": {"videos": [...]}}
// ============================================================
void ClientHandler::handleGetMyVideos(const QJsonObject &json)
{
    if (!ensureAuth(json, QStringLiteral("my_videos")))
        return;

    int userId = m_userId > 0 ? m_userId : m_db->getUserId(m_username);
    if (userId < 0) {
        sendResponse(RESP_SERVER_ERROR, "用户不存在", "my_videos");
        return;
    }

    QVariantList list = m_db->getUserVideos(userId, 0, 100);
    QJsonArray videoArray;
    for (const QVariant &v : list) {
        QVariantMap video = v.toMap();
        QJsonObject obj;
        obj["id"]         = video["id"].toInt();
        obj["title"]      = video["title"].toString();
        obj["playCount"]  = video["playCount"].toInt();
        obj["coverUrl"]   = rewriteHlsHttpUrl(video["coverUrl"].toString(), m_hlsIp, m_hlsPort);
        obj["createdAt"]  = video["createdAt"].toString();
        obj["hlsUrl"]     = rewriteHlsHttpUrl(video["hlsUrl"].toString(), m_hlsIp, m_hlsPort);
        videoArray.append(obj);
    }

    QJsonObject data;
    data["videos"] = videoArray;
    sendResponse(RESP_SUCCESS, "我的视频列表", "my_videos", data);
}

// ============================================================
// 个人中心：删除视频
// JSON: {"videoId": 1}
// 响应：{"code": 200, "message": "删除视频|成功"}
// ============================================================
void ClientHandler::handleDeleteVideo(const QJsonObject &json)
{
    if (!ensureAuth(json, QStringLiteral("delete_video")))
        return;

    int videoId = json["videoId"].toInt(-1);
    if (videoId <= 0) {
        sendResponse(RESP_SERVER_ERROR, "无效的视频ID", "delete_video");
        return;
    }

    int userId = m_userId > 0 ? m_userId : m_db->getUserId(m_username);
    if (userId < 0) {
        sendResponse(RESP_SERVER_ERROR, "用户不存在", "delete_video");
        return;
    }

    if (m_db->deleteVideo(videoId, userId)) {
        if (m_redis && m_redis->available()) {
            m_redis->invalidateVideoCaches(videoId);
            m_redis->invalidateVideoCaches();
        }
        sendResponse(RESP_SUCCESS, "成功", "delete_video");
    } else {
        sendResponse(RESP_SERVER_ERROR, "失败（视频不存在或无权删除）", "delete_video");
    }
}

// ============================================================
// Server 类实现
// ============================================================

// ============================================================
// Server 构造函数
// ============================================================
Server::Server(const QString &hlsIp, int hlsPort,
               const QString &hlsOutputDir,
               const RedisConfig &redisCfg,
               QObject *parent)
    : QObject(parent),
      m_threadPool(std::max(2u, std::thread::hardware_concurrency())),
      m_epollServer(&m_threadPool),
      m_redisCfg(redisCfg),
      m_hlsIp(hlsIp), m_hlsPort(hlsPort),
      m_hlsOutputDir(hlsOutputDir)
{
    m_redis.init(m_redisCfg);

    m_epollServer.setNewConnectionCallback(
        [this](int fd, const QString &peerAddr) {
            return createClientHandler(fd, peerAddr);
        });
    m_epollServer.setDisconnectedCallback(
        [this](int fd, const std::shared_ptr<ClientHandler> &handler) {
            onClientDisconnected(fd, handler);
        });
}

// ============================================================
// Server 析构函数
// ============================================================
Server::~Server()
{
    stop();
}

// ============================================================
// 启动服务器
// 参数：port = 监听端口
// 返回：true=成功, false=失败
// ============================================================
bool Server::start(quint16 port)
{
    qDebug() << "正在初始化数据库...";

    // 初始化数据库（使用默认连接参数）
    if (!m_database.init()) {
        qDebug() << "数据库初始化失败";
        return false;
    }
    
    if (!m_epollServer.listen(port)) {
        return false;
    }

    qDebug() << "==========================================";
    qDebug() << "  短视频录制平台 - 服务器启动成功";
    qDebug() << "  模型：epoll + 线程池（" << m_threadPool.size() << " 工作线程）";
    qDebug() << "  Redis：" << (m_redis.available() ? QStringLiteral("已连接") : QStringLiteral("未启用/降级"));
    qDebug() << "  TCP 端口：" << port;
    if (m_hlsPort > 0)
        qDebug() << "  HLS 静态服务（Nginx）：http://" << m_hlsIp << ":" << m_hlsPort << "/hls/";
    else
        qDebug() << "  警告：hls_port 未配置，客户端将无法获取有效播放地址";
    qDebug() << "  HLS 输出目录：" << m_hlsOutputDir;
    qDebug() << "  等待客户端连接...";
    qDebug() << "==========================================";

    // 启动上传会话清理定时器（每 5 分钟清理超过 1 小时的过期 session）
    connect(&m_cleanupTimer, &QTimer::timeout, this, &Server::onCleanupTimer);
    m_cleanupTimer.start(5 * 60 * 1000);
    qDebug() << "上传会话清理定时器已启动（间隔 5 分钟）";

    if (m_redis.available()) {
        connect(&m_counterFlushTimer, &QTimer::timeout, this, &Server::onCounterFlushTimer);
        const int flushMs = qMax(5, m_redisCfg.counterFlushSec) * 1000;
        m_counterFlushTimer.start(flushMs);
        qDebug() << "播放量计数刷盘定时器已启动（间隔" << m_redisCfg.counterFlushSec << "秒）";
    }

    return true;
}

// ============================================================
// 停止服务器
// ============================================================
void Server::stop()
{
    m_counterFlushTimer.stop();
    onCounterFlushTimer();

    m_cleanupTimer.stop();
    m_epollServer.stop();
    m_threadPool.shutdown();
    m_clients.clear();
    qDebug() << "服务器已停止";
}

std::shared_ptr<ClientHandler> Server::createClientHandler(int fd, const QString &peerAddr)
{
    EpollServer *epoll = &m_epollServer;
    auto sendFunc = [epoll, fd](const QByteArray &data) {
        epoll->enqueueSend(fd, data);
    };

    auto handler = std::make_shared<ClientHandler>(
        fd, peerAddr, sendFunc, &m_database, &m_redis,
        m_hlsIp, m_hlsPort, m_hlsOutputDir);
    m_clients.insert(fd, handler);
    return handler;
}

void Server::onClientDisconnected(int fd, const std::shared_ptr<ClientHandler> &handler)
{
    Q_UNUSED(handler);
    m_clients.remove(fd);
}

// ============================================================
// 定期清理过期上传会话（P1-1）
// 删除超过 1 小时未更新的 uploads/ 子目录
// 使用 Qt 5.0+ 兼容的 API（toMSecsSinceEpoch）
// ============================================================
void Server::onCleanupTimer()
{
    QString uploadsDir = QCoreApplication::applicationDirPath() + "/uploads";
    QDir dir(uploadsDir);
    if (!dir.exists()) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;  // Qt 4.7+ 兼容
    const qint64 MAX_AGE_SECS = 3600;  // 1 小时

    QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    int cleaned = 0;
    for (const QString &subdir : entries) {
        QString subPath = uploadsDir + "/" + subdir;
        // 检查 meta.json 的最后修改时间
        QString metaPath = subPath + "/meta.json";
        QFileInfo metaFi(metaPath);
        if (metaFi.exists()) {
            qint64 age = now - (metaFi.lastModified().toMSecsSinceEpoch() / 1000);
            if (age > MAX_AGE_SECS) {
                // 检查是否已完成
                QFile mf(metaPath);
                if (mf.open(QIODevice::ReadOnly)) {
                    QJsonDocument doc = QJsonDocument::fromJson(mf.readAll());
                    mf.close();
                    if (doc.isObject() && doc.object()["status"].toString() == "completed") {
                        continue;  // 已完成的上传保留
                    }
                }
                QDir(subPath).removeRecursively();
                cleaned++;
            }
        } else {
            // 没有 meta.json 的奇怪目录，检查目录修改时间
            QFileInfo dirFi(subPath);
            qint64 age = now - (dirFi.lastModified().toMSecsSinceEpoch() / 1000);
            if (age > MAX_AGE_SECS) {
                QDir(subPath).removeRecursively();
                cleaned++;
            }
        }
    }
    if (cleaned > 0) {
        qDebug() << "清理了" << cleaned << "个过期上传会话";
    }
}

void Server::onCounterFlushTimer()
{
    if (!m_redis.available())
        return;

    const QVector<QPair<int, int>> deltas = m_redis.drainPlayCounters();
    for (const QPair<int, int> &item : deltas) {
        if (!m_database.addPlayCountDelta(item.first, item.second)) {
            qDebug() << "播放量刷盘失败：videoId=" << item.first
                     << "delta=" << item.second;
            continue;
        }
        m_redis.invalidateVideoCaches(item.first);
    }
    if (!deltas.isEmpty())
        m_redis.invalidateVideoCaches();
}

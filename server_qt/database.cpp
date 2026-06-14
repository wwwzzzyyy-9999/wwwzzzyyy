#include "database.h"
#include "protocol.h"
#include "auth_util.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>

namespace {

bool constantTimeEqualUtf8(const QByteArray &a, const QByteArray &b)
{
    const int maxLen = qMax(a.size(), b.size());
    volatile char diff = 0;
    for (int i = 0; i < maxLen; ++i) {
        const char ca = (i < a.size()) ? a[i] : 0;
        const char cb = (i < b.size()) ? b[i] : 0;
        diff |= static_cast<char>(ca ^ cb);
    }
    return diff == 0 && a.size() == b.size();
}

static QByteArray normalizeHexUtf8(const QByteArray &bytes)
{
    if (bytes.size() == 64)
        return bytes.toLower();
    return bytes;
}

// 比对客户端哈希与库中记录；兼容早期固定盐/明文入库
bool verifyStoredPassword(const QString &stored, const QString &clientSent)
{
    const QString storedTrim = stored.trimmed();
    const QString clientTrim = clientSent.trimmed();
    const QByteArray storedBytes = normalizeHexUtf8(storedTrim.toUtf8());
    QByteArray clientBytes = normalizeHexUtf8(clientTrim.toUtf8());

    if (constantTimeEqualUtf8(storedBytes, clientBytes))
        return true;

    if (!isSha256Hex(storedTrim)) {
        const QString legacyHash = authLegacyPasswordHash(storedTrim);
        if (constantTimeEqualUtf8(normalizeHexUtf8(legacyHash.toUtf8()), clientBytes)) {
            qDebug() << "登录：明文密码已匹配（建议用户重新注册以升级存储）";
            return true;
        }
    }

    if (!isSha256Hex(clientTrim)) {
        const QString fromPlain = authLegacyPasswordHash(clientTrim);
        if (constantTimeEqualUtf8(storedBytes, normalizeHexUtf8(fromPlain.toUtf8())))
            return true;
    }

    return false;
}

static void ensureUsersSaltColumn(QSqlDatabase &db)
{
    QSqlQuery alterQuery;
    const QString alterTable = QStringLiteral(
        "ALTER TABLE users ADD COLUMN IF NOT EXISTS salt VARCHAR(64) DEFAULT ''");
    if (alterQuery.exec(alterTable))
        return;

    QSqlQuery checkCol;
    checkCol.prepare(
        "SELECT COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS "
        "WHERE TABLE_NAME = 'users' AND COLUMN_NAME = 'salt'");
    if (checkCol.exec() && !checkCol.next()) {
        QSqlQuery addCol;
        if (!addCol.exec("ALTER TABLE users ADD COLUMN salt VARCHAR(64) DEFAULT ''"))
            qDebug() << "添加 salt 列失败：" << addCol.lastError().text();
        else
            qDebug() << "已添加 salt 列到 users 表";
    }
}

} // namespace

// ===========================================================
// 构造函数
// ===========================================================
Database::Database(QObject *parent) : QObject(parent)
{
    // 使用 MySQL 驱动
    m_db = QSqlDatabase::addDatabase("QMYSQL");
}

// ===========================================================
// 析构函数
// ===========================================================
Database::~Database()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

// ===========================================================
// 初始化数据库
// 创建 users 表（如果不存在）
// 返回：true=成功, false=失败
// ===========================================================
bool Database::init(const QString &host, const QString &user,
                     const QString &password, const QString &dbName)
{
    // 设置 MySQL 连接参数
    m_db.setHostName(host);
    m_db.setUserName(user);
    m_db.setPassword(password);
    m_db.setDatabaseName(dbName);

    if (!m_db.open()) {
        qDebug() << "数据库连接失败：" << m_db.lastError().text();
        return false;
    }

    // 创建 users 表
    QSqlQuery query;
    QString createUsersTable = QString(
        "CREATE TABLE IF NOT EXISTS users ("
        "    id         INT PRIMARY KEY AUTO_INCREMENT,"  // 用户ID（自增）
        "    username   VARCHAR(255) UNIQUE NOT NULL,"  // 用户名（唯一）
        "    password   VARCHAR(255) NOT NULL,"          // 密码（SHA-256密文）
        "    salt       VARCHAR(64) DEFAULT '',"         // 每用户随机盐
        "    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP" // 创建时间
        ");"
    );

    if (!query.exec(createUsersTable)) {
        qDebug() << "创建 users 表失败：" << query.lastError().text();
        return false;
    }

    ensureUsersSaltColumn(m_db);

    // 创建 videos 表
    QString createVideosTable = QString(
        "CREATE TABLE IF NOT EXISTS videos ("
        "    id           INT PRIMARY KEY AUTO_INCREMENT," // 视频ID
        "    user_id      INT NOT NULL,"                  // 上传者ID
        "    title        TEXT,"                             // 标题
        "    description  TEXT,"                             // 描述
        "    cover_url    TEXT,"                             // 封面路径
        "    video_url    TEXT NOT NULL,"                    // 视频路径
        "    hls_url      VARCHAR(255) DEFAULT '',"        // HLS 播放地址
        "    duration     INT,"                               // 时长（秒）
        "    play_count   INT DEFAULT 0,"                 // 播放量
        "    likes_count  INT DEFAULT 0,"                 // 点赞数
        "    comments_count INT DEFAULT 0,"               // 评论数
        "    created_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (user_id) REFERENCES users(id)"
        ");"
    );

    if (!query.exec(createVideosTable)) {
        qDebug() << "创建 videos 表失败：" << query.lastError().text();
        return false;
    }

    // 【P0修复】如果表已存在但缺少 hls_url 列，则添加
    QSqlQuery alterQuery;
    QString alterTable = QString(
        "ALTER TABLE videos ADD COLUMN IF NOT EXISTS hls_url VARCHAR(255) DEFAULT ''"
    );
    if (!alterQuery.exec(alterTable)) {
        // MySQL 5.7 不支持 IF NOT EXISTS，尝试另一种方式
        // 先检查列是否存在
        QSqlQuery checkCol;
        checkCol.prepare(
            "SELECT COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS "
            "WHERE TABLE_NAME = 'videos' AND COLUMN_NAME = 'hls_url'"
        );
        if (checkCol.exec() && !checkCol.next()) {
            // 列不存在，添加
            QSqlQuery addCol;
            if (!addCol.exec("ALTER TABLE videos ADD COLUMN hls_url VARCHAR(255) DEFAULT ''")) {
                qDebug() << "添加 hls_url 列失败：" << addCol.lastError().text();
            } else {
                qDebug() << "已添加 hls_url 列到 videos 表";
            }
        }
    } else {
        qDebug() << "已添加 hls_url 列到 videos 表（或已存在）";
    }

    QString createVideoLikesTable = QString(
        "CREATE TABLE IF NOT EXISTS video_likes ("
        "    user_id    INT NOT NULL,"
        "    video_id   INT NOT NULL,"
        "    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "    PRIMARY KEY (user_id, video_id),"
        "    FOREIGN KEY (user_id) REFERENCES users(id),"
        "    FOREIGN KEY (video_id) REFERENCES videos(id)"
        ");"
    );
    if (!query.exec(createVideoLikesTable)) {
        qDebug() << "创建 video_likes 表失败：" << query.lastError().text();
        return false;
    }

    qDebug() << "数据库初始化成功，users / videos / video_likes 表已就绪";
    return true;
}

// ===========================================================
// 用户注册
// 将用户名和客户端已加密的密码直接写入数据库
// 返回：true=成功, false=失败（用户已存在或数据库错误）
// ===========================================================
bool Database::registerUser(const QString &username, const QString &salt,
                            const QString &passwordHash)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery checkQuery;
    checkQuery.prepare("SELECT id FROM users WHERE username = ?");
    checkQuery.addBindValue(username);
    if (checkQuery.exec() && checkQuery.next()) {
        qDebug() << "注册失败：用户名已存在";
        return false;
    }

    QSqlQuery query;
    query.prepare("INSERT INTO users (username, password, salt) VALUES (?, ?, ?)");
    query.addBindValue(username);
    query.addBindValue(passwordHash);
    query.addBindValue(salt);

    if (!query.exec()) {
        qDebug() << "注册失败：数据库错误 -" << query.lastError().text();
        return false;
    }

    qDebug() << "注册成功：用户" << username;
    return true;
}

// ===========================================================
// 用户登录验证
// 直接比对客户端发来的密码哈希与数据库存储值
// 返回：true=验证成功, false=失败
// ===========================================================
QString Database::getPasswordSalt(const QString &username, bool *found)
{
    QMutexLocker locker(&m_mutex);
    if (found)
        *found = false;

    QSqlQuery query;
    query.prepare("SELECT salt FROM users WHERE username = ?");
    query.addBindValue(username);

    if (!query.exec() || !query.next())
        return QString();

    if (found)
        *found = true;
    return query.value(0).toString();
}

bool Database::loginUser(const QString &username, const QString &passwordHash)
{
    QMutexLocker locker(&m_mutex);
    QSqlQuery query;
    query.prepare("SELECT password, salt FROM users WHERE username = ?");
    query.addBindValue(username);

    if (!query.exec()) {
        qDebug() << "登录查询失败：" << query.lastError().text();
        return false;
    }

    if (!query.next()) {
        qDebug() << "登录失败：用户不存在";
        return false;
    }

    const QString dbPassword = query.value(0).toString();
    const QString dbSalt = query.value(1).toString();

    bool matched = false;
    if (!dbSalt.isEmpty()) {
        const QByteArray storedBytes = normalizeHexUtf8(dbPassword.trimmed().toUtf8());
        const QByteArray clientBytes = normalizeHexUtf8(passwordHash.trimmed().toUtf8());
        matched = constantTimeEqualUtf8(storedBytes, clientBytes);
    } else {
        matched = verifyStoredPassword(dbPassword, passwordHash);
    }

    if (matched) {
        if (!isSha256Hex(dbPassword)) {
            QSqlQuery up;
            up.prepare(QStringLiteral("UPDATE users SET password = ? WHERE username = ?"));
            up.addBindValue(passwordHash.trimmed());
            up.addBindValue(username);
            if (up.exec())
                qDebug() << "已将用户" << username << "的密码存储升级为哈希";
        }
        qDebug() << "登录成功：用户" << username;
        return true;
    }
    qDebug() << "登录失败：密码错误";
    return false;
}

// ===========================================================
// 密码加密（SHA-256）
// ===========================================================

// ===========================================================
// 检查用户是否存在
// 返回：true=存在, false=不存在
// ===========================================================
bool Database::userExists(const QString &username)
{
    QMutexLocker locker(&m_mutex);
    QSqlQuery query;
    query.prepare("SELECT id FROM users WHERE username = ?");
    query.addBindValue(username);
    
    if (!query.exec()) {
        qDebug() << "查询失败：" << query.lastError().text();
        return false;
    }
    
    return query.next();  // 有结果说明用户存在
}

// ===========================================================
// 根据用户名获取用户ID
// 返回：用户ID（>0），不存在返回 -1
// ===========================================================
int Database::getUserId(const QString &username)
{
    QMutexLocker locker(&m_mutex);
    QSqlQuery query;
    query.prepare("SELECT id FROM users WHERE username = ?");
    query.addBindValue(username);
    
    if (!query.exec() || !query.next()) {
        return -1;
    }
    
    return query.value(0).toInt();
}

// ===========================================================
// videos 表操作
// ===========================================================

// 插入视频记录
int Database::insertVideo(int userId, const QString &title,
                           const QString &coverUrl, const QString &videoUrl,
                           int duration, const QString &hlsUrl)
{
    QMutexLocker locker(&m_mutex);
    QSqlQuery query;
    query.prepare(
        "INSERT INTO videos (user_id, title, cover_url, video_url, duration, hls_url) "
        "VALUES (?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(userId);
    query.addBindValue(title);
    query.addBindValue(coverUrl);
    query.addBindValue(videoUrl);
    query.addBindValue(duration);
    query.addBindValue(hlsUrl);

    if (!query.exec()) {
        qDebug() << "插入视频失败：" << query.lastError().text();
        return -1;
    }

    int videoId = query.lastInsertId().toInt();
    qDebug() << "插入视频成功，ID：" << videoId;
    return videoId;
}

// 获取视频列表（分页）
QVariantList Database::getVideoList(int offset, int limit)
{
    QMutexLocker locker(&m_mutex);
    QVariantList list;
    QSqlQuery query;
    query.prepare(
        "SELECT v.id, v.title, v.cover_url, v.play_count, "
        "       v.likes_count, v.created_at, u.username, v.hls_url "
        "FROM videos v "
        "LEFT JOIN users u ON v.user_id = u.id "
        "ORDER BY v.created_at DESC "
        "LIMIT ? OFFSET ?"
    );
    query.addBindValue(limit);
    query.addBindValue(offset);

    if (!query.exec()) {
        qDebug() << "查询视频列表失败：" << query.lastError().text();
        return list;
    }

    while (query.next()) {
        QVariantMap video;
        video["id"]           = query.value("id").toInt();
        video["title"]        = query.value("title").toString();
        video["coverUrl"]     = query.value("cover_url").toString();
        video["playCount"]    = query.value("play_count").toInt();
        video["likesCount"]   = query.value("likes_count").toInt();
        video["createdAt"]    = query.value("created_at").toString();
        video["author"]       = query.value("username").toString();
        video["hlsUrl"]      = query.value("hls_url").toString();  // 【新增】HLS 播放地址
        list.append(video);
    }

    return list;
}

QVariantList Database::searchVideos(const QString &keyword, int offset, int limit)
{
    QMutexLocker locker(&m_mutex);
    QVariantList list;

    QString kw = keyword.trimmed();
    kw.replace(QLatin1Char('%'), QString());
    kw.replace(QLatin1Char('_'), QString());
    if (kw.isEmpty())
        return list;

    const QString pattern = QLatin1Char('%') + kw + QLatin1Char('%');

    QSqlQuery query;
    query.prepare(
        "SELECT v.id, v.title, v.cover_url, v.play_count, "
        "       v.likes_count, v.created_at, u.username, v.hls_url "
        "FROM videos v "
        "LEFT JOIN users u ON v.user_id = u.id "
        "WHERE v.title LIKE ? OR u.username LIKE ? OR v.description LIKE ? "
        "ORDER BY v.created_at DESC "
        "LIMIT ? OFFSET ?"
    );
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(limit);
    query.addBindValue(offset);

    if (!query.exec()) {
        qDebug() << "搜索视频失败：" << query.lastError().text();
        return list;
    }

    while (query.next()) {
        QVariantMap video;
        video["id"]         = query.value("id").toInt();
        video["title"]      = query.value("title").toString();
        video["coverUrl"]   = query.value("cover_url").toString();
        video["playCount"]  = query.value("play_count").toInt();
        video["likesCount"] = query.value("likes_count").toInt();
        video["createdAt"]  = query.value("created_at").toString();
        video["author"]     = query.value("username").toString();
        video["hlsUrl"]     = query.value("hls_url").toString();
        list.append(video);
    }

    return list;
}

QVariantList Database::getHotVideoList(int offset, int limit,
                                       double alpha, double beta,
                                       double T, double gamma)
{
    QMutexLocker locker(&m_mutex);
    QVariantList list;
    QSqlQuery query;
    query.prepare(
        "SELECT v.id, v.title, v.cover_url, v.play_count, "
        "       v.likes_count, v.comments_count, v.created_at, u.username, v.hls_url, "
        "       ( "
        "           (v.play_count + ? * v.likes_count + ? * v.comments_count) "
        "           / POW( "
        "               GREATEST(TIMESTAMPDIFF(HOUR, v.created_at, NOW()), 0) + ?, "
        "               ? "
        "           ) "
        "       ) AS hot_score "
        "FROM videos v "
        "LEFT JOIN users u ON v.user_id = u.id "
        "ORDER BY hot_score DESC, v.id DESC "
        "LIMIT ? OFFSET ?"
    );
    query.addBindValue(alpha);
    query.addBindValue(beta);
    query.addBindValue(T);
    query.addBindValue(gamma);
    query.addBindValue(limit);
    query.addBindValue(offset);

    if (!query.exec()) {
        qDebug() << "查询热度视频列表失败：" << query.lastError().text();
        return list;
    }

    while (query.next()) {
        QVariantMap video;
        video["id"]            = query.value("id").toInt();
        video["title"]         = query.value("title").toString();
        video["coverUrl"]      = query.value("cover_url").toString();
        video["playCount"]     = query.value("play_count").toInt();
        video["likesCount"]    = query.value("likes_count").toInt();
        video["commentsCount"] = query.value("comments_count").toInt();
        video["createdAt"]     = query.value("created_at").toString();
        video["author"]        = query.value("username").toString();
        video["hlsUrl"]        = query.value("hls_url").toString();
        video["hotScore"]      = query.value("hot_score").toDouble();
        list.append(video);
    }

    return list;
}

// 根据ID获取视频信息
QVariantMap Database::getVideoById(int videoId)
{
    QMutexLocker locker(&m_mutex);
    QVariantMap video;
    QSqlQuery query;
    query.prepare(
        "SELECT v.*, u.username "
        "FROM videos v "
        "LEFT JOIN users u ON v.user_id = u.id "
        "WHERE v.id = ?"
    );
    query.addBindValue(videoId);

    if (!query.exec() || !query.next()) {
        qDebug() << "查询视频信息失败：" << query.lastError().text();
        return video;
    }

    video["id"]           = query.value("id").toInt();
    video["userId"]       = query.value("user_id").toInt();
    video["title"]        = query.value("title").toString();
    video["description"]  = query.value("description").toString();
    video["coverUrl"]     = query.value("cover_url").toString();
    video["videoUrl"]     = query.value("video_url").toString();
    video["hlsUrl"]      = query.value("hls_url").toString();  // 【新增】HLS 播放地址
    video["duration"]     = query.value("duration").toInt();
    video["playCount"]    = query.value("play_count").toInt();
    video["likesCount"]   = query.value("likes_count").toInt();
    video["author"]       = query.value("username").toString();

    return video;
}

// 获取某用户的视频列表
QVariantList Database::getUserVideos(int userId, int offset, int limit)
{
    QMutexLocker locker(&m_mutex);
    QVariantList list;
    QSqlQuery query;
    query.prepare(
        "SELECT id, title, cover_url, play_count, likes_count, created_at, hls_url "
        "FROM videos WHERE user_id = ? "
        "ORDER BY created_at DESC LIMIT ? OFFSET ?"
    );
    query.addBindValue(userId);
    query.addBindValue(limit);
    query.addBindValue(offset);

    if (!query.exec()) {
        qDebug() << "查询用户视频失败：" << query.lastError().text();
        return list;
    }

    while (query.next()) {
        QVariantMap video;
        video["id"]         = query.value("id").toInt();
        video["title"]      = query.value("title").toString();
        video["coverUrl"]   = query.value("cover_url").toString();
        video["playCount"]  = query.value("play_count").toInt();
        video["likesCount"] = query.value("likes_count").toInt();
        video["createdAt"]  = query.value("created_at").toString();
        video["hlsUrl"]    = query.value("hls_url").toString();  // 【新增】HLS 播放地址
        list.append(video);
    }

    return list;
}

// ===========================================================
// 更新视频的 HLS 地址
// ===========================================================
bool Database::updateVideoHlsUrl(int videoId, const QString &hlsUrl)
{
    QMutexLocker locker(&m_mutex);
    QSqlQuery query;
    query.prepare("UPDATE videos SET hls_url = ? WHERE id = ?");
    query.addBindValue(hlsUrl);
    query.addBindValue(videoId);

    if (!query.exec()) {
        qDebug() << "更新 HLS 地址失败：" << query.lastError().text();
        return false;
    }

    qDebug() << "HLS 地址已更新：videoId =" << videoId << ", hlsUrl =" << hlsUrl;
    return true;
}

// ============================================================
// 更新视频封面URL
// ============================================================
bool Database::updateVideoCover(int videoId, const QString &coverUrl)
{
    QMutexLocker locker(&m_mutex);
    QSqlQuery query;
    query.prepare("UPDATE videos SET cover_url = ? WHERE id = ?");
    query.addBindValue(coverUrl);
    query.addBindValue(videoId);

    if (!query.exec()) {
        qDebug() << "更新封面 URL 失败：" << query.lastError().text();
        return false;
    }

    qDebug() << "封面 URL 已更新：videoId =" << videoId << ", coverUrl =" << coverUrl;
    return true;
}

// 增加播放量
bool Database::incrementPlayCount(int videoId)
{
    QMutexLocker locker(&m_mutex);
    QSqlQuery query;
    query.prepare("UPDATE videos SET play_count = play_count + 1 WHERE id = ?");
    query.addBindValue(videoId);

    if (!query.exec()) {
        qDebug() << "增加播放量失败：" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool Database::addPlayCountDelta(int videoId, int delta)
{
    if (videoId <= 0 || delta <= 0)
        return false;

    QMutexLocker locker(&m_mutex);
    QSqlQuery query;
    query.prepare("UPDATE videos SET play_count = play_count + ? WHERE id = ?");
    query.addBindValue(delta);
    query.addBindValue(videoId);

    if (!query.exec()) {
        qDebug() << "批量增加播放量失败：" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

QVariantMap Database::toggleVideoLike(int videoId, int userId)
{
    QMutexLocker locker(&m_mutex);
    QVariantMap result;
    result["success"] = false;

    if (videoId <= 0 || userId <= 0) {
        result["message"] = QStringLiteral("参数无效");
        return result;
    }

    if (!m_db.transaction()) {
        result["message"] = QStringLiteral("数据库事务启动失败");
        return result;
    }

    QSqlQuery checkQuery;
    checkQuery.prepare(
        "SELECT 1 FROM video_likes WHERE user_id = ? AND video_id = ? LIMIT 1"
    );
    checkQuery.addBindValue(userId);
    checkQuery.addBindValue(videoId);
    if (!checkQuery.exec()) {
        m_db.rollback();
        result["message"] = checkQuery.lastError().text();
        return result;
    }

    const bool alreadyLiked = checkQuery.next();
    QSqlQuery mutateQuery;
    if (alreadyLiked) {
        mutateQuery.prepare("DELETE FROM video_likes WHERE user_id = ? AND video_id = ?");
        mutateQuery.addBindValue(userId);
        mutateQuery.addBindValue(videoId);
        if (!mutateQuery.exec() || mutateQuery.numRowsAffected() <= 0) {
            m_db.rollback();
            result["message"] = mutateQuery.lastError().text();
            return result;
        }

        QSqlQuery decQuery;
        decQuery.prepare(
            "UPDATE videos SET likes_count = GREATEST(likes_count - 1, 0) WHERE id = ?"
        );
        decQuery.addBindValue(videoId);
        if (!decQuery.exec()) {
            m_db.rollback();
            result["message"] = decQuery.lastError().text();
            return result;
        }
        result["liked"] = false;
    } else {
        mutateQuery.prepare("INSERT INTO video_likes (user_id, video_id) VALUES (?, ?)");
        mutateQuery.addBindValue(userId);
        mutateQuery.addBindValue(videoId);
        if (!mutateQuery.exec()) {
            m_db.rollback();
            result["message"] = mutateQuery.lastError().text();
            return result;
        }

        QSqlQuery incQuery;
        incQuery.prepare("UPDATE videos SET likes_count = likes_count + 1 WHERE id = ?");
        incQuery.addBindValue(videoId);
        if (!incQuery.exec()) {
            m_db.rollback();
            result["message"] = incQuery.lastError().text();
            return result;
        }
        result["liked"] = true;
    }

    QSqlQuery countQuery;
    countQuery.prepare("SELECT likes_count FROM videos WHERE id = ?");
    countQuery.addBindValue(videoId);
    if (!countQuery.exec() || !countQuery.next()) {
        m_db.rollback();
        result["message"] = countQuery.lastError().text();
        return result;
    }

    if (!m_db.commit()) {
        m_db.rollback();
        result["message"] = QStringLiteral("提交事务失败");
        return result;
    }

    result["success"] = true;
    result["likesCount"] = countQuery.value(0).toInt();
    result["videoId"] = videoId;
    return result;
}

QList<int> Database::getLikedVideoIds(int userId, const QList<int> &videoIds)
{
    QMutexLocker locker(&m_mutex);
    QList<int> likedIds;
    if (userId <= 0 || videoIds.isEmpty())
        return likedIds;

    QString placeholders;
    for (int i = 0; i < videoIds.size(); ++i) {
        if (i > 0)
            placeholders += QLatin1String(",");
        placeholders += QLatin1Char('?');
    }

    QSqlQuery query;
    query.prepare(QStringLiteral(
        "SELECT video_id FROM video_likes WHERE user_id = ? AND video_id IN (%1)"
    ).arg(placeholders));
    query.addBindValue(userId);
    for (int id : videoIds)
        query.addBindValue(id);

    if (!query.exec()) {
        qDebug() << "查询点赞状态失败：" << query.lastError().text();
        return likedIds;
    }

    while (query.next())
        likedIds.append(query.value(0).toInt());
    return likedIds;
}

bool Database::deleteVideo(int videoId, int userId)
{
    QMutexLocker locker(&m_mutex);

    // 先查询视频文件路径和 HLS URL（用于删除文件）
    QSqlQuery query;
    query.prepare("SELECT video_url, hls_url FROM videos WHERE id = ? AND user_id = ?");
    query.addBindValue(videoId);
    query.addBindValue(userId);

    if (!query.exec() || !query.next()) {
        qDebug() << "删除视频失败：视频不存在或无权删除";
        return false;
    }

    QString videoPath = query.value(0).toString();
    QString hlsUrl    = query.value(1).toString();

    QSqlQuery delLikesQuery;
    delLikesQuery.prepare("DELETE FROM video_likes WHERE video_id = ?");
    delLikesQuery.addBindValue(videoId);
    delLikesQuery.exec();

    // 删除数据库记录
    QSqlQuery delQuery;
    delQuery.prepare("DELETE FROM videos WHERE id = ? AND user_id = ?");
    delQuery.addBindValue(videoId);
    delQuery.addBindValue(userId);

    if (!delQuery.exec() || delQuery.numRowsAffected() <= 0) {
        qDebug() << "删除视频记录失败";
        return false;
    }

    // 删除物理文件
    QFile::remove(videoPath);

    // 删除 HLS 相关文件（ts/m3u8/cover.jpg 及其目录）
    if (!hlsUrl.isEmpty()) {
        // hlsUrl 格式：http://ip:port/hls/md5/output.m3u8
        // 需要提取本地路径
        int hlsIdx = hlsUrl.indexOf("/hls/");
        if (hlsIdx >= 0) {
            QString hlsRelative = hlsUrl.mid(hlsIdx + 1); // "hls/md5/output.m3u8"
            QString hlsLocalDir = QCoreApplication::applicationDirPath()
                                  + "/" + hlsRelative.section('/', 0, 1); // "appdir/hls/md5"
            QDir hlsDir(hlsLocalDir);
            if (hlsDir.exists()) {
                hlsDir.removeRecursively();
                qDebug() << "已删除 HLS 目录：" << hlsLocalDir;
            }
        }
    }

    qDebug() << "已删除视频 ID:" << videoId << "文件:" << videoPath;
    return true;
}

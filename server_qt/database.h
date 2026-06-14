#ifndef DATABASE_H
#define DATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QString>
#include <QDebug>
#include <QVariant>
#include <QMutex>
#include <QList>

// ============================================================
// 数据库操作类
// 负责用户数据的存储和查询
// ============================================================
class Database : public QObject
{
    Q_OBJECT
    
public:
    explicit Database(QObject *parent = nullptr);
    ~Database();
    
    // 初始化数据库（连接 MySQL）
    // host: MySQL 主机地址（默认 localhost）
    // user: 用户名（默认 root）
    // password: 密码（默认 123456）
    // dbName: 数据库名（默认 videorecorder）
    bool init(const QString &host = "localhost",
              const QString &user = "root",
              const QString &password = "123456",
              const QString &dbName = "videorecorder");
    
    // 用户注册：插入新用户（password 为客户端 SHA-256 哈希，salt 为每用户随机盐）
    bool registerUser(const QString &username, const QString &salt,
                      const QString &passwordHash);

    // 用户登录：验证用户名与密码哈希
    bool loginUser(const QString &username, const QString &passwordHash);

    // 获取用户密码盐（用户不存在时返回空且 found=false）
    QString getPasswordSalt(const QString &username, bool *found = nullptr);

    // 检查用户是否存在
    bool userExists(const QString &username);

    // 根据用户名获取用户ID
    int getUserId(const QString &username);

    // ==================== videos 表操作 ====================

    /**
     * @brief 插入视频记录
     * @param userId 用户ID
     * @param title 标题
     * @param coverUrl 封面URL
     * @param videoUrl 视频URL（本地路径）
     * @param duration 时长（秒）
     * @param hlsUrl HLS 播放地址（可选，默认为空）
     * @return int 新插入的视频ID（成功）或 -1（失败）
     */
    int insertVideo(int userId, const QString &title,
                   const QString &coverUrl, const QString &videoUrl,
                   int duration, const QString &hlsUrl = "");

    // 获取视频列表（分页：offset, limit）
    QVariantList getVideoList(int offset, int limit);

    // 按关键词搜索视频（标题 / 作者 / 描述）
    QVariantList searchVideos(const QString &keyword, int offset, int limit);

    // 热度排序视频列表（分页：offset, limit）
    QVariantList getHotVideoList(int offset, int limit,
                                 double alpha = 3.0, double beta = 2.0,
                                 double T = 2.0, double gamma = 1.5);

    // 根据ID获取视频信息
    QVariantMap getVideoById(int videoId);

    // 获取某用户的视频列表
    QVariantList getUserVideos(int userId, int offset, int limit);

    // 增加播放量
    bool incrementPlayCount(int videoId);

    // 批量增加播放量（Redis 计数落库）
    bool addPlayCountDelta(int videoId, int delta);

    // 切换点赞（已赞则取消）；返回 success/liked/likesCount/message
    QVariantMap toggleVideoLike(int videoId, int userId);

    // 批量查询用户已点赞的视频 ID
    QList<int> getLikedVideoIds(int userId, const QList<int> &videoIds);

    /**
     * @brief 更新视频的 HLS 地址
     * @param videoId 视频ID
     * @param hlsUrl HLS 播放地址
     * @return bool 成功返回 true
     */
    bool updateVideoHlsUrl(int videoId, const QString &hlsUrl);

    /**
     * @brief 更新视频封面URL
     * @param videoId 视频ID
     * @param coverUrl 封面URL
     * @return bool 成功返回 true
     */
    bool updateVideoCover(int videoId, const QString &coverUrl);

    /**
     * @brief 删除视频（级联删除文件 + DB 记录）
     * @param videoId 视频ID
     * @param userId 用户ID（仅允许删除自己的视频）
     * @return bool 成功返回 true
     */
    bool deleteVideo(int videoId, int userId);

private:
    QSqlDatabase m_db;
    QMutex m_mutex;  // 【Phase1】线程池工作线程通过 mutex 保护数据库访问
};

#endif // DATABASE_H

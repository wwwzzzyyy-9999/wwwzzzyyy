-- 方案 A 热度排序演示数据（在 videorecorder 库执行）
-- 用法: mysql -u root -p videorecorder < seed_hot_ranking_test.sql

USE videorecorder;

-- 确保有测试用户（已存在则跳过）
INSERT IGNORE INTO users (id, username, password) VALUES
(9001, 'hot_demo_alice', 'dummy'),
(9002, 'hot_demo_bob',   'dummy');

-- 清理旧演示视频（按标题前缀）
DELETE FROM videos WHERE title LIKE '[HOT-DEMO]%';

-- 高播放老视频（应低于较新的中等互动视频）
INSERT INTO videos (user_id, title, cover_url, video_url, hls_url, duration,
                    play_count, likes_count, comments_count, created_at)
VALUES
(9001, '[HOT-DEMO] 老热门 A', '', '/tmp/a.mp4', '', 60,
 1000, 50, 10, DATE_SUB(NOW(), INTERVAL 1 DAY));

-- 较新、互动好（应排前）
INSERT INTO videos (user_id, title, cover_url, video_url, hls_url, duration,
                    play_count, likes_count, comments_count, created_at)
VALUES
(9002, '[HOT-DEMO] 新热门 B', '', '/tmp/b.mp4', '', 90,
 200, 80, 30, DATE_SUB(NOW(), INTERVAL 6 HOUR));

-- 刚上传零互动（靠 fresh 混排曝光）
INSERT INTO videos (user_id, title, cover_url, video_url, hls_url, duration,
                    play_count, likes_count, comments_count, created_at)
VALUES
(9001, '[HOT-DEMO] 全新 C', '', '/tmp/c.mp4', '', 45,
 0, 0, 0, DATE_SUB(NOW(), INTERVAL 1 HOUR));

-- 较新少量互动
INSERT INTO videos (user_id, title, cover_url, video_url, hls_url, duration,
                    play_count, likes_count, comments_count, created_at)
VALUES
(9002, '[HOT-DEMO] 新视频 D', '', '/tmp/d.mp4', '', 120,
 50, 5, 2, DATE_SUB(NOW(), INTERVAL 2 HOUR));

-- 验证热度 SQL（与 RecommendEngine 一致）
SELECT id, title, play_count, likes_count, comments_count, created_at,
       (play_count + 3*likes_count + 2*comments_count)
         / POW(GREATEST(TIMESTAMPDIFF(HOUR, created_at, NOW()), 0) + 2, 1.5)
         AS hot_score
FROM videos
WHERE title LIKE '[HOT-DEMO]%'
ORDER BY hot_score DESC, id DESC;

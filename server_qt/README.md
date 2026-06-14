# VideoRecorderServer（Linux / Qt 5.9.9）

## 依赖

```bash
sudo apt-get install -y \
  qt5-qmake qtbase5-dev \
  libqt5sql5-mysql libmysqlclient-dev \
  ffmpeg
```

## 编译（原 Makefile 流程）

在 `server_qt` 目录下：

```bash
qmake server.pro
make -j$(nproc)
```

> **注意**：服务端使用 `server_qt/protocol.h`（自包含，不依赖上级目录）。
> 若只拷贝了 `server_qt/` 到 Linux，请确保 `protocol.h`、`recommendengine.*` 等文件一并同步。
> 客户端仍使用项目根目录的 `protocol.h`，两处枚举值需保持一致。

生成当前目录下的 `VideoRecorderServer` 及 `Makefile`（与原先一致，勿使用 `build-linux.sh`）。

## Nginx（HLS 静态服务）

HLS 分片与封面由 Nginx 提供，不再内嵌 HTTP 服务。

```bash
sudo apt-get install -y nginx
# 编辑 nginx-hls.conf：listen 与 alias 分别对齐 config.ini 的 hls_port、hls_output_dir
sudo cp nginx-hls.conf /etc/nginx/conf.d/videorecorder-hls.conf
sudo nginx -t && sudo systemctl enable --now nginx
```

确保 Nginx 运行用户对 `hls_output_dir` 有读权限。

## 运行

```bash
cp config.ini.example config.ini   # 首次
# 建议 hls_port=8080（80 需 root）；hls_ip 填客户端能访问的本机 IP
./VideoRecorderServer
```

（TCP 默认 8888，来自 `config.ini` 的 `tcp_port`；不要写成 `./VideoRecorderServer 8080`，否则会与 `hls_port` 冲突。）

启动日志应出现 **「HLS 静态服务（Nginx）」**。客户端播放地址形如：

`http://<hls_ip>:<hls_port>/hls/<md5>/output.m3u8`

`uploads/`、`videos/`、`hls/` 在 `hls_output_dir` 同级或配置路径下创建。

## HLS

`remuxToHls()` 已适配 FFmpeg 3.0.x（无 `-hls_segment_type`）。

## 方案 A：热度推荐 Feed

发现页默认调用 `MSG_GET_RECOMMEND_FEED`（协议号 23），服务端按热度 + 时间衰减排序，并混入约 15% 最新视频。

`config.ini` 需包含（见 `config.ini.example`）：

```ini
[recommend]
alpha=3.0
beta=2.0
T=2.0
gamma=1.5
fresh_ratio=0.15
safety_margin=20
```

客户端发现页支持 **热门 / 最新** 切换；「最新」仍走 `MSG_GET_VIDEO_LIST`。

### 演示数据

```bash
mysql -u root -p videorecorder < scripts/seed_hot_ranking_test.sql
```

插入后请求推荐列表，预期顺序约为：新热门 B > 新视频 D ≈ 老热门 A，全新 C 可能经 fresh 槽位出现在首页。

### 回退

客户端点击「最新」，或改回调 `sendGetVideoList()`，无需改数据库。

## 搜索

- 协议：`MSG_SEARCH_VIDEOS = 26`
- 请求：`{"keyword": "关键词", "offset": 0, "limit": 20}`
- 匹配字段：视频标题、作者用户名、描述（模糊匹配）
- 响应 action：`search_videos`，字段与 `video_list` 一致

## Redis（P0/P1，可选）

服务端内置 RESP 客户端（无 hiredis），`config.ini` 中 `[Redis] Enabled=false` 时自动降级，不影响原有 TCP/MySQL 流程。

| 能力 | Key 前缀 | 说明 |
|------|----------|------|
| 登录会话 | `session:token:{token}` | 登录返回 token，后续请求 JSON 带 `token` |
| 上传状态 | `upload:meta:` / `upload:chunks:` | 与本地 `uploads/`、`.part` 双写 |
| 接口缓存 | `cache:video:` / `cache:feed:` 等 | 列表、推荐、详情、搜索 |
| 播放量 | `counter:play:` | Redis INCR，定时刷 MySQL |
| 密码盐 | `auth:salt:{username}` | 登录前取盐缓存 |
| 限流 | `ratelimit:login:` 等 | 登录、上传、播放 |

```bash
sudo apt-get install -y redis-server
sudo systemctl start redis-server
cp config.ini.example config.ini   # 含 [Redis] / [RedisCache] / [RedisRateLimit]
```

启动日志出现 **「Redis 已连接」** 表示启用；**「Redis 不可用，将降级」** 时仍可正常运行。

## 点赞

- 协议：`MSG_GET_LIKE_STATUS = 24`、`MSG_TOGGLE_LIKE = 25`
- 数据表：`video_likes(user_id, video_id)`，每人每视频只能赞一次（可取消）
- `videos.likes_count` 与推荐热度公式联动（α=3.0）
- 客户端：发现页视频卡片右侧 ♡/♥ 按钮，需登录

# VideoRecorder 项目文档（小白版）📚

> 📌 本系列文档专为编程新手编写，用最通俗易懂的语言解释项目的每一个模块。
> 按照数据流向和代码逻辑组织，每个文档都可以独立阅读。

---

## 文档目录

| 序号 | 文档 | 内容 | 核心源文件 |
|------|------|------|-----------|
| 01 | [启动与登录模块](01-启动与登录模块.md) | 程序启动、登录/注册、密码加密、NetworkManager 初始化 | main.cpp, logindialog.cpp, registerdialog.cpp, networkmanager.cpp, auth_util.h |
| 02 | [通信协议模块](02-通信协议模块.md) | 自定义二进制帧协议、编解码、粘包处理、响应路由 | protocol.h, networkmanager.cpp |
| 03 | [视频浏览与主界面](03-视频浏览与主界面.md) | 视频卡片网格、导航栏、搜索、点赞 | videomainwindow.cpp, videocard.cpp |
| 04 | [视频播放引擎](04-视频播放引擎.md) | 三线程架构、音视频同步、PacketQueue、HLS seek | videoplayer.cpp, PacketQueue.cpp, videoplayer.h |
| 05 | [上传与下载模块](05-上传与下载模块.md) | 分块上传滑动窗口、断点续传、Base64编码、SHA-256校验 | networkmanager.cpp, server.cpp (handleUpload*) |
| 06 | [服务端核心模块](06-服务端核心模块.md) | epoll+线程池架构、消息路由、sendResponse、线程安全 | server.cpp, server.h, epollserver.cpp, threadpool.cpp |
| 07 | [数据库与安全模块](07-数据库与安全模块.md) | MySQL操作、密码安全、SQL注入防护、恒定时间比较 | database.cpp, auth_util.h |

---

## 推荐阅读顺序

### 新手入门（从启动到使用）
1. **01 - 启动与登录** → 理解程序怎么跑起来的
2. **03 - 视频浏览与主界面** → 理解用户能做什么
3. **02 - 通信协议** → 理解客户端和服务器怎么对话

### 进阶开发（理解核心机制）
4. **04 - 视频播放引擎** → 最复杂最核心的模块
5. **05 - 上传与下载** → 理解大文件怎么传输
6. **06 - 服务端核心** → 理解服务器怎么处理请求

### 安全专题
7. **07 - 数据库与安全** → 理解密码保护和防注入

---

## 技术栈速查

| 技术 | 用途 | 文档 |
|------|------|------|
| Qt 5.12 | 客户端 GUI 框架 | 01, 03 |
| Qt 5.9 | 服务端框架（无GUI） | 06 |
| FFmpeg 4.2.2 | 音视频解码/封装 | 04, 05 |
| SDL 2.0.10 | 音频输出 | 04 |
| MySQL 5.7 | 数据存储 | 07 |
| C++17 | atomic 变量 | 04 |
| TCP Socket | 网络通信 | 02 |
| epoll | 服务端IO多路复用 | 06 |

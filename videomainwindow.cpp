#include "videomainwindow.h"
#include "playerdialog.h"
#include "userprofiledialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStyle>
#include <QButtonGroup>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QSet>
#include <QSettings>

namespace {

bool isTcpDownloadTempFile(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    const QString name = info.fileName();
    if (!name.startsWith(QStringLiteral("vr_play_")) || !name.endsWith(QStringLiteral(".mp4")))
        return false;
    return info.absolutePath() == QDir(QDir::tempPath()).absolutePath();
}

void removeTcpDownloadTempFile(const QString &path)
{
    if (!isTcpDownloadTempFile(path))
        return;
    if (QFile::exists(path) && !QFile::remove(path))
        qWarning() << "无法删除下载临时文件：" << path;
    else if (!path.isEmpty())
        qDebug() << "已回收下载临时文件：" << path;
}

} // namespace

// ============================================================
// 构造函数
// ============================================================
VideoMainWindow::VideoMainWindow(NetworkManager *networkManager,
                               QWidget *parent)
    : QMainWindow(parent)
    , m_networkManager(networkManager)
    , m_gridLayout(nullptr)
    , m_scrollArea(nullptr)
    , m_emptyLabel(nullptr)
    , m_gridColumnCount(4)
    , m_searchEdit(nullptr)
    , m_userAvatarBtn(nullptr)
    , m_currentPage(0)
    , m_currentPlayingVideoId(-1)
    , m_navButtonGroup(nullptr)
    , m_btnDiscover(nullptr)
    , m_btnMyVideos(nullptr)
    , m_feedModeGroup(nullptr)
    , m_btnFeedHot(nullptr)
    , m_btnFeedLatest(nullptr)
    , m_useHotFeed(true)
    , m_feedBar(nullptr)
    , m_latestListRequestId(0)
    , m_uploadId("")
    , m_uploadFilePath()
    , m_uploadFile(nullptr)
    , m_uploadTotalChunks(0)
    , m_uploadNextToSend(0)
    , m_uploadInFlight(0)
    , m_downloadPlayVideoId(-1)
    , m_downloadPlayTotalChunks(0)
    , m_downloadPlayNextChunk(0)
    , m_downloadPlayTempPath()
    , m_downloadPlayFile(nullptr)
{
    setWindowTitle(QStringLiteral("短视频平台 - 发现 · 热门推荐"));
    resize(1200, 800);

    initUI();
    connectNetworkSignals();
    loadDiscoverFeed();
}

void VideoMainWindow::connectNetworkSignals()
{
    if (!m_networkManager)
        return;

    connect(m_networkManager, &NetworkManager::SIG_videoListResult,
            this, &VideoMainWindow::onVideoListResult);
    connect(m_networkManager, &NetworkManager::SIG_recommendFeedResult,
            this, &VideoMainWindow::onRecommendFeedResult);
    connect(m_networkManager, &NetworkManager::SIG_searchVideosResult,
            this, &VideoMainWindow::onSearchVideosResult);
    connect(m_networkManager, &NetworkManager::SIG_getLikeStatusResult,
            this, &VideoMainWindow::onGetLikeStatusResult);
    connect(m_networkManager, &NetworkManager::SIG_toggleLikeResult,
            this, &VideoMainWindow::onToggleLikeResult);
    connect(m_networkManager, &NetworkManager::SIG_videoInfoResult,
            this, &VideoMainWindow::onVideoInfoResult);
    connect(m_networkManager, &NetworkManager::SIG_myVideosResult,
            this, &VideoMainWindow::onMyVideosResult);
    connect(m_networkManager, &NetworkManager::SIG_deleteVideoResult,
            this, &VideoMainWindow::onDeleteVideoResult);
    connect(m_networkManager, &NetworkManager::SIG_uploadStartResult,
            this, &VideoMainWindow::onUploadStartResult);
    connect(m_networkManager, &NetworkManager::SIG_uploadChunkResult,
            this, &VideoMainWindow::onUploadChunkResult);
    connect(m_networkManager, &NetworkManager::SIG_uploadEndResult,
            this, &VideoMainWindow::onUploadEndResult);
    connect(m_networkManager, &NetworkManager::SIG_uploadProgress,
            this, &VideoMainWindow::onUploadProgress);
    connect(m_networkManager, &NetworkManager::SIG_uploadResumeResult,
            this, &VideoMainWindow::onUploadResumeResult);
    connect(m_networkManager, &NetworkManager::SIG_downloadStartResult,
            this, &VideoMainWindow::onDownloadStartResult);
    connect(m_networkManager, &NetworkManager::SIG_downloadChunkResult,
            this, &VideoMainWindow::onDownloadChunkResult);
}

// ============================================================
// 析构函数
// ============================================================
VideoMainWindow::~VideoMainWindow()
{
    if (m_downloadPlayFile) {
        m_downloadPlayFile->close();
        delete m_downloadPlayFile;
        m_downloadPlayFile = nullptr;
    }
    removeTcpDownloadTempFile(m_downloadPlayTempPath);
}

// ============================================================
// initUI：初始化整个界面
// ============================================================
void VideoMainWindow::initUI()
{
    QWidget *central = new QWidget(this);
    this->setCentralWidget(central);

    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 顶部栏
    QWidget *topBar = createTopBar();
    mainLayout->addWidget(topBar);

    // 下方内容区：左侧导航 + 右侧视频网格
    QHBoxLayout *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // 左侧导航栏
    QWidget *sideBar = createSideBar();
    sideBar->setFixedWidth(200);
    contentLayout->addWidget(sideBar);

    // 右侧视频网格区域
    QWidget *gridWidget = createVideoGrid();
    contentLayout->addWidget(gridWidget);

    mainLayout->addLayout(contentLayout);
}

// ============================================================
// createTopBar：顶部栏
// ============================================================
QWidget* VideoMainWindow::createTopBar()
{
    QWidget *topBar = new QWidget(this);
    topBar->setFixedHeight(56);
    topBar->setStyleSheet("QWidget { background-color: white; border-bottom: 1px solid #E0E0E0; }");

    QHBoxLayout *layout = new QHBoxLayout(topBar);
    layout->setContentsMargins(16, 8, 16, 8);

    QLabel *logoLabel = new QLabel("短视频平台", topBar);
    logoLabel->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #FF4444; }");
    layout->addWidget(logoLabel);

    layout->addSpacing(40);

    m_searchEdit = new QLineEdit(topBar);
    m_searchEdit->setPlaceholderText("搜索视频...");
    m_searchEdit->setFixedWidth(360);
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->setStyleSheet(
        "QLineEdit { border: 1px solid #E0E0E0; border-radius: 18px; padding-left: 16px; font-size: 14px; }"
        "QLineEdit:focus { border: 1px solid #409EFF; }"
    );
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &VideoMainWindow::onSearchClicked);
    layout->addWidget(m_searchEdit);

    layout->addStretch();

    QPushButton *uploadBtn = new QPushButton("上传视频", topBar);
    uploadBtn->setFixedSize(100, 36);
    uploadBtn->setStyleSheet(
        "QPushButton { background-color: #409EFF; color: white; border-radius: 18px; font-size: 14px; }"
        "QPushButton:hover { background-color: #66B1FF; }"
    );
    connect(uploadBtn, &QPushButton::clicked, this, &VideoMainWindow::onUploadClicked);
    layout->addWidget(uploadBtn);

    layout->addSpacing(16);

    m_userAvatarBtn = new QPushButton("我", topBar);
    m_userAvatarBtn->setFixedSize(36, 36);
    m_userAvatarBtn->setStyleSheet(
        "QPushButton { background-color: #409EFF; color: white; border-radius: 18px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #66B1FF; }"
    );
    connect(m_userAvatarBtn, &QPushButton::clicked, this, &VideoMainWindow::onUserAvatarClicked);
    layout->addWidget(m_userAvatarBtn);

    return topBar;
}

// ============================================================
// createSideBar：左侧导航栏（使用 QButtonGroup 互斥高亮）
// ============================================================
QWidget* VideoMainWindow::createSideBar()
{
    QWidget *sideBar = new QWidget(this);
    sideBar->setStyleSheet("QWidget { background-color: #F5F5F5; border-right: 1px solid #E0E0E0; }");

    QVBoxLayout *layout = new QVBoxLayout(sideBar);
    layout->setContentsMargins(0, 16, 0, 0);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignTop);

    QString navBtnStyle =
        "QPushButton { text-align: left; padding-left: 24px; height: 48px; "
        "border: none; background-color: transparent; font-size: 15px; }"
        "QPushButton:hover { background-color: #E0E0E0; }"
        "QPushButton:checked { background-color: #E6F7FF; color: #409EFF; "
        "font-weight: bold; border-right: 3px solid #409EFF; }";

    // 创建按钮组（互斥）
    m_navButtonGroup = new QButtonGroup(this);
    m_navButtonGroup->setExclusive(true);

    m_btnDiscover = new QPushButton(" 🏠  发现", sideBar);
    m_btnDiscover->setCheckable(true);
    m_btnDiscover->setChecked(true);
    m_btnDiscover->setStyleSheet(navBtnStyle);
    connect(m_btnDiscover, &QPushButton::clicked, this, &VideoMainWindow::onNavDiscoverClicked);
    layout->addWidget(m_btnDiscover);
    m_navButtonGroup->addButton(m_btnDiscover);

    m_btnMyVideos = new QPushButton(" 📁  本地视频", sideBar);
    m_btnMyVideos->setCheckable(true);
    m_btnMyVideos->setStyleSheet(navBtnStyle);
    connect(m_btnMyVideos, &QPushButton::clicked, this, &VideoMainWindow::onNavMyVideosClicked);
    layout->addWidget(m_btnMyVideos);
    m_navButtonGroup->addButton(m_btnMyVideos);

    layout->addStretch();
    return sideBar;
}

// ============================================================
// createVideoGrid：创建视频网格区域
// ============================================================
QWidget* VideoMainWindow::createVideoGrid()
{
    QWidget *container = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    m_feedBar = new QWidget(container);
    QHBoxLayout *feedLayout = new QHBoxLayout(m_feedBar);
    feedLayout->setContentsMargins(0, 0, 0, 0);
    feedLayout->setSpacing(8);

    const QString feedBtnStyle =
        "QPushButton { min-width: 72px; height: 32px; border: 1px solid #E0E0E0; "
        "border-radius: 16px; background: white; color: #666; font-size: 13px; }"
        "QPushButton:checked { background: #409EFF; color: white; border-color: #409EFF; }"
        "QPushButton:hover { border-color: #409EFF; }";

    m_feedModeGroup = new QButtonGroup(this);
    m_feedModeGroup->setExclusive(true);

    m_btnFeedHot = new QPushButton("热门", m_feedBar);
    m_btnFeedHot->setCheckable(true);
    m_btnFeedHot->setChecked(true);
    m_btnFeedHot->setStyleSheet(feedBtnStyle);
    connect(m_btnFeedHot, &QPushButton::clicked, this, &VideoMainWindow::onFeedHotClicked);
    feedLayout->addWidget(m_btnFeedHot);
    m_feedModeGroup->addButton(m_btnFeedHot);

    m_btnFeedLatest = new QPushButton("最新", m_feedBar);
    m_btnFeedLatest->setCheckable(true);
    m_btnFeedLatest->setStyleSheet(feedBtnStyle);
    connect(m_btnFeedLatest, &QPushButton::clicked, this, &VideoMainWindow::onFeedLatestClicked);
    feedLayout->addWidget(m_btnFeedLatest);
    m_feedModeGroup->addButton(m_btnFeedLatest);

    feedLayout->addStretch();
    layout->addWidget(m_feedBar);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background-color: #FAFAFA; }");

    QWidget *gridContainer = new QWidget();
    m_gridLayout = new QGridLayout(gridContainer);
    m_gridLayout->setContentsMargins(0, 0, 0, 0);
    m_gridLayout->setHorizontalSpacing(16);
    m_gridLayout->setVerticalSpacing(16);
    m_gridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    gridContainer->setLayout(m_gridLayout);
    m_scrollArea->setWidget(gridContainer);
    layout->addWidget(m_scrollArea);

    return container;
}

// ============================================================
// 加载发现页 Feed
// ============================================================
void VideoMainWindow::exitSearchMode()
{
    m_searchKeyword.clear();
    if (m_feedBar)
        m_feedBar->setVisible(true);
}

void VideoMainWindow::loadDiscoverFeed()
{
    if (!m_networkManager) {
        QMessageBox::warning(this, "提示", "网络管理器未初始化");
        return;
    }
    exitSearchMode();
    qDebug() << "加载发现页，模式：" << (m_useHotFeed ? "热门" : "最新")
             << "，页码：" << m_currentPage;
    const int offset = m_currentPage * 20;
    if (m_useHotFeed)
        m_latestListRequestId = m_networkManager->sendGetRecommendFeed(offset, 20);
    else
        m_latestListRequestId = m_networkManager->sendGetVideoList(offset, 20);
}

void VideoMainWindow::loadSearchResults()
{
    if (!m_networkManager) {
        QMessageBox::warning(this, "提示", "网络管理器未初始化");
        return;
    }
    if (m_searchKeyword.isEmpty())
        return;

    if (m_feedBar)
        m_feedBar->setVisible(false);

    qDebug() << "搜索视频，关键词：" << m_searchKeyword;
    m_latestListRequestId = m_networkManager->sendSearchVideos(
        m_searchKeyword, m_currentPage * 20, 20);
}

void VideoMainWindow::loadVideoList()
{
    loadDiscoverFeed();
}

// ============================================================
// 清空视频网格
// ============================================================
void VideoMainWindow::clearVideoGrid()
{
    if (!m_gridLayout) return;
    m_videoCards.clear();
    m_hlsUrlByVideoId.clear();
    m_emptyLabel = nullptr;
    QLayoutItem *item = nullptr;
    while ((item = m_gridLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

int VideoMainWindow::computeGridColumnCount() const
{
    if (!m_scrollArea || !m_gridLayout)
        return 4;

    const int viewportWidth = m_scrollArea->viewport()->width();
    if (viewportWidth <= 0)
        return 4;

    const int minCardWidth = 220;
    const int spacing = m_gridLayout->horizontalSpacing();
    return qMax(1, (viewportWidth + spacing) / (minCardWidth + spacing));
}

void VideoMainWindow::relayoutVideoGrid()
{
    if (!m_gridLayout)
        return;

    QLayoutItem *item = nullptr;
    while ((item = m_gridLayout->takeAt(0)) != nullptr)
        delete item;

    const int cols = computeGridColumnCount();
    m_gridColumnCount = cols;

    for (int c = 0; c < cols; ++c)
        m_gridLayout->setColumnStretch(c, 1);

    if (m_emptyLabel) {
        m_gridLayout->addWidget(m_emptyLabel, 0, 0, 1, cols);
        return;
    }

    for (int i = 0; i < m_videoCards.size(); ++i) {
        const int row = i / cols;
        const int col = i % cols;
        m_gridLayout->addWidget(m_videoCards[i], row, col);
    }
}

void VideoMainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    const int cols = computeGridColumnCount();
    if (cols != m_gridColumnCount || !m_videoCards.isEmpty() || m_emptyLabel)
        relayoutVideoGrid();
}

// ============================================================
// 添加一个视频卡片
// ============================================================
void VideoMainWindow::addVideoCard(const QJsonObject &videoObj)
{
    if (!m_gridLayout) return;

    int videoId   = videoObj["id"].toInt();
    QString title  = videoObj["title"].toString("未命名视频");
    QString author = videoObj["author"].toString("匿名用户");
    int playCount  = videoObj["playCount"].toInt();
    int likesCount = videoObj["likesCount"].toInt();
    QString coverUrl = videoObj["coverUrl"].toString();
    const QString hlsUrl = videoObj["hlsUrl"].toString();
    if (!hlsUrl.isEmpty())
        m_hlsUrlByVideoId.insert(videoId, hlsUrl);

    VideoCard *card = new VideoCard(videoId, coverUrl, title, author, playCount, likesCount,
                                    m_scrollArea->widget());
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(card, &VideoCard::SIG_cardClicked, this, &VideoMainWindow::onVideoCardClicked);
    connect(card, &VideoCard::SIG_likeClicked, this, &VideoMainWindow::onVideoLikeClicked);

    m_videoCards.append(card);
    relayoutVideoGrid();
}

// ============================================================
// 搜索
// ============================================================
void VideoMainWindow::onSearchClicked()
{
    const QString keyword = m_searchEdit->text().trimmed();
    qDebug() << "搜索关键词：" << keyword;
    m_currentPage = 0;
    clearVideoGrid();

    if (keyword.isEmpty()) {
        loadDiscoverFeed();
        setWindowTitle(m_useHotFeed
            ? QStringLiteral("短视频平台 - 发现 · 热门推荐")
            : QStringLiteral("短视频平台 - 发现 · 最新"));
        return;
    }

    m_searchKeyword = keyword;
    setWindowTitle(QStringLiteral("短视频平台 - 搜索 · %1").arg(keyword));
    loadSearchResults();
}

// ============================================================
// 上传按钮：弹出文件选择框，选择视频文件后上传
// ============================================================
void VideoMainWindow::onUploadClicked()
{
    if (!m_networkManager) {
        QMessageBox::warning(this, "提示", "网络管理器未初始化");
        return;
    }
    if (!m_networkManager->isLoggedIn()) {
        QMessageBox::warning(this, "提示", "请先登录");
        return;
    }

    if (m_uploadFile && m_uploadFile->isOpen()) {
        QMessageBox::warning(this, "提示", "当前有上传任务进行中，请等待完成或失败后续传。");
        return;
    }

    const PendingUploadInfo pending = loadPendingUpload();
    if (pending.isValid() && QFileInfo::exists(pending.filePath)) {
        const QString fileName = QFileInfo(pending.filePath).fileName();
        QMessageBox resumeBox(this);
        resumeBox.setWindowTitle(QStringLiteral("断点续传"));
        resumeBox.setText(QStringLiteral("检测到未完成的上传：%1").arg(fileName));
        resumeBox.setInformativeText(QStringLiteral("是否从上次进度继续上传？"));
        QPushButton *resumeBtn = resumeBox.addButton(QStringLiteral("继续上传"),
                                                     QMessageBox::AcceptRole);
        QPushButton *newFileBtn = resumeBox.addButton(QStringLiteral("选择新文件"),
                                                      QMessageBox::DestructiveRole);
        QPushButton *cancelBtn = resumeBox.addButton(QMessageBox::Cancel);
        resumeBox.exec();

        if (resumeBox.clickedButton() == resumeBtn) {
            m_uploadId = pending.uploadId;
            m_uploadFilePath = pending.filePath;
            m_uploadFileHash = pending.fileHash;
            m_uploadTotalChunks = pending.totalChunks;
            resumeUpload();
            return;
        }
        if (resumeBox.clickedButton() == cancelBtn || resumeBox.clickedButton() == nullptr)
            return;
        if (resumeBox.clickedButton() != newFileBtn)
            return;
        clearPendingUpload();
    }

    // 弹出文件选择对话框
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "选择视频文件",
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        "视频文件 (*.mp4 *.avi *.mov *.mkv *.flv);;所有文件 (*.*)"
    );

    if (filePath.isEmpty()) {
        return;  // 用户取消了选择
    }

    qDebug() << "选择视频文件：" << filePath;
    startUpload(filePath);
}

// ============================================================
// 用户头像菜单
// ============================================================
void VideoMainWindow::onUserAvatarClicked()
{
    QMenu menu(this);
    QAction *profileAction  = menu.addAction("个人中心");
    menu.addSeparator();
    QAction *logoutAction   = menu.addAction("退出登录");

    QAction *selected = menu.exec(
        m_userAvatarBtn->mapToGlobal(QPoint(0, m_userAvatarBtn->height()))
    );
    if (selected == logoutAction) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "确认", "确定要退出登录吗？",
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            emit SIG_logout();  // 通知 main() 返回登录界面
        }
    } else if (selected == profileAction) {
        onOpenProfile();
    }
}

// ============================================================
// 导航
// ============================================================
void VideoMainWindow::onNavDiscoverClicked()
{
    if (m_searchEdit)
        m_searchEdit->clear();
    m_currentPage = 0;
    clearVideoGrid();
    loadDiscoverFeed();
    setWindowTitle(m_useHotFeed
        ? QStringLiteral("短视频平台 - 发现 · 热门推荐")
        : QStringLiteral("短视频平台 - 发现 · 最新"));
}

void VideoMainWindow::onFeedHotClicked()
{
    if (m_searchEdit)
        m_searchEdit->clear();
    const bool wasSearching = !m_searchKeyword.isEmpty();
    exitSearchMode();
    if (m_useHotFeed && !wasSearching)
        return;
    m_useHotFeed = true;
    m_btnFeedHot->setChecked(true);
    setWindowTitle(QStringLiteral("短视频平台 - 发现 · 热门推荐"));
    m_currentPage = 0;
    clearVideoGrid();
    loadDiscoverFeed();
}

void VideoMainWindow::onFeedLatestClicked()
{
    if (m_searchEdit)
        m_searchEdit->clear();
    const bool wasSearching = !m_searchKeyword.isEmpty();
    exitSearchMode();
    if (!m_useHotFeed && !wasSearching)
        return;
    m_useHotFeed = false;
    m_btnFeedLatest->setChecked(true);
    setWindowTitle(QStringLiteral("短视频平台 - 发现 · 最新"));
    m_currentPage = 0;
    clearVideoGrid();
    loadDiscoverFeed();
}

void VideoMainWindow::onNavMyVideosClicked()
{
    // 和"上传视频"一样：打开文件目录，选择后直接播放
    if (!m_networkManager) {
        QMessageBox::warning(this, "提示", "网络管理器未初始化");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(
        this,
        "选择本地视频文件",
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        "视频文件 (*.mp4 *.avi *.mov *.mkv *.flv);;所有文件 (*.*)"
    );

    if (filePath.isEmpty()) {
        return;  // 用户取消了选择
    }

    qDebug() << "我的视频 - 选择文件：" << filePath;

    // 直接播放，不弹窗（在主界面内播放）
    onLocalVideoClicked(filePath);
}

// ============================================================
// 视频卡片点击 → 获取视频详情 → 下载 → 播放
// ============================================================
void VideoMainWindow::onVideoCardClicked(int videoId)
{
    qDebug() << "视频卡片被点击，videoId =" << videoId;
    m_currentPlayingVideoId = videoId;

    const QString hlsUrl = m_hlsUrlByVideoId.value(videoId);
    if (!hlsUrl.isEmpty()) {
        playHLSVideo(hlsUrl, videoId);
        return;
    }

    if (m_networkManager)
        m_networkManager->sendGetVideoInfo(videoId);
}

void VideoMainWindow::showDiscoverVideos(bool success,
                                         const QString &message,
                                         const QJsonArray &videos)
{
    qDebug() << "收到发现页列表：success =" << success << ", 数量 =" << videos.size();
    if (!success) {
        QMessageBox::warning(this, "加载失败", message);
        return;
    }

    clearVideoGrid();
    if (videos.isEmpty()) {
        const QString emptyText = m_searchKeyword.isEmpty()
            ? QStringLiteral("暂无视频，快来上传第一个视频吧！")
            : QStringLiteral("未找到与「%1」相关的视频").arg(m_searchKeyword);
        QLabel *emptyLabel = new QLabel(emptyText, m_scrollArea->widget());
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet("font-size: 16px; color: #999999; padding: 40px;");
        m_emptyLabel = emptyLabel;
        relayoutVideoGrid();
        return;
    }
    for (const QJsonValue &v : videos) {
        addVideoCard(v.toObject());
    }
    syncLikeStatusForCards();
}

void VideoMainWindow::syncLikeStatusForCards()
{
    if (!m_networkManager || !m_networkManager->isLoggedIn() || m_videoCards.isEmpty())
        return;

    QList<int> videoIds;
    for (VideoCard *card : m_videoCards)
        videoIds.append(card->getVideoId());
    m_networkManager->sendGetLikeStatus(videoIds);
}

VideoCard *VideoMainWindow::findVideoCard(int videoId) const
{
    for (VideoCard *card : m_videoCards) {
        if (card->getVideoId() == videoId)
            return card;
    }
    return nullptr;
}

void VideoMainWindow::onVideoLikeClicked(int videoId)
{
    if (!m_networkManager) {
        QMessageBox::warning(this, "提示", "网络管理器未初始化");
        return;
    }
    if (!m_networkManager->isLoggedIn()) {
        QMessageBox::information(this, "提示", "请先登录后再点赞");
        return;
    }
    m_networkManager->sendToggleLike(videoId);
}

void VideoMainWindow::onGetLikeStatusResult(bool success,
                                            const QString &message,
                                            const QList<int> &likedIds)
{
    if (!success) {
        qDebug() << "获取点赞状态失败：" << message;
        return;
    }

    QSet<int> likedSet;
    for (int id : likedIds)
        likedSet.insert(id);

    for (VideoCard *card : m_videoCards) {
        card->setLikeState(likedSet.contains(card->getVideoId()), card->getLikesCount());
    }
}

void VideoMainWindow::onToggleLikeResult(bool success, const QString &message,
                                         int videoId, bool liked, int likesCount)
{
    if (!success) {
        QMessageBox::warning(this, "点赞失败", message);
        return;
    }

    if (VideoCard *card = findVideoCard(videoId))
        card->setLikeState(liked, likesCount);
}

void VideoMainWindow::onVideoListResult(bool success,
                                        const QString &message,
                                        const QJsonArray &videos,
                                        quint64 requestId)
{
    if (requestId != m_latestListRequestId)
        return;
    if (!m_searchKeyword.isEmpty() || m_useHotFeed)
        return;
    showDiscoverVideos(success, message, videos);
}

void VideoMainWindow::onRecommendFeedResult(bool success,
                                            const QString &message,
                                            const QJsonArray &videos,
                                            quint64 requestId)
{
    if (requestId != m_latestListRequestId)
        return;
    if (!m_searchKeyword.isEmpty() || !m_useHotFeed)
        return;
    showDiscoverVideos(success, message, videos);
}

void VideoMainWindow::onSearchVideosResult(bool success,
                                           const QString &message,
                                           const QJsonArray &videos,
                                           const QString &keyword,
                                           quint64 requestId)
{
    if (requestId != m_latestListRequestId)
        return;
    if (m_searchKeyword.isEmpty())
        return;
    if (keyword != m_searchKeyword)
        return;
    showDiscoverVideos(success, message, videos);
}

// ============================================================
// 收到视频详情 → 获取 HLS 地址并播放
// ============================================================
void VideoMainWindow::onVideoInfoResult(bool success,
                                       const QString &message,
                                       const QJsonObject &videoInfo)
{
    qDebug() << "收到视频详情：success =" << success;
    if (!success || videoInfo.isEmpty()) {
        QMessageBox::warning(this, "加载失败", message);
        return;
    }

    // 从 videoInfo 中获取 HLS 播放地址
    QString hlsUrl = videoInfo["hlsUrl"].toString();
    qDebug() << "HLS 播放地址：" << hlsUrl;

    if (hlsUrl.isEmpty()) {
        QMessageBox::warning(this, "播放失败",
            QStringLiteral("该视频没有 HLS 流（上传时转码可能失败）。\n"
                           "请重新上传，或检查服务端 ffmpeg 日志与 Nginx /hls/ 配置。"));
        return;
    }

    playHLSVideo(hlsUrl, m_currentPlayingVideoId);
}

void VideoMainWindow::showPlayerDialog(const QString &filePath, const QString &windowTitle,
                                       int fallbackVideoId)
{
    if (filePath.isEmpty())
        return;

    if (m_activePlayerDialog) {
        PlayerDialog *old = m_activePlayerDialog.data();
        m_activePlayerDialog.clear();
        if (old) {
            old->setAttribute(Qt::WA_DeleteOnClose, false);
            delete old;
        }
    }

    const bool quietOnError = (fallbackVideoId > 0);
    const bool removeOnClose = isTcpDownloadTempFile(filePath);
    PlayerDialog *dialog = new PlayerDialog(filePath, this, quietOnError, removeOnClose);
    m_activePlayerDialog = dialog;
    if (!windowTitle.isEmpty())
        dialog->setWindowTitle(windowTitle);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QObject::destroyed, this, [this]() {
        m_activePlayerDialog = nullptr;
    });
    if (fallbackVideoId > 0) {
        connect(dialog, &PlayerDialog::playbackFailed, this,
                [this, fallbackVideoId](const QString &reason) {
            qDebug() << "HLS 播放失败，改用 TCP 下载：" << reason;
            startDownloadAndPlay(fallbackVideoId);
        });
    }
    dialog->show();
}

void VideoMainWindow::playHLSVideo(const QString &hlsUrl, int fallbackVideoId)
{
    qDebug() << "播放 HLS 视频：" << hlsUrl;

    if (hlsUrl.isEmpty()) {
        if (fallbackVideoId > 0) {
            startDownloadAndPlay(fallbackVideoId);
            return;
        }
        QMessageBox::warning(this, "播放失败", "HLS 地址为空");
        return;
    }

    if (m_currentPlayingVideoId > 0 && m_networkManager) {
        m_networkManager->sendIncrementPlay(m_currentPlayingVideoId);
    }

    showPlayerDialog(hlsUrl, QStringLiteral("HLS 视频播放"), fallbackVideoId);
}

void VideoMainWindow::startDownloadAndPlay(int videoId)
{
    if (!m_networkManager || videoId <= 0)
        return;

    setWindowTitle(QStringLiteral("短视频平台 - 正在下载视频（HLS 不可用）…"));

    if (m_downloadPlayFile) {
        m_downloadPlayFile->close();
        delete m_downloadPlayFile;
        m_downloadPlayFile = nullptr;
    }

    m_downloadPlayVideoId = videoId;
    m_downloadPlayTotalChunks = 0;
    m_downloadPlayNextChunk = 0;
    m_downloadPlayTempPath.clear();

    qDebug() << "开始 TCP 下载播放，videoId =" << videoId;
    m_networkManager->sendDownloadStart(videoId);
}

void VideoMainWindow::onDownloadStartResult(bool success, const QString &message,
                                            int videoId, qint64 fileSize, int totalChunks)
{
    if (videoId != m_downloadPlayVideoId)
        return;

    if (!success || totalChunks <= 0) {
        m_downloadPlayVideoId = -1;
        QMessageBox::warning(this, QStringLiteral("播放失败"),
            QStringLiteral("无法通过 TCP 下载视频：%1").arg(message));
        return;
    }

    m_downloadPlayTotalChunks = totalChunks;
    m_downloadPlayNextChunk = 0;
    m_downloadPlayTempPath = QDir(QDir::tempPath()).filePath(
        QStringLiteral("vr_play_%1.mp4").arg(videoId));

    m_downloadPlayFile = new QFile(m_downloadPlayTempPath);
    if (!m_downloadPlayFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_downloadPlayVideoId = -1;
        delete m_downloadPlayFile;
        m_downloadPlayFile = nullptr;
        removeTcpDownloadTempFile(m_downloadPlayTempPath);
        m_downloadPlayTempPath.clear();
        QMessageBox::warning(this, QStringLiteral("播放失败"), QStringLiteral("无法创建临时文件"));
        return;
    }

    qDebug() << "下载播放：大小" << fileSize << "，块数" << totalChunks;
    m_networkManager->sendDownloadChunkAck(videoId, 0);
}

void VideoMainWindow::onDownloadChunkResult(bool success, const QString &message,
                                            int chunkIndex, const QByteArray &chunkData)
{
    if (m_downloadPlayVideoId < 0)
        return;
    const int videoId = m_downloadPlayVideoId;

    if (!success || !m_downloadPlayFile) {
        m_downloadPlayVideoId = -1;
        if (m_downloadPlayFile) {
            m_downloadPlayFile->close();
            delete m_downloadPlayFile;
            m_downloadPlayFile = nullptr;
        }
        removeTcpDownloadTempFile(m_downloadPlayTempPath);
        m_downloadPlayTempPath.clear();
        QMessageBox::warning(this, QStringLiteral("播放失败"),
            QStringLiteral("下载视频块失败：%1").arg(message));
        return;
    }

    if (chunkIndex != m_downloadPlayNextChunk) {
        qDebug() << "下载块乱序，期望" << m_downloadPlayNextChunk << "收到" << chunkIndex;
        return;
    }

    if (m_downloadPlayFile->write(chunkData) != chunkData.size()) {
        m_downloadPlayVideoId = -1;
        m_downloadPlayFile->close();
        delete m_downloadPlayFile;
        m_downloadPlayFile = nullptr;
        removeTcpDownloadTempFile(m_downloadPlayTempPath);
        m_downloadPlayTempPath.clear();
        QMessageBox::warning(this, QStringLiteral("播放失败"), QStringLiteral("写入临时文件失败"));
        return;
    }

    m_downloadPlayNextChunk = chunkIndex + 1;
    if (m_downloadPlayNextChunk < m_downloadPlayTotalChunks) {
        m_networkManager->sendDownloadChunkAck(videoId, m_downloadPlayNextChunk);
        return;
    }

    m_downloadPlayFile->close();
    delete m_downloadPlayFile;
    m_downloadPlayFile = nullptr;
    m_downloadPlayVideoId = -1;

    const QString tempPath = m_downloadPlayTempPath;
    qDebug() << "TCP 下载完成，本地播放：" << tempPath;
    setWindowTitle(m_useHotFeed
        ? QStringLiteral("短视频平台 - 发现 · 热门推荐")
        : QStringLiteral("短视频平台 - 发现 · 最新"));
    showPlayerDialog(tempPath, QStringLiteral("视频播放"));
}

void VideoMainWindow::onLocalVideoClicked(const QString &filePath)
{
    qDebug() << "播放本地视频：" << filePath;

    if (!QFile::exists(filePath)) {
        QMessageBox::warning(this, "播放失败", "视频文件不存在：" + filePath);
        return;
    }

    showPlayerDialog(filePath, QStringLiteral("本地视频播放"));
}

// ============================================================
// 上传功能（分块上传）
// ============================================================

VideoMainWindow::PendingUploadInfo VideoMainWindow::loadPendingUpload() const
{
    PendingUploadInfo info;
    if (!m_networkManager || !m_networkManager->isLoggedIn())
        return info;

    QSettings settings;
    settings.beginGroup(QStringLiteral("pending_upload"));
    info.uploadId = settings.value(QStringLiteral("uploadId")).toString();
    info.filePath = settings.value(QStringLiteral("filePath")).toString();
    info.fileHash = settings.value(QStringLiteral("fileHash")).toString();
    info.totalChunks = settings.value(QStringLiteral("totalChunks")).toInt();
    info.username = settings.value(QStringLiteral("username")).toString();
    settings.endGroup();

    if (!info.username.isEmpty()
        && info.username != m_networkManager->pendingUploadUsername()) {
        return PendingUploadInfo();
    }
    return info;
}

void VideoMainWindow::savePendingUpload()
{
    if (!m_networkManager || m_uploadId.isEmpty() || m_uploadFilePath.isEmpty())
        return;

    QSettings settings;
    settings.beginGroup(QStringLiteral("pending_upload"));
    settings.setValue(QStringLiteral("uploadId"), m_uploadId);
    settings.setValue(QStringLiteral("filePath"), m_uploadFilePath);
    settings.setValue(QStringLiteral("fileHash"), m_uploadFileHash);
    settings.setValue(QStringLiteral("totalChunks"), m_uploadTotalChunks);
    settings.setValue(QStringLiteral("username"), m_networkManager->pendingUploadUsername());
    settings.endGroup();
}

void VideoMainWindow::clearPendingUpload()
{
    QSettings settings;
    settings.remove(QStringLiteral("pending_upload"));
}

bool VideoMainWindow::openUploadFile()
{
    closeUploadFile();

    m_uploadFile = new QFile(m_uploadFilePath);
    if (!m_uploadFile->open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("上传失败"),
            QStringLiteral("无法打开文件：%1").arg(m_uploadFile->errorString()));
        delete m_uploadFile;
        m_uploadFile = nullptr;
        return false;
    }

    m_uploadTotalChunks = static_cast<int>(
        (m_uploadFile->size() + DEFAULT_CHUNK_SIZE - 1) / DEFAULT_CHUNK_SIZE);
    return true;
}

void VideoMainWindow::closeUploadFile()
{
    if (!m_uploadFile)
        return;
    m_uploadFile->close();
    delete m_uploadFile;
    m_uploadFile = nullptr;
}

void VideoMainWindow::resumeUpload()
{
    if (!m_networkManager) {
        QMessageBox::warning(this, QStringLiteral("续传失败"),
                             QStringLiteral("网络未初始化。"));
        return;
    }

    if (m_uploadId.isEmpty()) {
        const PendingUploadInfo pending = loadPendingUpload();
        if (!pending.isValid()) {
            QMessageBox::warning(this, QStringLiteral("续传失败"),
                                 QStringLiteral("没有可恢复的上传会话。"));
            return;
        }
        m_uploadId = pending.uploadId;
        m_uploadFilePath = pending.filePath;
        m_uploadFileHash = pending.fileHash;
        m_uploadTotalChunks = pending.totalChunks;
    }

    if (!openUploadFile())
        return;

    m_uploadInFlight = 0;
    qDebug() << "请求断点续传，uploadId =" << m_uploadId;
    m_networkManager->sendUploadResume(m_uploadId);
}

// 开始上传流程
void VideoMainWindow::startUpload(const QString &filePath)
{
    clearPendingUpload();
    m_uploadId.clear();
    m_uploadInFlight = 0;
    m_uploadNextToSend = 0;

    m_uploadFilePath = filePath;
    QFileInfo fileInfo(filePath);

    // 检查文件是否存在
    if (!fileInfo.exists()) {
        QMessageBox::warning(this, "上传失败", "文件不存在：" + filePath);
        return;
    }

    // 计算文件哈希（用于校验）【修复】分块读取，避免内存溢出
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "上传失败", "无法打开文件：" + file.errorString());
        return;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    const int hashChunkSize = 65536;  // 64KB 块计算哈希
    while (!file.atEnd()) {
        hash.addData(file.read(hashChunkSize));
    }
    file.close();

    QString fileHash = QString(hash.result().toHex());
    m_uploadFileHash = fileHash;

    // 计算分块数
    qint64 fileSize = fileInfo.size();
    int totalChunks = (fileSize + DEFAULT_CHUNK_SIZE - 1) / DEFAULT_CHUNK_SIZE;  // 向上取整

    qDebug() << "开始上传：" << filePath
             << "，大小：" << fileSize
             << "，分块数：" << totalChunks;

    // 发送上传开始消息
    m_networkManager->sendUploadStart(fileInfo.fileName(), fileSize, totalChunks);
}

void VideoMainWindow::pumpUploadChunks()
{
    if (!m_uploadFile || !m_uploadFile->isOpen() || m_uploadId.isEmpty())
        return;

    while (m_uploadInFlight < UPLOAD_WINDOW_SIZE
           && m_uploadNextToSend < m_uploadTotalChunks) {
        const qint64 offset = static_cast<qint64>(m_uploadNextToSend) * DEFAULT_CHUNK_SIZE;
        if (!m_uploadFile->seek(offset)) {
            qWarning() << "定位块失败：" << m_uploadNextToSend;
            break;
        }
        QByteArray chunkData = m_uploadFile->read(DEFAULT_CHUNK_SIZE);
        if (chunkData.isEmpty()) {
            qWarning() << "读取块数据失败：" << m_uploadNextToSend;
            break;
        }

        m_networkManager->sendUploadChunk(m_uploadId, m_uploadNextToSend,
                                          m_uploadTotalChunks, chunkData);
        m_uploadNextToSend++;
        m_uploadInFlight++;
    }

    tryFinishUpload();
}

void VideoMainWindow::tryFinishUpload()
{
    if (m_uploadNextToSend >= m_uploadTotalChunks && m_uploadInFlight == 0) {
        m_networkManager->sendUploadEnd(m_uploadId, m_uploadFileHash);
    }
}

void VideoMainWindow::onUploadStartResult(bool success, const QString &message, const QString &uploadId)
{
    if (!success) {
        QMessageBox::warning(this, "上传失败", "上传开始失败：" + message);
        return;
    }

    m_uploadId = uploadId;
    qDebug() << "上传开始成功，uploadId =" << m_uploadId;

    if (!openUploadFile())
        return;

    m_uploadNextToSend = 0;
    m_uploadInFlight = 0;
    savePendingUpload();
    pumpUploadChunks();
}

// 上传块结果处理
void VideoMainWindow::onUploadChunkResult(bool success, const QString &message, int chunkIndex)
{
    if (!success) {
        m_uploadInFlight = qMax(0, m_uploadInFlight - 1);
        savePendingUpload();
        closeUploadFile();

        QMessageBox retryBox(this);
        retryBox.setWindowTitle(QStringLiteral("上传失败"));
        retryBox.setText(QStringLiteral("块 %1 上传失败：%2").arg(chunkIndex).arg(message));
        retryBox.setInformativeText(QStringLiteral("可尝试断点续传，从上次进度继续。"));
        QPushButton *resumeBtn = retryBox.addButton(QStringLiteral("续传重试"),
                                                    QMessageBox::AcceptRole);
        retryBox.addButton(QMessageBox::Cancel);
        retryBox.exec();

        if (retryBox.clickedButton() == resumeBtn)
            resumeUpload();
        return;
    }

    m_uploadInFlight = qMax(0, m_uploadInFlight - 1);
    pumpUploadChunks();
}

void VideoMainWindow::onUploadResumeResult(bool success, const QString &message,
                                           const QString &uploadId, int nextChunkIndex)
{
    if (!success) {
        QMessageBox::warning(this, QStringLiteral("续传失败"),
                             QStringLiteral("续传查询失败：%1").arg(message));
        closeUploadFile();
        return;
    }

    m_uploadId = uploadId;
    savePendingUpload();

    if (nextChunkIndex >= m_uploadTotalChunks) {
        qDebug() << "续传：分片已全部上传，发送 UploadEnd";
        m_uploadNextToSend = m_uploadTotalChunks;
        m_uploadInFlight = 0;
        tryFinishUpload();
        return;
    }

    m_uploadNextToSend = nextChunkIndex;
    m_uploadInFlight = 0;
    qDebug() << "续传：从块" << nextChunkIndex << "继续，共" << m_uploadTotalChunks;
    pumpUploadChunks();
}

// 上传结束结果处理
void VideoMainWindow::onUploadEndResult(bool success, const QString &message)
{
    closeUploadFile();
    m_uploadInFlight = 0;
    m_uploadNextToSend = 0;

    if (success) {
        clearPendingUpload();
        m_uploadId.clear();
        QString userMsg;
        if (message.contains("hls_failed") || message.contains("HLS 转封装失败")) {
            userMsg = QStringLiteral(
                "视频文件已上传并保存。\n"
                "流媒体（HLS）转码未成功，列表中可能暂时无法在线播放。\n"
                "请查看服务端日志（ffmpeg 命令与错误输出），并确认服务器 ffmpeg 版本不低于 3.2、/hls/ 目录可写且 HTTP 已配置。");
        } else {
            userMsg = QStringLiteral("视频上传成功！");
        }
        QMessageBox::information(this, "上传成功", userMsg);
        // 刷新视频列表
        m_currentPage = 0;
        clearVideoGrid();
        loadVideoList();
    } else {
        savePendingUpload();
        QMessageBox retryBox(this);
        retryBox.setWindowTitle(QStringLiteral("上传失败"));
        retryBox.setText(QStringLiteral("上传结束失败：%1").arg(message));
        retryBox.setInformativeText(QStringLiteral("分片可能已全部上传，可重试完成上传。"));
        QPushButton *resumeBtn = retryBox.addButton(QStringLiteral("续传重试"),
                                                    QMessageBox::AcceptRole);
        retryBox.addButton(QMessageBox::Cancel);
        retryBox.exec();
        if (retryBox.clickedButton() == resumeBtn)
            resumeUpload();
    }
}

// 上传进度更新
void VideoMainWindow::onUploadProgress(const QString &uploadId, qint64 bytesSent, qint64 bytesTotal)
{
    qDebug() << "上传进度 [" << uploadId << "]：" << bytesSent << "/" << bytesTotal;
    // TODO: 更新进度条对话框
}

// ============================================================
// 个人中心
// ============================================================
void VideoMainWindow::onOpenProfile()
{
    if (!m_networkManager || !m_networkManager->isLoggedIn()) {
        QMessageBox::warning(this, "提示", "请先登录");
        return;
    }

    if (m_profileDialog.isNull()) {
        m_profileDialog = new UserProfileDialog(m_networkManager, this);
        // 个人中心关闭时清理指针
        connect(m_profileDialog.data(), &QDialog::finished, this, [this]() {
            // 刷新主界面视频列表（可能删除了视频）
            m_currentPage = 0;
            clearVideoGrid();
            loadVideoList();
        });
    }
    m_profileDialog->show();
    m_profileDialog->raise();
    m_profileDialog->activateWindow();
    m_profileDialog->refreshVideos();  // 每次打开刷新列表
}

void VideoMainWindow::onMyVideosResult(bool success, const QString &message, const QJsonArray &videos)
{
    if (m_profileDialog) {
        m_profileDialog->onMyVideosResult(success, message, videos);
    }
}

void VideoMainWindow::onDeleteVideoResult(bool success, const QString &message)
{
    if (m_profileDialog) {
        m_profileDialog->onDeleteVideoResult(success, message);
    }
}

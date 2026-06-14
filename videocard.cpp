#include "videocard.h"
#include <QNetworkRequest>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QBitmap>
#include <QFileInfo>
#include <QPixmap>
#include <QDebug>

// ============================================================
// 构造函数：初始化视频卡片UI
// ============================================================
VideoCard::VideoCard(int videoId,
                     const QString &coverPath,
                     const QString &title,
                     const QString &author,
                     int playCount,
                     int likesCount,
                     QWidget *parent)
    : QWidget(parent)
    , m_videoId(videoId)
    , m_coverPath(coverPath)
    , m_title(title)
    , m_author(author)
    , m_playCount(playCount)
    , m_likesCount(likesCount)
    , m_liked(false)
    , m_coverLabel(nullptr)
    , m_titleLabel(nullptr)
    , m_authorLabel(nullptr)
    , m_playCountLabel(nullptr)
    , m_likeBtn(nullptr)
    , m_coverReply(nullptr)
{
    // 初始化UI布局
    initUI();

    // 初始化网络管理器
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &VideoCard::onCoverDownloaded);

    // 异步加载封面图片
    if (!m_coverPath.isEmpty()) {
        // 判断是否为网络 URL（http/https）
        if (m_coverPath.startsWith("http://") || m_coverPath.startsWith("https://")) {
            // 从网络下载封面
            qDebug() << "VideoCard: downloading cover from" << m_coverPath;
            if (m_coverReply) {
                m_coverReply->abort();
                m_coverReply->deleteLater();
                m_coverReply = nullptr;
            }
            QUrl coverUrl(m_coverPath);
            QNetworkRequest networkRequest;
            networkRequest.setUrl(coverUrl);
            networkRequest.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
            m_coverReply = m_networkManager->get(networkRequest);
        } else if (QFileInfo::exists(m_coverPath)) {
            // 本地路径加载
            QPixmap pixmap(m_coverPath);
            if (!pixmap.isNull()) {
                onImageLoaded(pixmap.toImage());
            }
        }
    }
}

// ============================================================
// 析构函数
// ============================================================
VideoCard::~VideoCard()
{
    if (m_coverReply) {
        m_coverReply->abort();
        m_coverReply->deleteLater();
        m_coverReply = nullptr;
    }
}

// ============================================================
// 初始化UI布局
// 卡片布局：
//   ┌─────────────────────┐
//   │     [封面图片]      │  ← 16:9 固定区域（240×135）
//   ├─────────────────────┤
//   │ 标题文本            │  ← 最多两行，超出省略
//   │ 作者名 · 播放量    │  ← 灰色小字
//   └─────────────────────┘
// ============================================================
void VideoCard::initUI()
{
    // 主布局：垂直布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);  // 卡片内边距
    mainLayout->setSpacing(6);                       // 控件间距

    // === 封面标签（16:9 自适应宽度，竖屏封面居中 pillarbox）===
    m_coverLabel = new QLabel(this);
    m_coverLabel->setScaledContents(false);
    m_coverLabel->setStyleSheet(
        "QLabel {"
        "    background-color: #000000;"
        "    border-radius: 6px;"
        "}"
    );
    // 默认显示"暂无封面"占位文字
    m_coverLabel->setText("暂无封面");
    m_coverLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_coverLabel);

    // === 标题标签 ===
    m_titleLabel = new QLabel(this);
    m_titleLabel->setText(m_title.isEmpty() ? "未命名视频" : m_title);
    m_titleLabel->setWordWrap(true);           // 自动换行
    m_titleLabel->setMaximumHeight(40);        // 最多显示两行
    m_titleLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 14px;"
        "    font-weight: bold;"
        "    color: #222222;"
        "}"
    );
    mainLayout->addWidget(m_titleLabel);

    // === 作者 + 播放量 水平布局 ===
    QHBoxLayout *infoLayout = new QHBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);

    m_authorLabel = new QLabel(this);
    m_authorLabel->setText(m_author.isEmpty() ? "匿名用户" : m_author);
    m_authorLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 12px;"
        "    color: #999999;"
        "}"
    );
    infoLayout->addWidget(m_authorLabel);

    infoLayout->addStretch();  // 弹簧，把播放量挤到右边

    m_playCountLabel = new QLabel(this);
    m_playCountLabel->setText(formatPlayCount(m_playCount));
    m_playCountLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 12px;"
        "    color: #999999;"
        "}"
    );
    infoLayout->addWidget(m_playCountLabel);

    m_likeBtn = new QPushButton(this);
    m_likeBtn->setCursor(Qt::PointingHandCursor);
    m_likeBtn->setFlat(true);
    m_likeBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_likeBtn, &QPushButton::clicked, this, &VideoCard::onLikeButtonClicked);
    updateLikeButton();
    infoLayout->addWidget(m_likeBtn);

    mainLayout->addLayout(infoLayout);

    // === 卡片整体样式 ===
    this->setStyleSheet(
        "VideoCard {"
        "    background-color: white;"
        "    border: 1px solid #E0E0E0;"
        "    border-radius: 8px;"
        "}"
        "VideoCard:hover {"
        "    background-color: #F5F5F5;"  // 鼠标悬停时浅灰背景
        "    border: 1px solid #409EFF;"  // 蓝色边框高亮
        "}"
    );
    this->setCursor(Qt::PointingHandCursor);  // 鼠标悬停时显示手型光标
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateCoverGeometry();
}

void VideoCard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateCoverGeometry();
}

void VideoCard::updateCoverGeometry()
{
    if (!m_coverLabel)
        return;

    const int coverWidth = qMax(120, width() - 16);
    const int coverHeight = coverWidth * 9 / 16;
    m_coverLabel->setFixedSize(coverWidth, coverHeight);

    if (!m_coverImage.isNull())
        onImageLoaded(m_coverImage);
}

// ============================================================
// 鼠标点击事件：发出卡片点击信号
// 当用户点击卡片任意位置时触发
// ============================================================
void VideoCard::mousePressEvent(QMouseEvent *event)
{
    if (m_likeBtn && m_likeBtn->geometry().contains(event->pos()))
        return;
    qDebug() << "VideoCard clicked: videoId =" << m_videoId;
    emit SIG_cardClicked(m_videoId);
}

void VideoCard::onLikeButtonClicked()
{
    emit SIG_likeClicked(m_videoId);
}

void VideoCard::setLikeState(bool liked, int likesCount)
{
    m_liked = liked;
    m_likesCount = likesCount;
    updateLikeButton();
}

void VideoCard::updateLikeButton()
{
    if (!m_likeBtn)
        return;

    const QString icon = m_liked ? QStringLiteral("♥") : QStringLiteral("♡");
    m_likeBtn->setText(icon + QLatin1Char(' ') + formatLikeCount(m_likesCount));
    m_likeBtn->setStyleSheet(m_liked
        ? QStringLiteral("QPushButton { font-size: 12px; color: #FF4444; border: none; padding: 0 4px; }"
                         "QPushButton:hover { color: #FF6666; }")
        : QStringLiteral("QPushButton { font-size: 12px; color: #999999; border: none; padding: 0 4px; }"
                         "QPushButton:hover { color: #FF4444; }"));
}

QString VideoCard::formatLikeCount(int count)
{
    return formatPlayCount(count);
}

// ============================================================
// 图片加载完成后的处理槽函数
// 参数：image = 加载到的图片（QImage格式）
// 将图片缩放到封面标签大小，并显示
// ============================================================
void VideoCard::onImageLoaded(const QImage &image)
{
    if (!m_coverLabel || image.isNull())
        return;

    m_coverImage = image;
    const QSize target = m_coverLabel->size();
    QPixmap pixmap = QPixmap::fromImage(image);

    // 统一 16:9 居中裁剪铺满，避免竖版封面（含历史黄条 pad）在顶部露出色块
    QPixmap scaled = pixmap.scaled(target, Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
    if (scaled.width() > target.width() || scaled.height() > target.height()) {
        const int x = (scaled.width() - target.width()) / 2;
        const int y = (scaled.height() - target.height()) / 2;
        scaled = scaled.copy(x, y, target.width(), target.height());
    } else {
        QPixmap canvas(target);
        canvas.fill(Qt::black);
        QPainter painter(&canvas);
        painter.drawPixmap((target.width() - scaled.width()) / 2,
                           (target.height() - scaled.height()) / 2,
                           scaled);
        scaled = canvas;
    }
    m_coverLabel->setPixmap(scaled);
    m_coverLabel->setText("");
}

// ============================================================
// 格式化播放量
// 规则：
//   < 10000    → 直接显示数字（如：9999）
//   >= 10000   → 显示"X.X万"（如：1.5万）
//   >= 100000  → 显示"XX万"（如：15万）
//   >= 100000000 → 显示"X.X亿"（如：1.2亿）
// ============================================================
QString VideoCard::formatPlayCount(int count)
{
    if (count < 10000) {
        return QString::number(count);
    } else if (count < 100000000) {
        double wan = count / 10000.0;
        if (wan == int(wan)) {
            return QString::number(int(wan)) + "万";
        } else {
            return QString::number(wan, 'f', 1) + "万";
        }
    } else {
        double yi = count / 100000000.0;
        if (yi == int(yi)) {
            return QString::number(int(yi)) + "亿";
        } else {
            return QString::number(yi, 'f', 1) + "亿";
        }
    }
}

// ============================================================
// 网络封面下载完成后的处理槽函数
// 参数：reply = 网络回复对象
// 读取图片数据，加载为 QImage，然后显示
// ============================================================
void VideoCard::onCoverDownloaded(QNetworkReply *reply)
{
    if (!reply) return;
    if (reply != m_coverReply)
        return;

    // 检查是否有错误
    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "VideoCard: cover download failed:" << reply->errorString();
        m_coverReply = nullptr;
        reply->deleteLater();
        return;
    }

    // 读取图片数据
    QByteArray imageData = reply->readAll();
    m_coverReply = nullptr;
    reply->deleteLater();

    if (imageData.isEmpty()) {
        qDebug() << "VideoCard: cover data is empty";
        return;
    }

    // 加载图片
    QImage image;
    if (image.loadFromData(imageData)) {
        qDebug() << "VideoCard: cover loaded successfully, size =" << image.size();
        onImageLoaded(image);
    } else {
        qDebug() << "VideoCard: failed to load cover image from data";
    }
}

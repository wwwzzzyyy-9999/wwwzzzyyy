#include "userprofiledialog.h"

UserProfileDialog::UserProfileDialog(NetworkManager *networkManager,
                                     QWidget *parent)
    : QDialog(parent)
    , m_networkManager(networkManager)
    , m_listWidget(nullptr)
    , m_deleteBtn(nullptr)
    , m_statusLabel(nullptr)
    , m_selectedVideoId(-1)
{
    initUI();
}

UserProfileDialog::~UserProfileDialog()
{
}

void UserProfileDialog::initUI()
{
    this->setWindowTitle("个人中心 - 我的视频");
    this->resize(500, 400);
    this->setMinimumSize(400, 300);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // 标题
    QLabel *titleLabel = new QLabel("我上传的视频", this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
    mainLayout->addWidget(titleLabel);

    // 状态提示
    m_statusLabel = new QLabel("正在加载...", this);
    m_statusLabel->setStyleSheet("color: #999; font-size: 13px;");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    // 视频列表
    m_listWidget = new QListWidget(this);
    m_listWidget->setStyleSheet(
        "QListWidget { border: 1px solid #E0E0E0; border-radius: 6px; font-size: 14px; }"
        "QListWidget::item { padding: 10px 12px; border-bottom: 1px solid #F0F0F0; }"
        "QListWidget::item:selected { background-color: #E6F7FF; color: #409EFF; }"
        "QListWidget::item:hover { background-color: #F5F5F5; }"
    );
    mainLayout->addWidget(m_listWidget);

    // 按钮行
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_deleteBtn = new QPushButton("删除选中视频", this);
    m_deleteBtn->setFixedHeight(36);
    m_deleteBtn->setStyleSheet(
        "QPushButton { background-color: #FF4444; color: white; border-radius: 6px; "
        "padding: 0 20px; font-size: 14px; }"
        "QPushButton:hover { background-color: #FF6666; }"
        "QPushButton:disabled { background-color: #DDD; color: #999; }"
    );
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, &UserProfileDialog::onDeleteClicked);
    btnLayout->addWidget(m_deleteBtn);

    mainLayout->addLayout(btnLayout);

    // 连接列表选择变化
    connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            QListWidgetItem *item = m_listWidget->item(row);
            m_selectedVideoId = item->data(Qt::UserRole).toInt();
            m_deleteBtn->setEnabled(true);
        } else {
            m_selectedVideoId = -1;
            m_deleteBtn->setEnabled(false);
        }
    });
}

void UserProfileDialog::refreshVideos()
{
    m_listWidget->clear();
    m_videoTitleMap.clear();
    m_selectedVideoId = -1;
    m_deleteBtn->setEnabled(false);
    m_statusLabel->setText("正在加载...");

    if (m_networkManager) {
        m_networkManager->sendGetMyVideos();  // 请求个人视频列表
    }
}

void UserProfileDialog::onMyVideosResult(bool success, const QString &message,
                                          const QJsonArray &videos)
{
    if (!success) {
        m_statusLabel->setText("加载失败：" + message);
        return;
    }

    m_listWidget->clear();
    m_videoTitleMap.clear();

    if (videos.isEmpty()) {
        m_statusLabel->setText("暂无上传的视频");
        return;
    }

    m_statusLabel->setText(QString("共 %1 个视频").arg(videos.size()));

    for (const QJsonValue &v : videos) {
        QJsonObject obj = v.toObject();
        int videoId   = obj["id"].toInt();
        QString title = obj["title"].toString("未命名");
        QString author = obj["author"].toString();
        int playCount = obj["playCount"].toInt();

        m_videoTitleMap[videoId] = title;

        QListWidgetItem *item = new QListWidgetItem();
        item->setText(QString("%1  [播放: %2]").arg(title).arg(playCount));
        item->setData(Qt::UserRole, videoId);
        m_listWidget->addItem(item);
    }
}

void UserProfileDialog::onDeleteVideoResult(bool success, const QString &message)
{
    if (success) {
        QMessageBox::information(this, "成功", "视频已删除");
        refreshVideos();  // 刷新列表
    } else {
        QMessageBox::warning(this, "失败", "删除失败：" + message);
    }
}

void UserProfileDialog::onDeleteClicked()
{
    if (m_selectedVideoId <= 0) return;

    QString title = m_videoTitleMap.value(m_selectedVideoId, "未知视频");

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认删除",
        QString("确定要删除视频「%1」吗？\n此操作不可恢复。").arg(title),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        m_statusLabel->setText("正在删除...");
        if (m_networkManager) {
            m_networkManager->sendDeleteVideo(m_selectedVideoId);
        }
    }
}

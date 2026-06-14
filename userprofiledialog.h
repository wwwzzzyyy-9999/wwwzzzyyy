#ifndef USERPROFILEDIALOG_H
#define USERPROFILEDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QDebug>
#include "networkmanager.h"

class UserProfileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserProfileDialog(NetworkManager *networkManager,
                                QWidget *parent = nullptr);
    ~UserProfileDialog();

    // 刷新视频列表
    void refreshVideos();

    // 网络回调（由 VideoMainWindow 转发）
    void onMyVideosResult(bool success, const QString &message, const QJsonArray &videos);
    void onDeleteVideoResult(bool success, const QString &message);

private slots:
    void onDeleteClicked();

private:
    void initUI();

    NetworkManager *m_networkManager;
    QListWidget    *m_listWidget;
    QPushButton    *m_deleteBtn;
    QLabel         *m_statusLabel;

    // 当前选中的视频 ID（用于删除确认）
    int m_selectedVideoId;
    // videoId → title 映射（用于显示）
    QMap<int, QString> m_videoTitleMap;
};

#endif // USERPROFILEDIALOG_H

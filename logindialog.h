#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QMessageBox>
#include "networkmanager.h"

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr, NetworkManager* networkManager = nullptr);
    ~LoginDialog();

signals:
    void SIG_loginSuccess(const QString& username, const QString& token);

private slots:
    void on_pb_login_clicked();
    void on_pb_register_clicked();
    void on_pb_cancel_clicked();
    
    // 网络响应槽函数
    void slot_loginResult(bool success, const QString& message, const QString& token);
    void slot_connected();
    void slot_error(const QString& errorString);

private:
    Ui::LoginDialog *ui;
    NetworkManager* m_networkManager;
    bool m_isConnecting;  // 防止重复点击连接

    void initNetwork();
    bool validateInput();
    void setUiEnabled(bool enabled);
};

#endif // LOGINDIALOG_H

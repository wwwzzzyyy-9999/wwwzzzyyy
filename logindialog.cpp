#include "logindialog.h"
#include "ui_logindialog.h"
#include "registerdialog.h"
#include <QMessageBox>

LoginDialog::LoginDialog(QWidget *parent, NetworkManager* networkManager)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
    , m_networkManager(networkManager)
    , m_isConnecting(false)
{
    ui->setupUi(this);
    
    // 设置窗口属性
    this->setWindowTitle("短视频播放平台 - 登录");
    this->setFixedSize(400, 300);
    
    // 设置密码输入框为密码模式
    ui->le_password->setEchoMode(QLineEdit::Password);
    
    // 初始化网络管理
    initNetwork();
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

void LoginDialog::initNetwork()
{
    if (!m_networkManager) {
        QMessageBox::critical(this, "错误", "网络管理器未初始化！");
        return;
    }

    // 先断开旧连接，防止多次构造时信号槽累积
    disconnect(m_networkManager, &NetworkManager::SIG_connected,
               this, &LoginDialog::slot_connected);
    disconnect(m_networkManager, &NetworkManager::SIG_loginResult,
               this, &LoginDialog::slot_loginResult);
    disconnect(m_networkManager, &NetworkManager::SIG_error,
               this, &LoginDialog::slot_error);

    connect(m_networkManager, &NetworkManager::SIG_connected,
            this, &LoginDialog::slot_connected);
    connect(m_networkManager, &NetworkManager::SIG_loginResult,
            this, &LoginDialog::slot_loginResult);
    connect(m_networkManager, &NetworkManager::SIG_error,
            this, &LoginDialog::slot_error);
}

bool LoginDialog::validateInput()
{
    QString username = ui->le_username->text().trimmed();
    QString password = ui->le_password->text();
    
    if (username.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入用户名");
        ui->le_username->setFocus();
        return false;
    }
    
    if (password.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入密码");
        ui->le_password->setFocus();
        return false;
    }
    
    return true;
}

void LoginDialog::on_pb_login_clicked()
{
    if (!validateInput()) {
        return;
    }

    // 防止重复点击
    if (m_isConnecting) {
        return;
    }

    // 如果已连接，直接发送登录请求
    if (m_networkManager->isSocketConnected()) {
        QString username = ui->le_username->text().trimmed();
        QString password = ui->le_password->text();
        m_networkManager->sendLogin(username, password);
        return;
    }

    // 发起连接
    m_isConnecting = true;
    setUiEnabled(false);
    m_networkManager->connectToServer();
}

void LoginDialog::on_pb_register_clicked()
{
    // 打开注册对话框，传入 NetworkManager
    RegisterDialog registerDialog(this, m_networkManager);
    registerDialog.exec();
    // 注册成功/失败由 RegisterDialog 自行处理，这里无需额外提示
}

void LoginDialog::on_pb_cancel_clicked()
{
    this->reject();
}

void LoginDialog::slot_connected()
{
    m_isConnecting = false;
    setUiEnabled(true);

    // 连接成功后发送登录请求
    QString username = ui->le_username->text().trimmed();
    QString password = ui->le_password->text();
    m_networkManager->sendLogin(username, password);
}

void LoginDialog::slot_loginResult(bool success, const QString& message, const QString& token)
{
    setUiEnabled(true);

    if (success) {
        // 成功无需提示，直接关闭对话框进入主界面
        emit SIG_loginSuccess(ui->le_username->text().trimmed(), token);
        this->accept();
    } else {
        // 失败必须有提示，确保 message 不为空
        QString errMsg = message.isEmpty() ? "登录失败，请重试" : message;
        QMessageBox::warning(this, "登录失败", errMsg);
    }
}

void LoginDialog::slot_error(const QString& errorString)
{
    m_isConnecting = false;
    setUiEnabled(true);
    QMessageBox::critical(this, "错误", QString("网络错误：%1").arg(errorString));
}

void LoginDialog::setUiEnabled(bool enabled)
{
    ui->pb_login->setEnabled(enabled);
    ui->pb_register->setEnabled(enabled);
    ui->le_username->setEnabled(enabled);
    ui->le_password->setEnabled(enabled);
}

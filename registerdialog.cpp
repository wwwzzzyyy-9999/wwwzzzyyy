#include "registerdialog.h"
#include "ui_registerdialog.h"

RegisterDialog::RegisterDialog(QWidget *parent, NetworkManager* networkManager)
    : QDialog(parent)
    , ui(new Ui::RegisterDialog)
    , m_networkManager(networkManager)
    , m_isConnecting(false)
{
    ui->setupUi(this);
    
    // 设置窗口属性
    this->setWindowTitle("短视频播放平台 - 注册");
    this->setFixedSize(400, 350);
    
    // 设置密码输入框为密码模式
    ui->le_password->setEchoMode(QLineEdit::Password);
    ui->le_confirmPassword->setEchoMode(QLineEdit::Password);
    
    // 初始化网络管理
    initNetwork();
}

RegisterDialog::~RegisterDialog()
{
    delete ui;
}

void RegisterDialog::initNetwork()
{
    if (!m_networkManager) {
        QMessageBox::critical(this, "错误", "网络管理器未初始化！");
        return;
    }

    connect(m_networkManager, &NetworkManager::SIG_connected,
            this, &RegisterDialog::slot_connected);
    connect(m_networkManager, &NetworkManager::SIG_registerResult,
            this, &RegisterDialog::slot_registerResult);
    connect(m_networkManager, &NetworkManager::SIG_error,
            this, &RegisterDialog::slot_error);
}

bool RegisterDialog::validateInput()
{
    QString username = ui->le_username->text().trimmed();
    QString password = ui->le_password->text();
    QString confirmPassword = ui->le_confirmPassword->text();
    
    if (username.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入用户名");
        ui->le_username->setFocus();
        return false;
    }
    
    if (username.length() < 3 || username.length() > 20) {
        QMessageBox::warning(this, "提示", "用户名长度应在3-20个字符之间");
        ui->le_username->setFocus();
        return false;
    }
    
    if (password.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入密码");
        ui->le_password->setFocus();
        return false;
    }
    
    if (password.length() < 6) {
        QMessageBox::warning(this, "提示", "密码长度不能少于6个字符");
        ui->le_password->setFocus();
        return false;
    }
    
    if (password != confirmPassword) {
        QMessageBox::warning(this, "提示", "两次输入的密码不一致");
        ui->le_confirmPassword->setFocus();
        return false;
    }
    
    return true;
}

void RegisterDialog::on_pb_register_clicked()
{
    if (!validateInput()) {
        return;
    }

    if (m_isConnecting) {
        return;
    }

    // 如果已连接，直接发送注册请求
    if (m_networkManager->isSocketConnected()) {
        QString username = ui->le_username->text().trimmed();
        QString password = ui->le_password->text();
        m_networkManager->sendRegister(username, password);
        return;
    }

    m_isConnecting = true;
    setUiEnabled(false);
    m_networkManager->connectToServer();
}

void RegisterDialog::on_pb_cancel_clicked()
{
    this->reject();
}

void RegisterDialog::slot_connected()
{
    m_isConnecting = false;
    setUiEnabled(true);

    QString username = ui->le_username->text().trimmed();
    QString password = ui->le_password->text();
    m_networkManager->sendRegister(username, password);
}

void RegisterDialog::slot_registerResult(bool success, const QString& message)
{
    setUiEnabled(true);

    if (success) {
        // 成功无需提示，直接关闭对话框返回登录界面
        this->accept();
    } else {
        // 失败必须有提示，确保 message 不为空
        QString errMsg = message.isEmpty() ? "注册失败，请重试" : message;
        QMessageBox::warning(this, "注册失败", errMsg);
    }
}

void RegisterDialog::slot_error(const QString& errorString)
{
    m_isConnecting = false;
    setUiEnabled(true);
    QMessageBox::critical(this, "错误", QString("网络错误：%1").arg(errorString));
}

void RegisterDialog::setUiEnabled(bool enabled)
{
    ui->pb_register->setEnabled(enabled);
    ui->pb_cancel->setEnabled(enabled);
    ui->le_username->setEnabled(enabled);
    ui->le_password->setEnabled(enabled);
    ui->le_confirmPassword->setEnabled(enabled);
}

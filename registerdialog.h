#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H

#include <QDialog>
#include <QMessageBox>
#include "networkmanager.h"

namespace Ui {
class RegisterDialog;
}

class RegisterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RegisterDialog(QWidget *parent = nullptr, NetworkManager* networkManager = nullptr);
    ~RegisterDialog();

private slots:
    void on_pb_register_clicked();
    void on_pb_cancel_clicked();

    // 网络响应槽函数
    void slot_registerResult(bool success, const QString& message);
    void slot_connected();
    void slot_error(const QString& errorString);

private:
    Ui::RegisterDialog *ui;
    NetworkManager* m_networkManager;
    bool m_isConnecting;

    void initNetwork();
    bool validateInput();
    void setUiEnabled(bool enabled);
};

#endif // REGISTERDIALOG_H

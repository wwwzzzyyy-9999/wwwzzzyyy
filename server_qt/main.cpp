#include <QCoreApplication>
#include <QDebug>
#include <QStringList>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <cstdlib>
#include "server.h"
#include "redisconfig.h"

struct ServerConfig {
    QString hlsIp;
    int     hlsPort;
    int     tcpPort;
    QString hlsOutputDir;
};

static ServerConfig loadServerConfig()
{
    ServerConfig cfg;
    cfg.hlsIp   = "10.56.5.9";
    cfg.hlsPort = 8080;
    cfg.tcpPort = 8888;
    cfg.hlsOutputDir = QCoreApplication::applicationDirPath() + "/hls";

    // 与客户端一致：优先当前工作目录，其次可执行文件目录
    QString configPath = QDir::currentPath() + "/config.ini";
    if (!QFile::exists(configPath))
        configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    qDebug() << "Reading config:" << configPath;

    if (!QFile::exists(configPath)) {
        qDebug() << "Config not found, creating default:" << configPath;
        QSettings settings(configPath, QSettings::IniFormat);
        settings.beginGroup("Server");
        settings.setValue("hls_ip", cfg.hlsIp);
        settings.setValue("hls_port", cfg.hlsPort);
        settings.setValue("tcp_port", cfg.tcpPort);
        settings.setValue("hls_output_dir", cfg.hlsOutputDir);
        settings.endGroup();
        settings.sync();
    } else {
        QSettings settings(configPath, QSettings::IniFormat);
        settings.beginGroup("Server");
        cfg.hlsIp = settings.value("hls_ip", cfg.hlsIp).toString();
        cfg.hlsPort = settings.value("hls_port", cfg.hlsPort).toInt();
        cfg.tcpPort = settings.value("tcp_port", cfg.tcpPort).toInt();
        cfg.hlsOutputDir = settings.value("hls_output_dir", cfg.hlsOutputDir).toString();
        settings.endGroup();
    }

    if (cfg.hlsOutputDir.isEmpty())
        cfg.hlsOutputDir = QCoreApplication::applicationDirPath() + "/hls";

    QDir().mkpath(cfg.hlsOutputDir);
    qDebug() << "Config loaded: hls_ip =" << cfg.hlsIp
             << ", tcp_port =" << cfg.tcpPort
             << ", hls_port =" << cfg.hlsPort
             << ", hls_output_dir =" << cfg.hlsOutputDir;
    return cfg;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setApplicationName("VideoRecorderServer");
    QCoreApplication::setApplicationVersion("1.0");

    qsrand(static_cast<uint>(QDateTime::currentMSecsSinceEpoch()));

    ServerConfig cfg = loadServerConfig();
    quint16 port = static_cast<quint16>(cfg.tcpPort > 0 ? cfg.tcpPort : 8888);

    QStringList args = QCoreApplication::arguments();
    if (args.size() > 1) {
        bool ok;
        quint16 argPort = args[1].toUShort(&ok);
        if (ok && argPort > 0) {
            port = argPort;
            qDebug() << "命令行指定 TCP 端口：" << port
                     << "（HLS 由 Nginx 提供，hls_port =" << cfg.hlsPort << "）";
        } else {
            qDebug() << "Usage:" << args[0] << "[tcp_port]";
            return 1;
        }
    }

    if (port == static_cast<quint16>(cfg.hlsPort)) {
        qDebug() << "错误：TCP 端口与 hls_port 不能相同（" << port << "）";
        qDebug() << "请在 config.ini 中设置 tcp_port=8888、hls_port=8080";
        return 1;
    }

    const RedisConfig redisCfg = loadRedisConfig();
    Server server(cfg.hlsIp, cfg.hlsPort, cfg.hlsOutputDir, redisCfg);

    if (!server.start(port)) {
        qDebug() << "Server start failed";
        return 1;
    }

    return a.exec();
}

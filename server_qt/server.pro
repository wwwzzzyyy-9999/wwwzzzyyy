# ============================================================
# 短视频录制平台 - 服务器项目文件
# 开发环境：Linux + Qt Creator 5.9.9
# 编译：qmake server.pro && make
# ============================================================

TEMPLATE = app
TARGET = VideoRecorderServer

QT += core network sql
QT -= gui

CONFIG += c++11
CONFIG += console
CONFIG -= app_bundle

SOURCES += \
    main.cpp \
    server.cpp \
    epollserver.cpp \
    database.cpp \
    recommendengine.cpp \
    redisclient.cpp \
    redisservice.cpp

HEADERS += \
    protocol.h \
    server.h \
    epollserver.h \
    threadpool.h \
    database.h \
    recommendengine.h \
    auth_util.h \
    redisclient.h \
    redisservice.h \
    redisconfig.h

QMAKE_CXXFLAGS += -Wall -Wextra
QMAKE_CXXFLAGS_RELEASE += -O2

# Linux 下需要链接的系统库（与原先 Makefile 一致，由 qmake 自动带上 Qt 与 pthread）
unix:!macx {
    QMAKE_CXXFLAGS += -pthread
    QMAKE_LFLAGS += -pthread
    exists(/usr/lib/x86_64-linux-gnu/libmysqlclient.so) | exists(/usr/lib/libmysqlclient.so) {
        LIBS += -lmysqlclient
    } else:exists(/usr/lib/x86_64-linux-gnu/libmariadb.so) | exists(/usr/lib/libmariadb.so) {
        LIBS += -lmariadb
    }
}

target.path = /usr/local/bin
INSTALLS += target

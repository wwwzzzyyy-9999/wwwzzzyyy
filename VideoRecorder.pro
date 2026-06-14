QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

DEFINES += SDL_MAIN_HANDLED

SOURCES += \
    main.cpp \
    networkmanager.cpp \
    logindialog.cpp \
    registerdialog.cpp \
    videoplayer.cpp \
    PacketQueue.cpp \
    playerdialog.cpp \
    videocard.cpp \
    videomainwindow.cpp \
    userprofiledialog.cpp

HEADERS += \
    protocol.h \
    auth_util.h \
    networkmanager.h \
    logindialog.h \
    registerdialog.h \
    videoplayer.h \
    PacketQueue.h \
    playerdialog.h \
    videocard.h \
    videomainwindow.h \
    userprofiledialog.h

FORMS += \
    logindialog.ui \
    registerdialog.ui \
    playerdialog.ui

INCLUDEPATH += $$PWD/ffmpeg-4.2.2/include \
    $$PWD/SDL2-2.0.10/include \
    $$PWD/opengl/

LIBS += $$PWD/ffmpeg-4.2.2/lib/libavcodec.dll.a \
    $$PWD/ffmpeg-4.2.2/lib/libavformat.dll.a \
    $$PWD/ffmpeg-4.2.2/lib/libavutil.dll.a \
    $$PWD/ffmpeg-4.2.2/lib/libswresample.dll.a \
    $$PWD/ffmpeg-4.2.2/lib/libswscale.dll.a \
    $$PWD/SDL2-2.0.10/lib/x86/SDL2.lib

include($$PWD/opengl/opengl.pri)

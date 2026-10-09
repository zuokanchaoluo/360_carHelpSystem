#-------------------------------------------------
#
# Project created by QtCreator 2026-02-08T12:52:23
#
#-------------------------------------------------

QT       += core gui concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = third_360Car
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11

SOURCES += \
        main.cpp \
        widget.cpp \
    socketconnect.cpp \
    readthread.cpp \
    writethread.cpp \
    loginwidget.cpp \
    registerwidget.cpp \
    mainmanager.cpp \
    videorecordpage.cpp \
    drivingmodepage.cpp \
    reversemodepage.cpp \
    birdviewpage.cpp \
    featurememorypage.cpp \
    settingspage.cpp \
    startupwidget.cpp \
    camerathread.cpp \
    videoplayer.cpp

HEADERS  += \
        widget.h \
    protocol.h \
    socketconnect.h \
    readthread.h \
    writethread.h \
    loginwidget.h \
    registerwidget.h \
    mainmanager.h \
    videorecordpage.h \
    drivingmodepage.h \
    reversemodepage.h \
    birdviewpage.h \
    featurememorypage.h \
    settingspage.h \
    startupwidget.h \
    camerathread.h \
    videoplayer.h

FORMS    += \
        widget.ui \
    loginwidget.ui \
    registerwidget.ui \
    mainmanager.ui \
    videorecordpage.ui \
    drivingmodepage.ui \
    reversemodepage.ui \
    birdviewpage.ui \
    featurememorypage.ui \
    settingspage.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
#Linux opencv
INCLUDEPATH +=/usr/local/include/
LIBS+=/usr/local/lib/libopencv_world.so

RESOURCES += \
    resources.qrc

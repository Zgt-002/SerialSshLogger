QT += core gui widgets serialport testlib
greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = logger_tests
win32-msvc: QMAKE_CXXFLAGS += /utf-8
unix:!macx: LIBS += -lutil

include(../libssh.pri)
INCLUDEPATH += ..
SOURCES += test_logger.cpp \
    ../terminalstream.cpp \
    ../logwriter.cpp \
    ../captureworker.cpp \
    ../terminalview.cpp \
    ../sessionpanel.cpp
HEADERS += ../terminalstream.h \
    ../logwriter.h \
    ../captureworker.h \
    ../terminalview.h \
    ../sessionpanel.h

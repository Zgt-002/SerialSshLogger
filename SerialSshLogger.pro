QT += core gui widgets serialport
greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat
CONFIG += c++17
TEMPLATE = app
TARGET = SerialSshLogger
VERSION = 1.0.0
win32-msvc: QMAKE_CXXFLAGS += /utf-8

include(libssh.pri)

SOURCES += main.cpp \
    sessionpanel.cpp \
    terminalview.cpp \
    captureworker.cpp \
    terminalstream.cpp \
    logwriter.cpp

HEADERS += sessionpanel.h \
    terminalview.h \
    captureworker.h \
    terminalstream.h \
    logwriter.h

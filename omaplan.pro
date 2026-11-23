QT += core gui qml quick quickcontrols2 dbus sql

CONFIG += c++17 release
TARGET = omaplan
TEMPLATE = app

HEADERS += \
    src/theme.h \
    src/translator.h \
    src/task.h \
    src/store.h \
    src/actions.h \
    src/cli.h

SOURCES += \
    src/main.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/task.cpp \
    src/store.cpp \
    src/actions.cpp \
    src/cli.cpp

RESOURCES += src/resources.qrc

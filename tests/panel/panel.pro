# The settings panel, offscreen: the whole window (qrc:/Main.qml) with
# invented data, a fake command engine and no real AI. Keys and clicks as a
# user gives them.
QT += core gui qml quick quickcontrols2 sql network testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
CONFIG += link_pkgconfig
PKGCONFIG += fontconfig
TARGET = panel-tests
TEMPLATE = app

SRC = $$PWD/../../src
INCLUDEPATH += $$SRC
HEADERS += $$files($$SRC/*.h)
SOURCES += tst_panel.cpp $$files($$SRC/*.cpp)
SOURCES -= $$SRC/main.cpp
RESOURCES += $$files($$SRC/*.qrc)
TRANSLATIONS = $$files($$PWD/../../i18n/omaplan_*.ts)
CONFIG += lrelease embed_translations

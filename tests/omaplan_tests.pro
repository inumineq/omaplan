QT += core sql testlib
QT -= gui

CONFIG += c++17 testcase console
TARGET = omaplan_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/task.h ../src/store.h ../src/actions.h ../src/cli.h ../src/planmodel.h
SOURCES += omaplan_tests.cpp ../src/task.cpp ../src/store.cpp ../src/actions.cpp ../src/cli.cpp ../src/planmodel.cpp

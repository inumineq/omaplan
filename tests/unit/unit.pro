QT += core network testlib
QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = ai-unit-tests
TEMPLATE = app

# Fixtures live next to this project; baked in as an absolute path so the
# tests find them no matter where the binary is run from.
DEFINES += FIXTURES_DIR=\\\"$$PWD/../ai-fixtures\\\"

INCLUDEPATH += ../../src

SOURCES += \
    tst_ai.cpp \
    ../../src/engine.cpp \
    ../../src/engine_vibe.cpp \
    ../../src/engine_claude.cpp \
    ../../src/engine_local.cpp \
    ../../src/engine_command.cpp \
    ../../src/prompt.cpp \
    ../../src/proposal.cpp \
    ../../src/config.cpp

HEADERS += \
    ../../src/engine.h \
    ../../src/prompt.h \
    ../../src/proposal.h \
    ../../src/config.h

RESOURCES += ../../src/ai-schema.qrc

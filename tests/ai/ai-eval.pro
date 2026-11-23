QT += core network
QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = ai-eval
TEMPLATE = app

# Fixtures live next to this project; baked in as an absolute path so the
# binary finds them no matter where it is run from.
DEFINES += FIXTURES_DIR=\\\"$$PWD/../ai-fixtures\\\"

INCLUDEPATH += ../../src

SOURCES += \
    eval.cpp \
    ../../src/engine.cpp \
    ../../src/engine_vibe.cpp \
    ../../src/engine_claude.cpp \
    ../../src/engine_ollama.cpp \
    ../../src/engine_command.cpp \
    ../../src/prompt.cpp \
    ../../src/proposal.cpp

HEADERS += \
    ../../src/engine.h \
    ../../src/prompt.h \
    ../../src/proposal.h

RESOURCES += ../../src/ai-skema.qrc

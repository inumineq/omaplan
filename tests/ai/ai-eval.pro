QT += core gui sql network
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = ai-eval
TEMPLATE = app

# Fixtures live next to this project; baked in as an absolute path so the
# binary finds them no matter where it is run from.
DEFINES += FIXTURES_DIR=\\\"$$PWD/../ai-fixtures\\\"

INCLUDEPATH += ../../src

# Same sources as `omaplan add` needs, so eval runs the exact same path:
# date annotation (dateannotate -> Actions::parseDateTime), the engine
# factory with config.toml, and prompt building inside AiEngine::propose.
SOURCES += \
    eval.cpp \
    ../../src/actions.cpp \
    ../../src/config.cpp \
    ../../src/dateannotate.cpp \
    ../../src/engine.cpp \
    ../../src/engine_vibe.cpp \
    ../../src/engine_claude.cpp \
    ../../src/engine_local.cpp \
    ../../src/engine_command.cpp \
    ../../src/prompt.cpp \
    ../../src/proposal.cpp \
    ../../src/store.cpp \
    ../../src/task.cpp \
    ../../src/translator.cpp

HEADERS += \
    ../../src/actions.h \
    ../../src/config.h \
    ../../src/dateannotate.h \
    ../../src/engine.h \
    ../../src/prompt.h \
    ../../src/proposal.h \
    ../../src/store.h \
    ../../src/task.h \
    ../../src/translator.h

RESOURCES += ../../src/ai-schema.qrc

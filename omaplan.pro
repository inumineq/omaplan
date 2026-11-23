QT += core gui qml quick quickcontrols2 dbus sql network

CONFIG += c++17 release
TARGET = omaplan
TEMPLATE = app

HEADERS += \
    src/theme.h \
    src/translator.h \
    src/task.h \
    src/store.h \
    src/actions.h \
    src/cli.h \
    src/planmodel.h \
    src/config.h \
    src/dateannotate.h \
    src/engine.h \
    src/prompt.h \
    src/proposal.h

SOURCES += \
    src/main.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/task.cpp \
    src/store.cpp \
    src/actions.cpp \
    src/cli.cpp \
    src/planmodel.cpp \
    src/config.cpp \
    src/dateannotate.cpp \
    src/engine.cpp \
    src/engine_vibe.cpp \
    src/engine_claude.cpp \
    src/engine_local.cpp \
    src/engine_command.cpp \
    src/prompt.cpp \
    src/proposal.cpp

RESOURCES += src/resources.qrc src/ai-skema.qrc

QT += core gui qml quick quickcontrols2 sql network

CONFIG += c++17 release
TARGET = omaplan
TEMPLATE = app

# The one place for the version: --version, the MCP serverInfo and (checked
# by a test) pkgbuild/PKGBUILD.
VERSION = 0.2.0
DEFINES += OMAPLAN_VERSION=\\\"$$VERSION\\\"

HEADERS += \
    src/theme.h \
    src/uilanguage.h \
    src/task.h \
    src/store.h \
    src/actions.h \
    src/cli.h \
    src/planmodel.h \
    src/config.h \
    src/dateannotate.h \
    src/datewords.h \
    src/skill.h \
    src/engine.h \
    src/prompt.h \
    src/proposal.h \
    src/mcp.h \
    src/render.h \
    src/snapshot.h \
    src/server.h

SOURCES += \
    src/main.cpp \
    src/theme.cpp \
    src/uilanguage.cpp \
    src/task.cpp \
    src/store.cpp \
    src/actions.cpp \
    src/cli.cpp \
    src/planmodel.cpp \
    src/config.cpp \
    src/dateannotate.cpp \
    src/datewords.cpp \
    src/skill.cpp \
    src/engine.cpp \
    src/engine_vibe.cpp \
    src/engine_claude.cpp \
    src/engine_local.cpp \
    src/engine_command.cpp \
    src/prompt.cpp \
    src/proposal.cpp \
    src/mcp.cpp \
    src/render.cpp \
    src/snapshot.cpp \
    src/server.cpp

RESOURCES += src/resources.qrc src/ai-schema.qrc src/skill.qrc src/web.qrc

# Translations: every i18n/omaplan_<lang>.ts is compiled by lrelease and
# embedded as :/i18n/omaplan_<lang>.qm, so a new language needs no code
# change (see docs/translating.md).
TRANSLATIONS = $$files(i18n/omaplan_*.ts)
CONFIG += lrelease embed_translations

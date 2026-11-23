QT += core gui sql network testlib dbus qml

CONFIG += c++17 testcase console
TARGET = omaplan_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/task.h ../src/store.h ../src/actions.h ../src/cli.h ../src/planmodel.h \
           ../src/config.h ../src/dateannotate.h ../src/skill.h \
           ../src/engine.h ../src/prompt.h ../src/proposal.h ../src/translator.h ../src/mcp.h ../src/theme.h \
           ../src/render.h ../src/snapshot.h ../src/server.h
SOURCES += omaplan_tests.cpp ../src/task.cpp ../src/store.cpp ../src/actions.cpp ../src/cli.cpp \
           ../src/planmodel.cpp ../src/config.cpp ../src/dateannotate.cpp ../src/skill.cpp \
           ../src/engine.cpp ../src/engine_vibe.cpp ../src/engine_claude.cpp ../src/engine_local.cpp \
           ../src/engine_command.cpp ../src/prompt.cpp ../src/proposal.cpp ../src/translator.cpp ../src/mcp.cpp ../src/theme.cpp \
           ../src/render.cpp ../src/snapshot.cpp ../src/server.cpp

RESOURCES += ../src/ai-skema.qrc ../src/skill.qrc ../src/web.qrc
# The source tree, for checks over the QML and the translations.
DEFINES += OMAPLAN_SRC=\\\"$$PWD/../src\\\"

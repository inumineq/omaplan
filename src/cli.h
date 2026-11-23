#pragma once

#include <QStringList>

class Store;
class QTextStream;

// The command line over Actions, in Hype's cli.cpp pattern: every command
// takes --json, exits 0/1, writes errors to stderr. `args` excludes argv[0].
// main() runs this offscreen (QT_QPA_PLATFORM=offscreen) and keeps the GUI
// behind `omaplan open`; tests pass their own store and streams.
int runCli(const QStringList &args, Store *store, QTextStream &out, QTextStream &err);

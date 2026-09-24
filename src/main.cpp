#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("textdichter"));
    app.setApplicationVersion(QStringLiteral(TEXTDICHTER_VERSION));
    app.setDesktopFileName(QStringLiteral("textdichter"));

    // Qt's own strings (standard buttons, file dialogs) and ours.
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("textdichter"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        app.installTranslator(&translator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("main", "A light Markdown editor."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QApplication::translate("main", "Markdown file to open."),
                                 QStringLiteral("[file]"));
    parser.process(app);

    MainWindow window;
    if (!parser.positionalArguments().isEmpty())
        window.openFromCommandLine(parser.positionalArguments().first());
    window.show();
    return app.exec();
}

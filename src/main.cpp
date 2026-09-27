#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    // Before the app exists, so Qt registers it with the desktop portal before
    // anything else asks the portal; later the portal refuses the name.
    QGuiApplication::setDesktopFileName(QStringLiteral("textdichter"));
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("textdichter"));
    app.setApplicationVersion(QStringLiteral(TEXTDICHTER_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/textdichter.svg")));

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
    else
        window.recoverUntitled();
    window.show();
    return app.exec();
}

#include "selftest.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QIcon>
#include <QTimer>

namespace {

// Receives "open this file" requests from Finder (macOS file associations).
class SpotlightApp : public QApplication {
public:
    using QApplication::QApplication;
    spotlight::MainWindow *window = nullptr;
    QString pendingFile;

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::FileOpen) {
            const QString path = static_cast<QFileOpenEvent *>(e)->file();
            if (window)
                window->openProject(path);
            else
                pendingFile = path;
            return true;
        }
        return QApplication::event(e);
    }
};

} // namespace

int main(int argc, char *argv[])
{
    // Prefer Qt Multimedia's FFmpeg backend (default from Qt 6.5); it decodes
    // the widest range of formats identically on macOS and Linux.
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND"))
        qputenv("QT_MEDIA_BACKEND", "ffmpeg");

    QApplication::setOrganizationName(QStringLiteral("Spotlight"));
    QApplication::setOrganizationDomain(QStringLiteral("spotlight.studio"));
    QApplication::setApplicationName(QStringLiteral("SPOTLIGHT"));
    QApplication::setApplicationDisplayName(QStringLiteral("SPOTLIGHT!"));
    QApplication::setApplicationVersion(QStringLiteral(SPOTLIGHT_VERSION));

    SpotlightApp app(argc, argv);
    // resources.qrc lives in the static spotlight_ui library; static-library
    // resources are only registered when initialised explicitly.
    Q_INIT_RESOURCE(resources);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/spotlight.png")));
    spotlight::theme::apply(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SPOTLIGHT! creative studio"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Project file to open (.spotlight)"));
    const QCommandLineOption selfTest(QStringLiteral("self-test"),
                                      QStringLiteral("Check this installation end to end and exit (0 = OK)."),
                                      QStringLiteral("output-dir"));
    parser.addOption(selfTest);
    parser.process(app);
    if (parser.isSet(selfTest))
        return spotlight::runSelfTest(parser.value(selfTest));

    spotlight::MainWindow window;
    app.window = &window;
    window.show();

    const QStringList args = parser.positionalArguments();
    const QString toOpen = !args.isEmpty() ? args.first() : app.pendingFile;
    QTimer::singleShot(0, &window, [&window, toOpen] {
        if (!toOpen.isEmpty())
            window.openProject(toOpen);
        else
            window.offerRecovery();
    });
    return app.exec();
}

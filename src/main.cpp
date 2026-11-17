#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

#include "ui/MainWindow.h"

#ifdef Q_OS_WIN
#include <memory>

#include "remote/RemoteApi.h"
#include "remote/RemoteWin.h"
#endif

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("dm1sv-mmtty"));
	QApplication::setOrganizationName(QStringLiteral("DM1SV"));
	QApplication::setApplicationVersion(QStringLiteral("2.0.0"));
	QApplication::setWindowIcon(QIcon(QStringLiteral(":/mmtty.png")));

	QStringList args = QApplication::arguments();
#ifdef Q_OS_WIN
	// Engine remote mode (-r -u -s -t -m ...): single-dash options of the original, handled before the CLI parser.
	const remote::StartOptions remoteOpt = remote::parseStartOptions(args.mid(1));
	args = QStringList{args.value(0)} + remoteOpt.rest;
	if( remoteOpt.active() && !remoteOpt.allowMultiple && RemoteWin::anotherInstanceRunning() ) return 0;
#endif

	QCommandLineParser cli;
	cli.setApplicationDescription(QStringLiteral("DM1SV-MMTTY - RTTY sound card modem"));
	cli.addHelpOption();
	cli.addVersionOption();
	QCommandLineOption optNull(QStringLiteral("no-audio"), QStringLiteral("Do not open any sound device (internal loopback only)."));
	QCommandLineOption optIni(QStringLiteral("ini"), QStringLiteral("Use this settings file."), QStringLiteral("file"));
	cli.addOption(optNull);
	cli.addOption(optIni);
	cli.process(args);

	MainWindow::Options opt;
	opt.nullAudio = cli.isSet(optNull);
	opt.iniPath = cli.value(optIni);
#ifdef Q_OS_WIN
	opt.remote = remoteOpt.active();
#endif

	MainWindow w(opt);
#ifdef Q_OS_WIN
	std::unique_ptr<RemoteWin> remoteHost;
	std::unique_ptr<RemoteApi> remoteApi;
	if( remoteOpt.active() ){
		remoteHost.reset(new RemoteWin(&w, remoteOpt.appHandle));
		remoteApi.reset(new RemoteApi(&w, remoteHost.get(), remoteOpt));
		remoteHost->attach(remoteApi.get());
		QApplication::setQuitOnLastWindowClosed(false);		// the window may be hidden while the settings dialog closes
		app.installNativeEventFilter(remoteHost.get());
		w.winId();										// the application needs the handle even while the window is hidden
		remoteApi->start();
		if( !remoteApi->startHidden() ) w.show();
	}
	else
#endif
	w.show();
	const int rc = app.exec();
#ifdef Q_OS_WIN
	if( remoteHost ) app.removeNativeEventFilter(remoteHost.get());
	remoteApi.reset();
#endif
	return rc;
}

// GUI smoke test: drives a real MainWindow (offscreen, null audio, free running) through the
// same code paths as the buttons and checks the transmit state machine, decode and persistence.
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include "MainWindow.h"

static int failures = 0;
#define CHECK(cond, msg) do { if( !(cond) ){ qWarning("FAIL: %s", msg); failures++; } else qInfo("PASS: %s", msg); } while(0)

static void pump(int ms)
{
	QElapsedTimer t; t.start();
	while( t.elapsed() < ms ) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

template<class F> static bool waitFor(F cond, int ms)
{
	QElapsedTimer t; t.start();
	while( !cond() && t.elapsed() < ms ) pump(10);
	return cond();
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QTemporaryDir dir;
	const QString ini = dir.filePath("t.ini");

	{
		MainWindow::Options opt;
		opt.nullAudio = true;
		opt.freeRun = true;
		opt.iniPath = ini;
		MainWindow w(opt);
		w.show();
		pump(200);
		struct Closer { MainWindow &w; ~Closer() { w.close(); } } closer{w};

		CHECK(!w.isTxDown(), "starts in receive");
		CHECK(w.settings().macro[0].endsWith('\\'), "default macro M1 ends with a return-to-RX marker");

		// ---- typed text: TX, text goes out through the loopback, TX button -> WAIT -> RX ----
		w.pressTx();
		CHECK(w.isTxDown(), "TX button engages transmit");
		w.typeText(QStringLiteral("CQ CQ DE JA7UDE\r\nTHE QUICK BROWN FOX 0123456789\r\n"));
		waitFor([&]{ return w.rxText().contains("THE QUICK BROWN FOX 0123456789"); }, 30000);
		CHECK(w.rxText().contains("CQ CQ DE JA7UDE"), "typed text is sent and decoded back");
		CHECK(w.rxText().contains("THE QUICK BROWN FOX 0123456789"), "full typed text decoded");
		w.pressTx();							// finish and return to receive
		const bool back = waitFor([&]{ return !w.isTxDown() && !w.engine()->isTx(); }, 30000);
		CHECK(back, "TX button returns to receive when the FIFO is empty");

		// ---- macro with trailing backslash returns to RX on its own ----
		w.pressMacro(3);						// "__\r\nQSL UR 599-599 TU\r\n_\"
		CHECK(w.isTxDown(), "macro starts transmit");
		const bool macroBack = waitFor([&]{ return !w.isTxDown() && !w.engine()->isTx(); }, 30000);
		CHECK(macroBack, "macro with trailing \\ returns to receive automatically");
		CHECK(w.rxText().contains("QSL UR 599-599 TU"), "macro text decoded through the loopback");

		// ---- repeat macro (M13 ends with '#') keeps sending until TX OFF ----
		const int before = w.rxText().count("RYRYRY");
		w.pressMacro(12);
		CHECK(w.isTxDown(), "repeat macro starts transmit");
		waitFor([&]{ return w.rxText().count("RYRYRY") >= before + 3; }, 30000);
		CHECK(w.rxText().count("RYRYRY") >= before + 3, "repeat macro keeps retransmitting");
		w.pressTxOff();
		const bool off = waitFor([&]{ return !w.isTxDown() && !w.engine()->isTx(); }, 30000);
		CHECK(off, "TX OFF aborts the transmission");
	}

	CHECK(QFile::exists(ini), "settings file written on close");
	{
		MainWindow::Options opt;
		opt.nullAudio = true;
		opt.freeRun = true;
		opt.iniPath = ini;
		MainWindow w(opt);
		CHECK(qAbs(w.settings().mark - 2125.0) < 0.5, "settings reloaded");
	}
	return failures;
}

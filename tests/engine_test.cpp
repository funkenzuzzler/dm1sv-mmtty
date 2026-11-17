// Engine test: drives the real Engine (worker thread, null audio device, free running) through
// a complete transmit -> internal loopback -> receive cycle and checks the decoded text.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <QDir>
#include <QTemporaryDir>

#include "Engine.h"
#include "Macro.h"

static int failures = 0;
#define CHECK(cond, msg) do { if( !(cond) ){ qWarning("FAIL: %s", msg); failures++; } else qInfo("PASS: %s", msg); } while(0)

static void pump(int ms)
{
	QElapsedTimer t; t.start();
	while( t.elapsed() < ms ) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);

	Engine::Config cfg;
	cfg.nullAudio = true;
	cfg.realtime = false;			// free-running: no sound card, runs as fast as the CPU allows
	Engine eng(cfg);

	QString rx;
	QObject::connect(&eng, &Engine::rxText, [&](const QString &t, bool){ rx += t; });
	int txChanges = 0;
	QObject::connect(&eng, &Engine::txStateChanged, [&](bool){ txChanges++; });

	eng.start();

	// ---- TX -> internal loopback -> RX ---------------------------------------
	eng.setTxEcho(1);
	const QByteArray text = "RYRYRY DE JA7UDE\r\nTHE QUICK BROWN FOX 0123456789\r\n";
	eng.requestTx(false);
	eng.putText(text);
	QElapsedTimer t; t.start();
	while( !(eng.isTx() && eng.txIdle() && eng.txBufferCount() == 0) && t.elapsed() < 20000 ) pump(10);
	CHECK(eng.isTx(), "engine switched to TX");
	eng.requestRx(false);
	t.restart();
	while( eng.isTx() && t.elapsed() < 20000 ) pump(10);
	CHECK(!eng.isTx(), "engine returned to RX");
	pump(200);
	qInfo().noquote() << "decoded:" << rx;
	CHECK(rx.contains("THE QUICK BROWN FOX 0123456789"), "loopback text decoded");
	CHECK(txChanges >= 2, "txStateChanged signalled");

	// ---- AFC: tune the receiver off frequency in test mode and let AFC pull it in ----------
	rx.clear();
	eng.setFftOptions(1, 2, 0);
	eng.setAfcParams(1, 32, 2.0, 1.0);
	eng.setAfc(true);
	eng.setRxFrequencies(2125, 2295);
	eng.setTest(true);			// modulator feeds the receiver with a test signal
	eng.putText(QByteArray("RY").repeated(1500));	// an idle modulator only sends mark: give it mark/space data
	pump(100);
	{	// detune only the demodulator; the test-signal modulator stays on 2125/2295
		QMutexLocker l(&eng.lock());
		eng.dem().SetMarkFreq(2125 + 40);
		eng.dem().SetSpaceFreq(2295 + 40);
	}
	bool moved = false;
	Engine::Spectrum sp;
	t.restart();
	while( t.elapsed() < 20000 && !moved ){
		pump(20);
		if( eng.fetchSpectrum(sp) ) moved |= eng.doAfc();
	}
	eng.setTest(false);
	double mark;
	{ QMutexLocker l(&eng.lock()); mark = eng.dem().GetMarkFreq(); }
	qInfo() << "AFC mark after pull-in:" << mark;
	CHECK(moved && qAbs(mark - 2125.0) < 40.0, "AFC pulled the receiver towards the signal");
	CHECK(sp.window > 0 && sp.bins.size() > 100, "spectrum frames are produced");

	// ---- WAV record + play ------------------------------------------------------
	QTemporaryDir dir;
	const QString wav = dir.filePath("t.wav");
	QString err;
	CHECK(eng.recordFile(wav, &err), "WAV record opens");
	eng.setTest(true);
	pump(600);
	eng.setTest(false);
	eng.stopFile();
	CHECK(QFileInfo(wav).size() > 1000, "WAV file written");
	bool finished = false;
	QObject::connect(&eng, &Engine::fileFinished, [&]{ finished = true; });
	CHECK(eng.playFile(wav, &err), "WAV play opens");
	t.restart();
	while( !finished && t.elapsed() < 20000 ) pump(10);
	CHECK(finished, "WAV playback finishes");

	eng.stop();
	return failures;
}

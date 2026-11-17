// Software FSK test.
//  1. FskKeyer: timing accuracy of the edge thread and abort behaviour.
//  2. Engine: transmit text with FSK keying (test tap instead of a serial port), capture the
//     line transitions with their timestamps and decode them as an asynchronous 5N1.5 Baudot
//     stream.  This checks both the bit timing and the framing the radio would see.
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <vector>

#include "Engine.h"
#include "FskKeyer.h"

static int failures = 0;
#define CHECK(cond, msg) do { if( !(cond) ){ qWarning("FAIL: %s", msg); failures++; } else qInfo("PASS: %s", msg); } while(0)

static void pump(int ms)
{
	QElapsedTimer t; t.start();
	while( t.elapsed() < ms ) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

struct Tap { bool mark; qint64 ns; };

// Decodes edges as 5 data bits (MSB first, as the modulator sends them), 1 start, >= 1 stop bit.
static QByteArray decodeEdges(const std::vector<Tap> &e, double baud, int *framingErrors)
{
	QByteArray codes;
	*framingErrors = 0;
	const double bit = 1e9 / baud;
	auto levelAt = [&](double t) {
		bool lvl = true;							// idle = mark
		for( const Tap &x : e ){ if( double(x.ns) <= t ) lvl = x.mark; else break; }
		return lvl;
	};
	size_t i = 0;
	while( i < e.size() ){
		if( !(e[i].mark == false) ){ i++; continue; }		// a falling edge starts a character
		const double t0 = double(e[i].ns);
		int code = 0;
		for( int b = 0; b < 5; b++ ) code = (code << 1) | (levelAt(t0 + bit * (1.5 + b)) ? 1 : 0);
		if( !levelAt(t0 + bit * 6.25) ) (*framingErrors)++;			// stop bit must be mark
		codes.append(char(code));
		const double end = t0 + bit * 6.0;
		while( i < e.size() && double(e[i].ns) < end ) i++;
	}
	return codes;
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);

	// ------------------------------------------------------------------ keyer timing
	{
		FskKeyer k;
		QMutex mx;
		std::vector<Tap> got;
		k.start([&](bool m){ QMutexLocker l(&mx); got.push_back({m, FskKeyer::nowNs()}); });
		const int64_t t0 = FskKeyer::nowNs() + 50000000;
		std::vector<FskKeyer::Edge> edges;
		for( int i = 0; i < 400; i++ ) edges.push_back({t0 + int64_t(i) * 2200000, (i & 1) == 0});	// 2.2 ms apart
		k.push(edges);
		QElapsedTimer t; t.start();
		while( t.elapsed() < 2500 && k.firedCount() < 400 ) pump(10);
		CHECK(k.firedCount() == 400, "keyer delivers all edges");
		double maxErr = 0, sum = 0;
		{
			QMutexLocker l(&mx);
			for( size_t i = 0; i < got.size(); i++ ){
				const double err = double(got[i].ns - (t0 + int64_t(i) * 2200000)) / 1e3;	// us
				sum += err;
				maxErr = std::max(maxErr, std::fabs(err));
			}
		}
		qInfo("keyer: mean lateness %.1f us, worst %.1f us (no real-time priority)", sum / 400.0, maxErr);
		CHECK(maxErr < 1500.0, "keyer worst-case error below 1.5 ms (ordinary scheduling)");
		CHECK(sum / 400.0 < 200.0, "keyer mean error below 0.2 ms");
		k.stop();
	}
	{
		FskKeyer k;
		std::atomic<int> n{0};
		std::atomic<bool> last{false};
		k.start([&](bool m){ last = m; n++; });
		const int64_t t0 = FskKeyer::nowNs() + 300000000;
		k.push({{t0, false}, {t0 + 1000000, true}});
		pump(50);
		k.abort(true);
		pump(500);
		CHECK(n.load() == 1 && last.load(), "abort drops pending edges and drives the line to mark");
	}

	// ------------------------------------------------------------------ engine end-to-end
	Engine::Config cfg;
	cfg.nullAudio = true;
	cfg.realtime = true;
	QMutex mx;
	std::vector<Tap> taps;
	cfg.fskTap = [&](bool m, qint64 ns){ QMutexLocker l(&mx); taps.push_back({m, ns}); };
	Engine eng(cfg);
	eng.start();
	CHECK(eng.fskActive(), "FSK keying active with a tap configured");
	eng.setBaudRate(45.45);
	eng.setTxEcho(1);

	const QByteArray text = "RYRYRY DE JA7UDE 0123456789\r\n";
	QElapsedTimer t; t.start();
	eng.requestTx(false);
	eng.putText(text);
	while( !(eng.isTx() && eng.txIdle() && eng.txBufferCount() == 0) && t.elapsed() < 20000 ) pump(10);
	eng.requestRx(false);
	t.restart();
	while( eng.isTx() && t.elapsed() < 20000 ) pump(10);
	CHECK(!eng.isTx(), "FSK transmission ends and returns to RX");
	pump(100);

	std::vector<Tap> e;
	{ QMutexLocker l(&mx); e = taps; }
	CHECK(e.size() > 40, "line transitions were produced");
	CHECK(!e.empty() && e.back().mark, "line idles at mark after transmit");

	// Inside a character every edge must sit on the bit grid of its start edge; consecutive
	// characters must follow each other at 5 data + 1 start + 1.5 stop = 7.5 bit times.
	const double bit = 1e9 / 45.45;
	double worst = 0, minSpacing = 1e18;
	int chars = 0;
	for( size_t i = 0; i < e.size(); ){
		if( e[i].mark ){ i++; continue; }
		const double t0 = double(e[i].ns);
		size_t j = i + 1;
		for( ; j < e.size() && double(e[j].ns) - t0 < bit * 5.9; j++ ){
			const double q = (double(e[j].ns) - t0) / bit;
			worst = std::max(worst, std::fabs(q - std::round(q)) * bit);
		}
		// next start edge
		for( ; j < e.size() && e[j].mark; j++ ) {}
		if( j < e.size() ){
			const double sp = double(e[j].ns) - t0;
			if( sp < bit * 8.2 ) minSpacing = std::min(minSpacing, sp);
		}
		chars++;
		i = j;
	}
	qInfo("characters: %d, worst in-character deviation %.3f ms, tightest character spacing %.3f bit times",
	      chars, worst / 1e6, minSpacing / bit);
	CHECK(chars >= 25, "all characters were keyed");
	CHECK(worst < 1.5e6, "bit edges within 1.5 ms of the ideal 22 ms grid");
	CHECK(std::fabs(minSpacing / bit - 7.5) < 0.1, "character spacing is 7.5 bit times (5N1.5)");

	int ferr = 0;
	const QByteArray codes = decodeEdges(e, 45.45, &ferr);
	CHECK(ferr == 0, "no framing errors in the keyed bit stream");
	QString decoded;
	{
		QMutexLocker l(&eng.lock());
		eng.rtty().ClearRX();
		for( char c : codes ){
			const char a = eng.rtty().ConvAscii(uchar(c));
			if( a ) decoded += QChar(uchar(a));
		}
	}
	qInfo().noquote() << "decoded from the FSK line:" << decoded;
	CHECK(decoded.contains("RYRYRY DE JA7UDE 0123456789"), "text decoded from the keyed line");

	eng.stop();
	return failures;
}

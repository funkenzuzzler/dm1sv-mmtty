// Tests of the engine remote mode API: the portable protocol code (constants, shared memory layout, bit
// packing, command line) and the RemoteApi logic, driven through a fake host against a real MainWindow
// (offscreen, null audio).  The Windows transport itself (window messages, file mapping) is not covered.
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <cstring>
#include <memory>
#include <vector>

#include "MainWindow.h"
#include "RemoteApi.h"

using namespace remote;

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

struct FakeHost : RemoteHost {
	struct Msg { unsigned m; qint64 l; };
	std::vector<Msg> sent;
	quintptr app = 0;
	std::unique_ptr<ComArray> mem;
	bool appEnabled = true;
	int activated = 0;

	void postToApp(unsigned m, qint64 l) override { if( app ) sent.push_back({m, l}); }
	void postRaw(unsigned m, qint64 l) override { sent.push_back({m, l}); }
	bool appKnown() const override { return app != 0; }
	void setApp(quintptr h) override { app = h; }
	quintptr windowHandle() const override { return 0x1234; }
	unsigned threadId() const override { return 77; }
	ComArray *openShared(const QString &) override { return mem.get(); }
	void activateApp() override { activated++; }
	void enableApp(bool e) override { appEnabled = e; }

	int count(unsigned m) const { int n = 0; for( const Msg &x : sent ) if( x.m == m ) n++; return n; }
	bool last(unsigned m, qint64 &l) const { for( auto it = sent.rbegin(); it != sent.rend(); ++it ) if( it->m == m ){ l = it->l; return true; } return false; }
	qint64 lastOr(unsigned m, qint64 def) const { qint64 l; return last(m, l) ? l : def; }
};

static void testProtocol()
{
	CHECK(RXM_HANDLE == 0 && RXM_EXIT == 2 && RXM_SETSWITCH == 11 && RXM_SETVIEW == 14 && RXM_SUSPEND == 22
	      && RXM_PROFILE == 24 && RXM_TIMER == 25 && RXM_PTTFSK == 30 && RXM_SOUNDSOURCE == 31, "RXM_ numbers");
	CHECK(TXM_HANDLE == 0x8000 && TXM_START == 0x8002 && TXM_CHAR == 0x8003 && TXM_LEVEL == 0x800b
	      && TXM_THREAD == 0x800f && TXM_SHOWPROFILE == 0x8015, "TXM_ numbers");
	CHECK(sizeof(ComArray) == 13524, "shared memory size");
	CHECK(offsetof(ComArray, smpFFT) == 148 && offsetof(ComArray, flagFFT) == 152, "shared memory offsets");

	SwitchState s;
	s.demType = 2; s.afc = true; s.sq = true; s.fixShift = 2; s.twoNotch = true; s.viaTx = true; s.sendWay = 2; s.dualPeak = true;
	const uint32_t d = encodeSwitch(s);
	CHECK(d == (2u | 4u | 0x80u | (2u << 10) | 0x4000u | 0x8000u | (2u << 17) | 0x80000u), "switch word bits");
	const SwitchState r = decodeSwitch(d);
	CHECK(r.demType == 2 && r.afc && r.sq && !r.net && r.fixShift == 2 && r.twoNotch && r.viaTx && r.sendWay == 2 && r.dualPeak,
	      "switch word round trip");

	ViewState v;
	v.fftRange = 5; v.fftGain = 6; v.fftResp = 3; v.xySize = 1; v.xyQuality = 2; v.xyOn = true;
	const uint32_t vd = encodeView(v);
	const ViewState vr = decodeView(vd);
	CHECK(vr.fftRange == 5 && vr.fftGain == 6 && vr.fftResp == 3 && vr.xySize == 1 && vr.xyQuality == 2 && vr.xyOn,
	      "view word round trip (gain 6 uses the upper bits)");
	CHECK((vd & 0x6000) == 0x2000, "view word: gain bit 2 sits in b13");

	const StartOptions o = parseStartOptions({"-r", "-d", "-p", "-n", "-a", "-h1A2b", "-CFOO", "-T5", "-Z", "--no-audio"});
	CHECK(o.active() && (o.flags & REM_MENU) && (o.flags & REM_CTRL) && o.clientWidth == 640, "-r");
	CHECK(o.frameless && (o.flags & REM_NOMOVE) && (o.flags & REM_NOPTT) && (o.flags & REM_NOCNV) && (o.flags & REM_DISSTAYONTOP),
	      "-d -p -n -a");
	CHECK(o.appHandle == 0x1a2b && o.sharedName == "FOO" && o.timeoutTicks == 50 && o.allowMultiple, "-h -C -T -Z");
	CHECK(o.rest == QStringList{"--no-audio"}, "other arguments are passed on");
	const StartOptions none = parseStartOptions({"--ini", "x.ini"});
	CHECK(!none.active() && none.rest.size() == 2, "no remote option: normal start");
	const StartOptions late = parseStartOptions({"-p", "-t"});
	CHECK(late.active() && !(late.flags & REM_NOPTT), "-p before a mode option is ignored, as in the original");

	ComArray m;
	memset(&m, 0, sizeof(m));
	std::vector<int> fft(2048);
	for( int i = 0; i < 2048; i++ ) fft[i] = i * 10;
	exportFft(m, fft.data(), 2048, 11025.0, 0, 1);
	CHECK(m.flagFFT == 1 && m.arrayFFT[100] == 1000 && m.smpFreq == 11025, "FFT export: plain copy at 11025 Hz");
	memset(&m, 0, sizeof(m));
	exportFft(m, fft.data(), 2048, 11025.0, 0, 4);
	CHECK(m.arrayFFT[100] == 100, "FFT export: automatic gain scales by 0.1");
	memset(&m, 0, sizeof(m));
	m.smpFFT = 1;
	exportFft(m, fft.data(), 2048, 11025.0, 0, 1);
	CHECK(m.smpFreq == 8000 && m.arrayFFT[800] == fft[int(800 * 8000.0 / 11025.0 + 0.5)], "FFT export: resampled to the 8000 Hz base");

	std::vector<double> x(512, 100.0), y(512, -50.0);
	exportXY(m, x.data(), y.data(), false);
	CHECK(m.flagXY == 1 && m.arrayX[0] == 800 && m.arrayY[0] == -400, "XY export scales by 16384/2048 without the limiter");
	exportXY(m, x.data(), y.data(), true);
	CHECK(m.arrayX[0] == 100 && m.arrayY[0] == -50, "XY export is raw with the limiter");
	CHECK(clockErrorPpm(0, 11025.0) == 0 && clockErrorPpm(1, 8000.8) == 100, "clock error in ppm");

	char buf[8];
	writeString(buf, sizeof(buf), QStringLiteral("0123456789"));
	CHECK(readString(buf, sizeof(buf)) == "0123456" && buf[7] == 0, "strings are cut and terminated");
}

static void testApi(const QString &ini)
{
	MainWindow::Options opt;
	opt.nullAudio = true;
	opt.freeRun = true;
	opt.iniPath = ini;
	opt.remote = true;
	MainWindow w(opt);

	FakeHost host;
	host.mem.reset(new ComArray);
	memset(host.mem.get(), 0, sizeof(ComArray));
	strcpy(host.mem->title, "Test Panel");
	strcpy(host.mem->comName, "NONE");

	StartOptions so = parseStartOptions({"-r", "-n"});
	so.timeoutTicks = 100;
	// -n would feed raw codes; the first part of the test uses the normal ASCII mode
	so.flags &= ~unsigned(REM_NOCNV);
	RemoteApi api(&w, &host, so);
	w.show();
	api.start();

	CHECK(host.count(TXM_THREAD) == 1 && host.count(TXM_HANDLE) == 1 && host.count(TXM_START) == 1, "start-up posts thread, handle and start");
	CHECK(host.lastOr(TXM_HANDLE, 0) == 0x1234 && host.lastOr(TXM_THREAD, 0) == 77, "start-up values");
	CHECK(w.windowTitle() == "Test Panel", "title taken from the shared memory");
	CHECK(readString(host.mem->verMMTTY, 16) == "Ver1.70", "version written to the shared memory");
	CHECK(host.mem->smpDemFreq > 0, "demodulator sampling frequency written");

	// ---- no application window known yet: PostApp posts nothing ----
	api.handle(RXM_SETBAUD, 5000);
	CHECK(host.count(TXM_BAUD) == 0, "nothing is posted to an unknown application window");
	CHECK(qAbs(w.settings().baud - 50.0) < 1e-6, "RXM_SETBAUD applied (value / 100)");

	// ---- handshake ----
	host.sent.clear();
	api.handle(RXM_HANDLE, 0x4321);
	CHECK(host.app == 0x4321 && host.activated == 1, "RXM_HANDLE stores the application window");
	CHECK(host.count(TXM_BAUD) == 1 && host.lastOr(TXM_BAUD, 0) == 5000, "full state is sent after RXM_HANDLE: baud");
	CHECK(host.count(TXM_MARK) == 1 && host.count(TXM_SPACE) == 1 && host.count(TXM_SWITCH) == 1 && host.count(TXM_VIEW) == 1
	      && host.count(TXM_RESO) == 1 && host.count(TXM_LPF) == 1 && host.count(TXM_HEIGHT) == 1, "full state is sent after RXM_HANDLE");
	CHECK(host.count(TXM_PTTEVENT) == 1 && host.lastOr(TXM_PTTEVENT, -1) == 0, "PTT state is sent after RXM_HANDLE");

	// ---- only changes are posted ----
	host.sent.clear();
	api.handle(RXM_SETBAUD, 5000);
	CHECK(host.count(TXM_BAUD) == 0, "unchanged values are not posted again");
	api.handle(RXM_SETBAUD, 4545);
	CHECK(host.lastOr(TXM_BAUD, 0) == 4545, "changed baud rate is posted (x100)");
	api.handle(RXM_SETBAUD, 5);
	CHECK(qAbs(w.settings().baud - 45.45) < 1e-6, "out of range baud rate is ignored");

	// ---- frequencies ----
	host.sent.clear();
	api.handle(RXM_SETMARK, 2000);
	CHECK(host.lastOr(TXM_MARK, 0) == 2000 && host.count(TXM_SPACE) == 0 && qAbs(w.settings().mark - 2000) < 1e-6,
	      "RXM_SETMARK changes only the mark frequency");
	api.handle(RXM_SETSPACE, 2400);
	CHECK(host.lastOr(TXM_SPACE, 0) == 2400 && qAbs(w.settings().shift - 400) < 1e-6, "RXM_SETSPACE");
	api.handle(RXM_SETMARK, 50);
	CHECK(qAbs(w.settings().mark - 2000) < 1e-6, "out of range mark is ignored");
	api.handle(RXM_SETHAM, 0);
	CHECK(host.lastOr(TXM_MARK, 0) == 2125 && host.lastOr(TXM_SPACE, 0) == 2295 && qAbs(w.settings().baud - 45.45) < 1e-6,
	      "RXM_SETHAM: default frequencies, 45.45 baud");

	// ---- switch word ----
	host.sent.clear();
	SwitchState sw = decodeSwitch(uint32_t(host.lastOr(TXM_SWITCH, 0)));
	sw.demType = 0; sw.atc = true; sw.rev = true; sw.sq = true; sw.afc = false;
	api.handle(RXM_SETSWITCH, encodeSwitch(sw));
	const SwitchState back = decodeSwitch(uint32_t(host.lastOr(TXM_SWITCH, 0)));
	CHECK(back.demType == 0 && back.atc && back.rev && back.sq && !back.afc, "RXM_SETSWITCH is applied and reported back");
	CHECK(w.settings().atc && w.settings().reverse && w.settings().squelch && !w.settings().afc && w.settings().demType == 0,
	      "RXM_SETSWITCH reaches the settings");

	// ---- view word ----
	ViewState v = decodeView(uint32_t(host.lastOr(TXM_VIEW, 0)));
	v.fftGain = 6; v.fftResp = 2; v.fftRange = 3; v.xyOn = false;
	api.handle(RXM_SETVIEW, encodeView(v));
	CHECK(w.settings().fftGain == 6 && w.settings().fftResp == 2 && w.settings().fftRange == 3 && !w.settings().showXY, "RXM_SETVIEW");

	// ---- filters ----
	api.handle(RXM_SETRESO, (64u << 16) | 120u);
	api.handle(RXM_SETLPF, (80u << 16) | 90u);
	api.handle(RXM_REQPARA, 0);
	CHECK(host.lastOr(TXM_RESO, 0) == ((64 << 16) | 120), "RXM_SETRESO");
	CHECK(host.lastOr(TXM_LPF, 0) == ((80 << 16) | 90), "RXM_SETLPF");
	api.handle(RXM_NOTCH, 1500);
	CHECK((host.lastOr(TXM_NOTCH, 0) & 0xffff) == 1500, "RXM_NOTCH");
	api.handle(RXM_SETLENGTH, 7);
	CHECK(w.settings().dataBits == 7, "RXM_SETLENGTH");
	api.handle(RXM_SETLENGTH, 5);

	// ---- squelch, TX delay, PTT timer ----
	api.handle(RXM_SETSQLVL, 5000);
	CHECK(w.settings().squelchLevel < 1024, "RXM_SETSQLVL is limited");
	api.handle(RXM_SETTXDELAY, 20);		// (applied in toTx; the transmissions below run with it)

	// ---- transmit: characters go out and are decoded again ----
	host.sent.clear();
	api.handle(RXM_PTT, 2);
	CHECK(w.isTxDown() && host.lastOr(TXM_PTTEVENT, -1) == 1, "RXM_PTT 2 starts transmit and reports it");
	const QString text = "CQ TEST";
	for( QChar c : text ) api.handle(RXM_CHAR, c.unicode());
	CHECK(waitFor([&] { return w.rxText().contains("CQ TEST"); }, 30000), "RXM_CHAR text is transmitted (loopback decode)");
	const bool gotChars = waitFor([&] { return host.count(TXM_CHAR) >= 7; }, 5000);
	CHECK(gotChars, "decoded characters are posted as TXM_CHAR");
	api.handle(RXM_PTT, 1);
	CHECK(waitFor([&] { return !w.isTxDown() && !w.engine()->isTx(); }, 30000), "RXM_PTT 1 returns to receive");
	CHECK(waitFor([&] { return host.lastOr(TXM_PTTEVENT, -1) == 0; }, 5000), "receive is reported");
	CHECK(host.count(TXM_FIGEVENT) >= 1, "FIG / LTR state is reported with the PTT events");

	// ---- key buffer mode ----
	SwitchState kb = decodeSwitch(uint32_t(host.lastOr(TXM_SWITCH, 0)));
	kb.viaTx = true;
	api.handle(RXM_SETSWITCH, encodeSwitch(kb));
	CHECK((api.flags() & REM_VIATX) != 0, "key buffer mode switched on");
	api.handle(RXM_PTT, 2);
	for( QChar c : QStringLiteral("ab") ) api.handle(RXM_CHAR, c.unicode());
	api.handle(RXM_CHAR, '|');	// not a Baudot character: dropped
	CHECK(waitFor([&] { return w.rxText().contains("AB"); }, 30000), "key buffer: characters are upper-cased and sent");
	CHECK(!w.rxText().contains("|"), "key buffer: characters without a Baudot code are dropped");
	api.handle(RXM_PTT, 4);
	api.handle(RXM_PTT, 0);
	CHECK(waitFor([&] { return !w.isTxDown() && !w.engine()->isTx(); }, 30000), "RXM_PTT 0 aborts the transmission");
	kb.viaTx = false;
	api.handle(RXM_SETSWITCH, encodeSwitch(kb));

	// ---- shared memory ----
	host.mem->flagFFT = 0;
	host.mem->flagXY = 0;
	api.handle(RXM_SETVIEW, encodeView(ViewState{0, 1, 2, 0, 0, true}));
	const bool fftDone = waitFor([&] { return host.mem->flagFFT == 1; }, 5000);
	CHECK(fftDone, "FFT data appears in the shared memory");
	const bool xyDone = waitFor([&] { return host.mem->flagXY == 1; }, 5000);
	CHECK(xyDone, "XY data appears in the shared memory");
	host.mem->flagFFT = 0;
	api.handle(RXM_ENBSHARED, 0);
	pump(300);
	CHECK(host.mem->flagFFT == 0, "RXM_ENBSHARED 0 stops the updates");
	api.handle(RXM_ENBSHARED, 1);

	// ---- setup dialog is not opened here (modal); suspend / resume ----
	api.handle(RXM_SUSPEND, 1);
	CHECK(!w.engine()->running() && !w.isVisible(), "RXM_SUSPEND 1 stops the engine and hides the window");
	api.handle(RXM_SUSPEND, 0);
	CHECK(w.engine()->running() && w.isVisible(), "RXM_SUSPEND 0 restarts");

	// ---- profiles ----
	strcpy(host.mem->ProfileName[3], "My profile");
	host.sent.clear();
	api.handle(RXM_PROFILE, 0x00010000 | 3);
	CHECK(host.lastOr(TXM_PROFILE, -1) == (0x00010000 | 3), "profile stored");
	api.handle(RXM_SETBAUD, 10000);
	api.handle(RXM_PROFILE, 3);
	CHECK(qAbs(w.settings().baud - 45.45) < 1e-6 && host.lastOr(TXM_PROFILE, -1) == 3, "profile recalled");
	api.handle(RXM_PROFILE, 0x00020000 | 3);
	CHECK(host.lastOr(TXM_PROFILE, -1) == (0x00020000 | 3), "profile deleted");

	// ---- time-out and exit ----
	host.app = 0;
	api.start();
	CHECK(host.count(TXM_START) >= 1, "start posts again");
	api.handle(RXM_HANDLE, 0x4321);
	CHECK(true, "handshake clears the time-out");
	api.handle(RXM_EXIT, 0);
	pump(50);
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	testProtocol();
	QTemporaryDir dir;
	testApi(dir.filePath("remote.ini"));
	return failures;
}

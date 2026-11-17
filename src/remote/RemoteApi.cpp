#include "RemoteApi.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QAction>
#include <algorithm>
#include <cmath>

#include "MainWindow.h"
#include "compat.h"

using namespace remote;

namespace {
constexpr const char *kVersion = "Ver1.70";			// VERID of the original, shown to the application
}

RemoteApi::RemoteApi(MainWindow *w, RemoteHost *host, const StartOptions &opt, QObject *parent)
	: QObject(parent), m_w(w), m_host(host), m_opt(opt), m_flags(opt.flags)
{
	m_w->m_portsLocked = (m_flags & REM_NOPTT) != 0;
	if( m_flags & REM_NOCNV ) m_w->m_engine->setRawRx(true);
	if( m_w->m_s.fskLine ) m_fskLine = m_w->m_s.fskLine;

	// window: control panel look, no minimize / maximize / close buttons, stays on top unless -a
	Qt::WindowFlags f = Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint;
	if( !(m_flags & REM_DISSTAYONTOP) ) f |= Qt::WindowStaysOnTopHint;
	if( opt.frameless ) f |= Qt::FramelessWindowHint;
	m_w->setWindowFlags(f);
	m_w->setWindowTitle(QStringLiteral("RTTY Control panel"));
	m_w->applyRemoteLayout((m_flags & REM_CTRL) != 0, (m_flags & REM_MENU) != 0);
	if( opt.clientWidth > 0 && !m_w->m_ini->contains(m_w->windowKey() + QStringLiteral("/geometry")) )
		m_w->resize(opt.clientWidth, opt.clientHeight);
	if( opt.fixedBorder ) m_w->setFixedSize(m_w->size());

	MainWindow *mw = m_w;
	connect(mw, &MainWindow::spectrumUpdated, this, &RemoteApi::onSpectrum);
	connect(mw, &MainWindow::xyUpdated, this, &RemoteApi::onXY);
	connect(mw, &MainWindow::levelUpdated, this, &RemoteApi::onLevel);
	connect(mw, &MainWindow::pttEvent, this, &RemoteApi::onPtt);
	connect(mw, &MainWindow::settingsOpen, this, &RemoteApi::onSettingsOpen);
	connect(mw, &MainWindow::closing, this, &RemoteApi::onClosing);
	Engine *e = mw->m_engine.get();
	connect(e, &Engine::rxText, this, &RemoteApi::onRxText);
	connect(e, &Engine::rawChar, this, &RemoteApi::onRawChar);
	connect(e, &Engine::figChanged, this, &RemoteApi::onFig);
	connect(e, &Engine::tuningChanged, this, [this] { freq(); });

	m_timer.setInterval(100);
	connect(&m_timer, &QTimer::timeout, this, &RemoteApi::tick);
}

bool RemoteApi::startHidden() const
{
	return (m_flags & (REM_SHOWOFF | REM_NOMOVE)) != 0;
}

// ===========================================================================
// start-up handshake
// ===========================================================================
void RemoteApi::start()
{
	m_timeout = m_opt.timeoutTicks;
	firstFileMapping();
	m_host->postRaw(TXM_THREAD, m_host->threadId());
	m_host->postRaw(TXM_HANDLE, qint64(m_host->windowHandle()));
	m_host->postRaw(TXM_START, 0);
	m_timer.start();
}

void RemoteApi::tick()
{
	if( !m_timeout ) return;
	m_timeout--;
	if( !m_timeout ){
		quitApp();
	}
	else if( !(m_timeout % 5) ){
		firstFileMapping();
		m_host->postRaw(TXM_THREAD, m_host->threadId());
		m_host->postRaw(TXM_HANDLE, qint64(m_host->windowHandle()));
	}
}

void RemoteApi::quitApp()
{
	MainWindow *w = m_w;
	QMetaObject::invokeMethod(qApp, [w] {
		w->close();
		QCoreApplication::quit();
	}, Qt::QueuedConnection);
}

void RemoteApi::post(unsigned message, qint64 lParam)
{
	m_host->postToApp(message, lParam);
}

void RemoteApi::onClosing()
{
	m_timer.stop();
	QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
}

// ===========================================================================
// shared memory
// ===========================================================================
void RemoteApi::firstFileMapping()
{
	if( m_shared ) return;
	m_shared = m_host->openShared(m_opt.sharedName);
	if( !m_shared ) return;
	m_shared->smpFreq = m_shared->smpFFT ? 8000 : int32_t(SampFreq);
	updateComarray();
	m_shared->flagFFT = 0;
	m_shared->flagXY = 0;
	writeString(m_shared->verMMTTY, sizeof(m_shared->verMMTTY), QString::fromLatin1(kVersion));
	m_shared->errorClock = clockErrorPpm(SampType, SampFreq);
	m_shared->smpDemFreq = int32_t(DemSamp + 0.5);
}

// Port names are exchanged both ways: a name written by the application is used, an empty field is filled in.
void RemoteApi::updateComarray()
{
	if( !m_shared ) return;
	const QString title = readString(m_shared->title, sizeof(m_shared->title));
	if( !title.isEmpty() ) m_w->setWindowTitle(title);

	AppSettings &s = m_w->m_s;
	const QString com = readString(m_shared->comName, sizeof(m_shared->comName));
	if( !com.isEmpty() ){
		s.pttPort = com.compare(QLatin1String("NONE"), Qt::CaseInsensitive) == 0 ? QString() : com;
	}
	else {
		writeString(m_shared->comName, sizeof(m_shared->comName), s.pttPort.isEmpty() ? QStringLiteral("NONE") : s.pttPort);
	}
	// A separate radio-command port does not exist in this port: the field is only kept intact.
	m_w->applyEngineConfig();
}

void RemoteApi::onSpectrum(const Engine::Spectrum &sp)
{
	if( sp.lostSound ) m_lostHold = 25;
	if( sp.overflow ) m_overHold = 25;
	if( !m_shared || (m_flags & REM_DISSHARED) ) return;
	if( m_shared->flagFFT == 0 && int(sp.raw.size()) >= kFftPoints )
		exportFft(*m_shared, sp.raw.data(), FFT_SIZE, SampFreq, SampType, sp.fftGain);
}

void RemoteApi::onXY(const std::vector<double> &x, const std::vector<double> &y, bool limiter)
{
	if( !m_shared || (m_flags & REM_DISSHARED) ) return;
	if( m_shared->flagXY == 0 && int(x.size()) >= kXyPoints && int(y.size()) >= kXyPoints )
		exportXY(*m_shared, x.data(), y.data(), limiter);
}

void RemoteApi::onLevel(const Engine::Level &lv)
{
	if( m_shared && !(m_flags & REM_DISSHARED) ){
		m_shared->flagLostSound = m_lostHold > 0;
		m_shared->flagOverflow = (!m_w->m_txDown && m_overHold > 0) ? 1 : 0;
		m_shared->txBuffCount = (m_flags & REM_VIATX) ? m_w->fifoLen() : m_w->m_engine->txBufferCount();
	}
	if( m_lostHold > 0 ) m_lostHold--;
	if( m_overHold > 0 ) m_overHold--;
	post(TXM_LEVEL, (quint32(lv.sqLevel) << 16) + quint32(lv.avg));
}

// ===========================================================================
// notifications MMTTY -> application
// ===========================================================================
void RemoteApi::onPtt(bool tx)
{
	const int fig = m_w->m_engine->rtty().m_fig;
	if( tx ){
		post(TXM_PTTEVENT, 1);
		post(TXM_FIGEVENT, fig);
	}
	else {
		post(TXM_FIGEVENT, fig);
		if( m_shared && echoMode() != 2 && !(m_flags & REM_DISSHARED) ){
			memset(m_shared->arrayFFT, 0, sizeof(m_shared->arrayFFT));
			memset(m_shared->arrayX, 0, sizeof(m_shared->arrayX));
			memset(m_shared->arrayY, 0, sizeof(m_shared->arrayY));
		}
		post(TXM_PTTEVENT, 0);
	}
}

int RemoteApi::echoMode() const
{
	return m_w->m_s.echo;
}

void RemoteApi::onRxText(const QString &text, bool)
{
	if( m_flags & REM_NOCNV ) return;			// the raw codes are reported instead
	for( QChar c : text ) post(TXM_CHAR, uchar(c.toLatin1()));
}

void RemoteApi::onRawChar(int code)
{
	post(TXM_CHAR, code & 0xff);
}

void RemoteApi::onFig(bool fig)
{
	post(TXM_FIGEVENT, fig ? 1 : 0);
}

void RemoteApi::onSettingsOpen(bool open)
{
	post(TXM_SHOWSETUP, open ? 1 : 0);
}

// ===========================================================================
// state notification (RemoteStat / RemoteFreq): only changes are posted
// ===========================================================================
void RemoteApi::initStat()
{
	m_cSwitch = m_cView = m_cBaud = m_cHeight = m_cReso = m_cLpf = -1;
	m_cMark = m_cSpace = m_cNotch = m_cDefShift = -1;
}

void RemoteApi::freq()
{
	Engine &e = *m_w->m_engine;
	int mark, space;
	{
		QMutexLocker l(&e.lock());
		if( m_w->m_txDown && m_w->m_s.echo != 2 ){
			mark = int(e.mod().GetMarkFreq());
			space = int(e.mod().GetSpaceFreq());
		}
		else {
			mark = int(e.dem().GetMarkFreq());
			space = int(e.dem().GetSpaceFreq());
		}
	}
	if( m_cMark != mark ){
		post(TXM_MARK, mark);
		m_cMark = mark;
	}
	if( m_cSpace != space ){
		post(TXM_SPACE, space);
		m_cSpace = space;
	}
	qint64 notch;
	{
		QMutexLocker l(&e.lock());
		notch = qint64(quint32(e.lms().m_lmsNotch) + (quint32(e.lms().m_lmsNotch2) << 16));
	}
	if( notch != m_cNotch ){
		post(TXM_NOTCH, notch);
		m_cNotch = notch;
	}
	const int defShift = int(m_w->m_s.defShift);
	if( defShift != m_cDefShift ){
		post(TXM_DEFSHIFT, defShift);
		m_cDefShift = defShift;
	}
}

uint32_t RemoteApi::switchWord() const
{
	Engine &e = *m_w->m_engine;
	const AppSettings &s = m_w->m_s;
	QMutexLocker l(&e.lock());
	SwitchState sw;
	sw.demType = s.demType;
	sw.afc = s.afc;
	sw.net = s.txNet;
	sw.atc = s.atc;
	sw.bpf = s.bpf;
	sw.notch = s.notch;
	sw.sq = s.squelch;
	sw.rev = s.reverse;
	sw.uos = s.uos;
	sw.fixShift = s.afcMode;
	sw.lpfIir = e.dem().m_lpf != 0;
	sw.lmsNotch = e.lms().m_Type != 0;
	sw.twoNotch = e.lms().m_twoNotch != 0;
	sw.viaTx = (m_flags & REM_VIATX) != 0;
	sw.wordWrap = m_wordWrap;
	sw.sendWay = m_w->m_sendWay;
	sw.dualPeak = e.dem().m_AA6YQ.m_fEnabled != 0;
	return encodeSwitch(sw);
}

uint32_t RemoteApi::viewWord() const
{
	const AppSettings &s = m_w->m_s;
	ViewState v;
	v.fftRange = s.fftRange;
	v.fftGain = s.fftGain;
	v.fftResp = s.fftResp;
	v.xySize = m_xySize;
	v.xyQuality = m_xyQuality;
	v.xyOn = s.showXY;
	return encodeView(v);
}

void RemoteApi::stat()
{
	const qint64 size = qint64(m_w->width()) + (qint64(m_w->height()) << 16);
	if( m_cHeight != size ){
		post(TXM_HEIGHT, size);
		m_cHeight = size;
	}
	Engine &e = *m_w->m_engine;
	qint64 d = qint64(quint32(m_w->m_s.baud * 100));
	{
		QMutexLocker l(&e.lock());
		d = qint64(quint32(e.dem().GetBaudRate() * 100));
	}
	if( m_cBaud != d ){
		post(TXM_BAUD, d);
		m_cBaud = d;
	}
	freq();
	d = switchWord();
	if( m_cSwitch != d ){
		post(TXM_SWITCH, d);
		m_cSwitch = d;
	}
	d = viewWord();
	if( m_cView != d ){
		post(TXM_VIEW, d);
		m_cView = d;
	}
	qint64 reso, lpf;
	{
		QMutexLocker l(&e.lock());
		reso = qint64(quint32(e.dem().m_iirfw) + (quint32(e.dem().GetFilterTap()) << 16));
		lpf = qint64(quint32(e.dem().GetSmoozFreq()) + (quint32(e.dem().m_lpffreq) << 16));
	}
	if( m_cReso != reso ){
		post(TXM_RESO, reso);
		m_cReso = reso;
	}
	if( m_cLpf != lpf ){
		post(TXM_LPF, lpf);
		m_cLpf = lpf;
	}
}

// ===========================================================================
// commands application -> MMTTY
// ===========================================================================
void RemoteApi::setTuning(double mark, double space)
{
	AppSettings &s = m_w->m_s;
	s.mark = mark;
	s.shift = space - mark;
	m_w->updateTuningCombos();
}

void RemoteApi::handle(unsigned command, qint64 lParam)
{
	MainWindow &w = *m_w;
	Engine &e = *w.m_engine;
	AppSettings &s = w.m_s;
	const uint32_t lp = uint32_t(lParam);

	switch( command ){
		case RXM_HANDLE:
			m_timeout = 0;
			m_host->setApp(quintptr(lParam));
			if( m_host->appKnown() ){
				m_host->activateApp();
				initStat();
				stat();
				post(TXM_PTTEVENT, w.m_txDown ? 1 : 0);
			}
			break;
		case RXM_REQHANDLE:
			m_host->postRaw(TXM_HANDLE, qint64(m_host->windowHandle()));
			break;
		case RXM_PTT:
			cmdPtt(int(lParam));
			break;
		case RXM_CHAR:
			cmdChar(int(lp & 0xff));
			break;
		case RXM_EXIT:
			m_shared = nullptr;
			quitApp();
			break;
		case RXM_WINPOS:
			w.move(qint16(lp & 0xffff), qint16(lp >> 16));
			break;
		case RXM_WIDTH: {
			const int cw = int(lp & 0xffff), ch = int(lp >> 16);
			w.resize(cw > 10 ? cw : w.width(), ch > 10 ? ch : w.height());
			break;
		}
		case RXM_REQPARA:
			initStat();
			stat();
			break;
		case RXM_SETBAUD:
			if( lParam > 10 && lParam <= 120000 ){
				const double d = double(lParam) / 100.0;
				e.setBaudRate(d);
				s.baud = d;
				w.updateTuningCombos();
			}
			stat();
			break;
		case RXM_SETMARK:
			if( lParam > 100 && lParam < 3000 ){
				const double d = double(lParam);
				double space;
				{
					QMutexLocker l(&e.lock());
					space = e.dem().GetSpaceFreq();
				}
				e.setRxFrequencies(d, space);
				if( w.m_txDown && s.echo != 2 ){
					QMutexLocker l(&e.lock());
					e.mod().SetMarkFreq(d);
				}
				setTuning(d, space);
			}
			stat();
			break;
		case RXM_SETSPACE:
			if( lParam > 100 && lParam < 3000 ){
				const double d = double(lParam);
				double mark;
				{
					QMutexLocker l(&e.lock());
					mark = e.dem().GetMarkFreq();
				}
				e.setRxFrequencies(mark, d);
				if( w.m_txDown && s.echo != 2 ){
					QMutexLocker l(&e.lock());
					e.mod().SetSpaceFreq(d);
				}
				setTuning(mark, d);
			}
			stat();
			break;
		case RXM_SETSWITCH:
			applySwitch(lp);
			break;
		case RXM_SETHAM:
			ham();
			break;
		case RXM_SHOWSETUP: {
			const bool disableApp = lParam == 1;
			QMetaObject::invokeMethod(this, [this, disableApp] { showSetup(disableApp); }, Qt::QueuedConnection);
			break;
		}
		case RXM_SETVIEW:
			applyView(lp);
			break;
		case RXM_SETSQLVL: {
			const double lvl = std::clamp(double(lParam), 0.0, 1023.0);
			s.squelchLevel = lvl;
			e.setSquelch(s.squelch, lvl);
			break;
		}
		case RXM_SHOW:
			if( lParam ){
				if( !(m_flags & REM_SHOWOFF) ){
					w.show();
					w.raise();
				}
			}
			else {
				w.hide();
			}
			break;
		case RXM_SETFIG: {
			{
				QMutexLocker l(&e.lock());
				e.rtty().m_fig = lParam ? 1 : 0;
			}
			w.onFig(lParam != 0);
			post(TXM_FIGEVENT, lParam ? 1 : 0);
			break;
		}
		case RXM_SETRESO: {
			QMutexLocker l(&e.lock());
			const int iir = int(lp & 0xffff), tap = int(lp >> 16);
			if( iir > 0 && iir < 500 ) e.dem().SetIIR(iir);
			if( tap > 0 && tap <= 512 ) e.dem().SetFilterTap(tap);
			break;
		}
		case RXM_SETLPF: {
			QMutexLocker l(&e.lock());
			const int smooz = int(lp & 0xffff), lpf = int(lp >> 16);
			if( smooz > 10 && smooz < 1000 ) e.dem().SetSmoozFreq(smooz);
			if( lpf > 10 && lpf <= 1000 ) e.dem().SetLPFFreq(lpf);
			break;
		}
		case RXM_SETTXDELAY: {
			const int ms = int(lp & 0xffff);
			if( ms >= 0 && ms < 10000 ) w.m_txDelayMs = ms;
			break;
		}
		case RXM_UPDATECOM:
			updateComarray();
			break;
		case RXM_SUSPEND:
			cmdSuspend(lParam != 0);
			break;
		case RXM_NOTCH:
			e.setNotchFrequency(double(lp & 0xffff));
			freq();
			break;
		case RXM_PROFILE:
			cmdProfile(lp);
			break;
		case RXM_TIMER:
			if( !(lp & 0xffff0000) ){			// (the original asks the operator when the upper word is set)
				w.m_pttTimerSec = int(lp & 0xffff);
				w.m_pttClock.restart();
			}
			break;
		case RXM_ENBFOCUS:
			if( lParam ) m_flags &= ~unsigned(REM_DISFOCUS); else m_flags |= REM_DISFOCUS;
			break;
		case RXM_SETDEFFREQ:
			s.defMark = double(lp & 0xffff);
			s.defShift = double(lp >> 16);
			e.setTxNet(s.txNet, s.txFixShift, s.defMark, s.defShift);
			break;
		case RXM_SETLENGTH:
			if( lParam >= 5 && lParam <= 8 ){
				s.dataBits = int(lParam);
				e.setBitFormat(s.dataBits, s.stopSel, s.parity);
			}
			break;
		case RXM_ENBSHARED:
			if( lParam ) m_flags &= ~unsigned(REM_DISSHARED); else m_flags |= REM_DISSHARED;
			break;
		case RXM_PTTFSK: {
			// 0: AFSK only; 1 and 2: keyed FSK.  Without the UART of the original, FSK is keyed on a control line
			// of the PTT port: the one configured in the settings, otherwise DTR.
			const int mode = int(lp & 0x03);
			const int line = mode == 0 ? 0 : m_fskLine;
			if( s.fskLine != line ){
				if( s.fskLine ) m_fskLine = s.fskLine;
				s.fskLine = line;
				w.applyEngineConfig();
			}
			break;
		}
		case RXM_SOUNDSOURCE: {
			// 1 = left channel, otherwise right (original Wave.cpp)
			const int ch = (lp & 0x03) == 1 ? 0 : 1;
			if( s.inputChannel != ch ){
				s.inputChannel = ch;
				w.applyEngineConfig();
			}
			break;
		}
		default:
			break;
	}
}

void RemoteApi::cmdPtt(int mode)
{
	MainWindow &w = *m_w;
	const bool viaTx = (m_flags & REM_VIATX) != 0;
	switch( mode ){
		case 0:
		case 5:
			if( viaTx ) w.m_txEdit->clear();
			w.toRx(mode == 0);
			break;
		case 1:
			if( !w.m_txDown ) break;
			if( (viaTx && w.fifoLen()) || !w.m_engine->txIdle() ){
				w.m_reqRecv = true;
				w.updateTxUi();
			}
			else {
				w.toRx(false);
			}
			break;
		case 2:
			w.toTx(QByteArray(), false);
			break;
		case 3:
			w.toTx(QByteArray("~"), true);
			break;
		case 4:
			if( viaTx ) w.m_txEdit->clear();
			w.m_engine->clearTxBuffer();
			break;
		default:
			break;
	}
}

void RemoteApi::cmdChar(int c)
{
	MainWindow &w = *m_w;
	if( m_flags & REM_NOCNV ){
		w.m_engine->putRawCode(c);
	}
	else if( m_flags & REM_VIATX ){
		if( c != '\n' ) pushKey(char(c));
	}
	else if( c != '#' ){
		w.outputStr(QByteArray(1, char(c)));
	}
}

// Port of TMmttyWd::PushKey: the key buffer is the text field that the transmit loop reads from.
void RemoteApi::pushKey(char key)
{
	MainWindow &w = *m_w;
	QTextDocument *doc = w.m_txEdit->document();
	QTextCursor cur(doc);
	cur.movePosition(QTextCursor::End);
	auto put = [&](char c) {
		if( c == 0x08 ) cur.deletePreviousChar();
		else cur.insertText(QString(QLatin1Char(c)));
	};

	key = char(toupper(uchar(key)));
	if( m_wordWrap && key == ' ' ){
		if( doc->lastBlock().text().size() >= 60 ){
			put(key);
			key = 0x0d;
		}
	}
	int bits;
	{
		QMutexLocker l(&w.m_engine->lock());
		bits = w.m_engine->dem().m_BitLen;
	}
	if( bits <= 6 && key != '%' && key != 0x08 && key != '^' ){
		if( !m_check.IsChar(m_check.ConvRTTY(key)) ) return;
	}
	put(key);
}

// RXM_SETHAM: back to the default HAM RTTY setting (default mark and shift, 45.45 baud, 5 bits)
void RemoteApi::ham()
{
	MainWindow &w = *m_w;
	AppSettings &s = w.m_s;
	Engine &e = *w.m_engine;
	e.setRxFrequencies(s.defMark, s.defMark + s.defShift);
	{
		QMutexLocker l(&e.lock());
		e.mod().SetMarkFreq(s.defMark);
		e.mod().SetSpaceFreq(s.defMark + s.defShift);
	}
	s.baud = 45.45;
	s.dataBits = 5;
	s.stopSel = 1;			// the original's default stop length (DefStopLen 0 -> code 3) is the 1.5 bit setting here
	s.parity = 0;
	e.setBaudRate(s.baud);
	{
		QMutexLocker l(&e.lock());
		e.mod().SetBaudRate(s.baud);
	}
	e.setBitFormat(s.dataBits, s.stopSel, s.parity);
	setTuning(s.defMark, s.defMark + s.defShift);
	stat();
}

void RemoteApi::applySwitch(uint32_t d)
{
	MainWindow &w = *m_w;
	AppSettings &s = w.m_s;
	Engine &e = *w.m_engine;
	const SwitchState sw = decodeSwitch(d);

	s.demType = sw.demType;
	e.setDemodType(sw.demType);
	{
		QMutexLocker l(&e.lock());
		e.lms().m_Type = sw.lmsNotch ? 1 : 0;
		e.lms().m_twoNotch = sw.twoNotch ? 1 : 0;
		e.dem().m_lpf = sw.lpfIir ? 1 : 0;
		const bool aa = sw.dualPeak;
		if( aa != (e.dem().m_AA6YQ.m_fEnabled != 0) ){
			e.dem().m_AA6YQ.m_fEnabled = aa;
			if( aa ) e.dem().m_AA6YQ.Create();
		}
	}
	s.afc = sw.afc;			e.setAfc(sw.afc);
	s.txNet = sw.net;		e.setTxNet(sw.net, s.txFixShift, s.defMark, s.defShift);
	s.atc = sw.atc;			e.setAtc(sw.atc);
	s.bpf = sw.bpf;			e.setBandpass(sw.bpf);
	s.notch = sw.notch;		e.setNotch(sw.notch);
	s.squelch = sw.sq;		e.setSquelch(sw.sq, s.squelchLevel);
	s.reverse = sw.rev;		e.setReverse(sw.rev);
	s.uos = sw.uos;			e.setUos(sw.uos, s.txUos);
	s.afcMode = sw.fixShift;
	e.setAfcParams(s.afcMode, s.afcSquelch, s.afcTime, s.afcSweep);
	if( sw.viaTx ) m_flags |= REM_VIATX; else m_flags &= ~unsigned(REM_VIATX);
	w.m_viaTx = sw.viaTx;
	m_wordWrap = sw.wordWrap;
	w.m_sendWay = sw.sendWay;
	w.updateMenuState();
	stat();
}

void RemoteApi::applyView(uint32_t d)
{
	MainWindow &w = *m_w;
	AppSettings &s = w.m_s;
	const ViewState v = decodeView(d);
	s.fftRange = v.fftRange;
	s.fftGain = v.fftGain;
	s.fftResp = v.fftResp;
	m_xySize = v.xySize;
	m_xyQuality = v.xyQuality;
	w.m_engine->setFftOptions(s.fftGain, s.fftResp, s.fftRange);
	if( s.showXY != v.xyOn ){
		s.showXY = v.xyOn;
		w.m_engine->setXYEnabled(v.xyOn);
	}
	w.updateMenuState();
}

void RemoteApi::showSetup(bool disableApp)
{
	if( m_setupOpen ){
		m_w->raise();
		return;
	}
	m_setupOpen = true;
	if( disableApp ) m_host->enableApp(false);
	m_w->openSettings();
	if( disableApp ) m_host->enableApp(true);
	m_host->activateApp();
	m_setupOpen = false;
	stat();
}

void RemoteApi::cmdSuspend(bool suspend)
{
	MainWindow &w = *m_w;
	if( suspend ){
		w.toRx(true);
		w.m_engine->stop();					// releases the sound card and the PTT / FSK port
		if( !(m_flags & REM_SHOWOFF) ) w.hide();
	}
	else {
		w.m_engine->start();
		w.applyEngineConfig();				// settings changed while suspended
		if( !(m_flags & REM_SHOWOFF) ) w.show();
	}
}

// Profiles: a snapshot of the receiver / transmitter settings in numbered groups of the settings file.
// Load / save of profile files (flags 3 and 4) asked the operator for a file name in the original and are not supported.
void RemoteApi::cmdProfile(uint32_t flag)
{
	MainWindow &w = *m_w;
	QSettings &ini = *w.m_ini;
	const int n = int(flag & 0xffff);
	if( n > kProfiles - 1 ) return;
	const QString key = QStringLiteral("Profiles/%1").arg(n);

	switch( flag & 0xffff0000 ){
		case 0x00000000: {			// recall
			if( !ini.contains(key + QStringLiteral("/name")) ) return;
			AppSettings p = w.m_s;
			ini.beginGroup(key);
			p.load(ini);
			ini.endGroup();
			const AppSettings &cur = w.m_s;		// not part of a profile: station, devices, ports, macros
			p.call = cur.call;
			p.inputDevice = cur.inputDevice;
			p.outputDevice = cur.outputDevice;
			p.inputChannel = cur.inputChannel;
			p.sampleRate = cur.sampleRate;
			p.txOffset = cur.txOffset;
			p.pttPort = cur.pttPort;
			p.pttLines = cur.pttLines;
			p.pttInvert = cur.pttInvert;
			p.fskLine = cur.fskLine;
			p.fskInvert = cur.fskInvert;
			for( int i = 0; i < AppSettings::kMacros; i++ ){
				p.macro[i] = cur.macro[i];
				p.macroName[i] = cur.macroName[i];
			}
			for( int i = 0; i < AppSettings::kInputButtons; i++ ){
				p.inButton[i] = cur.inButton[i];
				p.inButtonName[i] = cur.inButtonName[i];
			}
			p.markList = cur.markList;
			p.shiftList = cur.shiftList;
			p.baudList = cur.baudList;
			w.m_s = p;
			w.pushSettings();
			w.updateMenuState();
			w.updateTuningCombos();
			stat();
			post(TXM_PROFILE, n);
			break;
		}
		case 0x00010000:			// store (the name comes from the shared memory)
		case 0x00050000: {			// assign: store without announcing it
			QString name = m_shared ? readString(m_shared->ProfileName[n], kProfileNameLen) : QString();
			if( name.isEmpty() ) name = QStringLiteral("Remote %1").arg(n + 1);
			ini.beginGroup(key);
			ini.setValue(QStringLiteral("name"), name);
			w.m_s.save(ini);
			ini.endGroup();
			ini.sync();
			if( (flag & 0xffff0000) == 0x00010000 ) post(TXM_PROFILE, 0x00010000 + n);
			break;
		}
		case 0x00020000:			// delete
			ini.remove(key);
			ini.sync();
			post(TXM_PROFILE, 0x00020000 + n);
			break;
		default:
			break;
	}
}

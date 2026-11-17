#include "Engine.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>
#include <QMetaObject>
#include <QMutexLocker>
#include <climits>
#include <cmath>

namespace {
constexpr int kTickMs = 5;		// worker poll interval
constexpr int kAfcPeakDown = 128;
}

// ===========================================================================
// construction / lifecycle
// ===========================================================================
Engine::Engine(const Config &cfg, QObject *parent)
	: QObject(parent), m_cfg(cfg)
{
	// The DSP code reads these process-wide parameters (as in the original).
	SampFreq = cfg.sampleRate;
	sys.m_SampFreq = cfg.sampleRate;
	sys.m_TxOffset = cfg.txOffset;
	InitSampType();

	m_dem.reset(new CFSKDEM);
	m_mod.reset(new CFSKMOD);
	m_mod->SetDem(m_dem.get());
	m_block = SampSize;
	m_fftWindow = int((SampType == 2 ? 3000 : 4000) * FFT_SIZE / SampFreq);
	m_buf.assign(m_block * 4, 0.0);

	memset(m_zBpf, 0, sizeof(m_zBpf));
	memset(m_hBpf, 0, sizeof(m_hBpf));
	m_lms.SetWindow(m_dem->GetMarkFreq(), m_dem->GetSpaceFreq());
	m_rxMark = m_dem->GetMarkFreq();
	m_rxSpace = m_dem->GetSpaceFreq();
	m_mod->SetSampFreq(SampFreq + sys.m_TxOffset);

	moveToThread(&m_thread);
}

Engine::~Engine()
{
	stop();
}

void Engine::start()
{
	if( m_started ) return;
	m_started = true;
	m_thread.start();
	QMetaObject::invokeMethod(this, "init", Qt::BlockingQueuedConnection);
}

void Engine::stop()
{
	if( !m_started ) return;
	m_started = false;
	if( m_thread.isRunning() ){
		QMetaObject::invokeMethod(this, "shutdown", Qt::BlockingQueuedConnection);
		m_thread.quit();
		m_thread.wait();
	}
}

void Engine::init()
{
	m_mono.start();
	m_clock.start();
	m_lastClockNs = 0;
	QString err;
	if( !m_ptt.open(m_cfg.ptt, &err) ) emit message(err);
	setupFsk();
	openInput();
	m_timer = new QTimer(this);
	m_timer->setTimerType(Qt::PreciseTimer);
	m_timer->setInterval(kTickMs);
	connect(m_timer, &QTimer::timeout, this, &Engine::tick);
	m_timer->start();
}

void Engine::shutdown()
{
	if( m_timer ){
		m_timer->stop();
		delete m_timer;
		m_timer = nullptr;
	}
	setPttLine(false);
	closeInput();
	closeOutput();
	m_keyer.stop();
	m_fskOn = m_fskTx = false;
	m_ptt.close();
	m_wav.close();
}

// Starts the keying thread when a serial FSK line (or a test tap) is configured.
void Engine::setupFsk()
{
	m_keyer.stop();
	m_fskTx = false;
	m_fskOn = (m_ptt.isOpen() && m_ptt.fskConfigured()) || bool(m_cfg.fskTap);
	if( !m_fskOn ) return;
	const auto tap = m_cfg.fskTap;			// copy: the keying thread must not touch m_cfg
	m_keyer.start([this, tap](bool mark) {
		if( tap ) tap(mark, FskKeyer::nowNs());
		m_ptt.setFsk(mark);
	});
}

void Engine::applyConfig(const Config &cfg)
{
	QMetaObject::invokeMethod(this, [this, cfg]{ reconfigure(cfg); }, Qt::BlockingQueuedConnection);
}

void Engine::reconfigure(const Config &cfg)
{
	m_keyer.stop();						// the keying thread uses the port: stop it before anything changes
	m_fskTx = false;
	const bool devChanged = cfg.inputDevice != m_cfg.inputDevice || cfg.outputDevice != m_cfg.outputDevice
	                        || cfg.nullAudio != m_cfg.nullAudio || cfg.inputChannel != m_cfg.inputChannel
	                        || cfg.fifoTxBlocks != m_cfg.fifoTxBlocks;
	const bool pttChanged = cfg.ptt.portName != m_cfg.ptt.portName || cfg.ptt.lines != m_cfg.ptt.lines
	                        || cfg.ptt.invert != m_cfg.ptt.invert || cfg.ptt.fskLine != m_cfg.ptt.fskLine
	                        || cfg.ptt.fskInvert != m_cfg.ptt.fskInvert;
	const bool offsetChanged = cfg.txOffset != m_cfg.txOffset;
	m_cfg = cfg;
	if( offsetChanged ){
		QMutexLocker l(&m_mutex);
		sys.m_TxOffset = cfg.txOffset;
		m_mod->SetSampFreq(SampFreq + sys.m_TxOffset);
	}
	if( pttChanged ){
		QString err;
		if( !m_ptt.open(cfg.ptt, &err) ) emit message(err);
	}
	setupFsk();
	if( devChanged ){
		closeOutput();
		closeInput();
		if( !m_tx || sys.m_echo == 2 ) openInput();
		if( m_tx ) openOutput();
	}
}

// ===========================================================================
// device helpers (worker thread)
// ===========================================================================
QList<QPair<QString, QByteArray>> Engine::inputDevices()
{
	QList<QPair<QString, QByteArray>> l;
	for( const QAudioDevice &d : QMediaDevices::audioInputs() ) l.append({d.description(), d.id()});
	return l;
}

QList<QPair<QString, QByteArray>> Engine::outputDevices()
{
	QList<QPair<QString, QByteArray>> l;
	for( const QAudioDevice &d : QMediaDevices::audioOutputs() ) l.append({d.description(), d.id()});
	return l;
}

static QAudioDevice findDevice(const QList<QAudioDevice> &list, const QByteArray &id, const QAudioDevice &def)
{
	if( id.isEmpty() ) return def;
	for( const QAudioDevice &d : list ) if( d.id() == id ) return d;
	return def;
}

// Picks the format closest to MMTTY's native one that the device accepts.
static QAudioFormat chooseFormat(const QAudioDevice &dev, double wantRate)
{
	QAudioFormat f;
	f.setSampleRate(int(wantRate + 0.5));
	f.setChannelCount(1);
	f.setSampleFormat(QAudioFormat::Int16);
	if( dev.isFormatSupported(f) ) return f;
	// fall back to what the device prefers; we resample and mix ourselves
	QAudioFormat p = dev.preferredFormat();
	if( p.sampleFormat() == QAudioFormat::Unknown ) p.setSampleFormat(QAudioFormat::Int16);
	return p;
}

bool Engine::openInput()
{
	closeInput();
	if( m_cfg.nullAudio ) return true;
	const QAudioDevice dev = findDevice(QMediaDevices::audioInputs(), m_cfg.inputDevice, QMediaDevices::defaultAudioInput());
	if( dev.isNull() ){
		emit message(tr("No sound input device available"));
		return false;
	}
	m_inFmt = chooseFormat(dev, SampBase);
	m_source = new QAudioSource(dev, m_inFmt, this);
	m_source->setBufferSize(m_inFmt.bytesForDuration(200000));		// 200 ms
	m_inDev = m_source->start();
	if( !m_inDev || m_source->error() != QAudio::NoError ){
		emit message(tr("Cannot open sound input: %1").arg(dev.description()));
		closeInput();
		return false;
	}
	m_inRs.setRates(double(m_inFmt.sampleRate()), SampFreq);
	m_inQueue.clear();
	return true;
}

void Engine::closeInput()
{
	if( m_source ){
		m_source->stop();
		delete m_source;
		m_source = nullptr;
	}
	m_inDev = nullptr;
	m_inQueue.clear();
}

bool Engine::openOutput()
{
	closeOutput();
	if( m_cfg.nullAudio ) return true;
	const QAudioDevice dev = findDevice(QMediaDevices::audioOutputs(), m_cfg.outputDevice, QMediaDevices::defaultAudioOutput());
	if( dev.isNull() ){
		emit message(tr("No sound output device available"));
		return false;
	}
	m_outFmt = chooseFormat(dev, SampBase);
	m_sink = new QAudioSink(dev, m_outFmt, this);
	// buffer depth: fifoTxBlocks blocks of audio
	const qint64 us = qint64(1e6 * m_block * qMax(2, m_cfg.fifoTxBlocks) / SampFreq);
	m_sink->setBufferSize(m_outFmt.bytesForDuration(us));
	m_outDev = m_sink->start();
	if( !m_outDev || m_sink->error() != QAudio::NoError ){
		emit message(tr("Cannot open sound output: %1").arg(dev.description()));
		closeOutput();
		return false;
	}
	m_outRs.setRates(SampFreq, double(m_outFmt.sampleRate()));
	m_audioEndNs = m_mono.nsecsElapsed();
	return true;
}

void Engine::closeOutput()
{
	if( m_sink ){
		m_sink->stop();
		delete m_sink;
		m_sink = nullptr;
	}
	m_outDev = nullptr;
}

// Reads whatever the sound card delivered, converts to the DSP rate, appends to the queue.
void Engine::pullInput()
{
	if( !m_inDev ) return;
	const QByteArray raw = m_inDev->readAll();
	if( raw.isEmpty() ) return;
	std::vector<double> tmp;
	AudioConvert::decode(raw, m_inFmt, m_cfg.inputChannel, tmp);
	m_inRs.process(tmp.data(), tmp.size(), m_inQueue);
	// guard against unbounded growth if the GUI/DSP stalls
	const size_t cap = size_t(SampFreq * 2);
	if( m_inQueue.size() > cap ){
		m_inQueue.erase(m_inQueue.begin(), m_inQueue.end() - cap);
		m_lostSound = true;
	}
}

bool Engine::outputHasRoom() const
{
	if( !m_sink ) return true;
	const qint64 need = m_outFmt.bytesForDuration(qint64(1e6 * m_block / SampFreq)) * 2;
	return m_sink->bytesFree() >= need;
}

void Engine::writeOutput(const double *blk, int n)
{
	if( !m_outDev ) return;
	std::vector<double> rs;
	m_outRs.process(blk, size_t(n), rs);
	QByteArray out;
	AudioConvert::encode(rs.data(), rs.size(), m_outFmt, out);
	const qint64 w = m_outDev->write(out);
	if( w < out.size() ) m_lostSound = true;
	const qint64 now = m_mono.nsecsElapsed();
	const qint64 durNs = qint64(1e9 * double(n) / SampFreq);
	m_audioEndNs = qMax(m_audioEndNs, now) + durNs;
}

// Software clock: returns true when one more block worth of real time has elapsed.
bool Engine::clockCredit()
{
	const qint64 now = m_mono.nsecsElapsed();
	m_clockCredit += double(now - m_lastClockNs) * 1e-9 * SampFreq;
	m_lastClockNs = now;
	if( m_clockCredit > m_block * 8 ) m_clockCredit = m_block * 8;
	if( m_clockCredit >= m_block ){
		m_clockCredit -= m_block;
		return true;
	}
	return false;
}

// ===========================================================================
// processing loop
// ===========================================================================
void Engine::tick()
{
	QMutexLocker lock(&m_mutex);
	// Run as many blocks as are available this tick (bounded so the GUI stays responsive).
	for( int guard = 0; guard < 16; guard++ ){
		const bool playing = (m_wav.mode() == WavFile::Play) && !m_tx;
		const bool haveInput = m_inDev != nullptr;
		const bool needOutput = m_tx && !m_cfg.nullAudio && m_sink;
		bool run = false;

		if( haveInput ) pullInput();

		if( !m_cfg.realtime && m_cfg.nullAudio && !m_fskTx ){
			run = true;					// free-running (tests); FSK keying must stay in real time
		}
		else if( playing ){
			run = clockCredit();
		}
		else if( m_tx && needOutput ){
			if( sys.m_echo == 2 && haveInput ){
				run = m_inQueue.size() >= size_t(m_block) && outputHasRoom();
			}
			else {
				run = outputHasRoom();
			}
		}
		else if( m_tx || !haveInput ){
			run = clockCredit();			// no card: pace by software clock
		}
		else {
			run = m_inQueue.size() >= size_t(m_block);
		}
		if( !run ) break;

		processBlock();
		if( !m_cfg.realtime && m_cfg.nullAudio && guard >= 15 ) break;
	}
	deliverRx();
}

void Engine::processBlock()
{
	double *buf = m_buf.data();
	const int len = m_block;

	// ---- acquire ---------------------------------------------------------
	if( m_inDev && (!m_tx || sys.m_echo == 2) && m_wav.mode() != WavFile::Play ){
		if( m_inQueue.size() >= size_t(len) ){
			std::copy(m_inQueue.begin(), m_inQueue.begin() + len, buf);
			m_inQueue.erase(m_inQueue.begin(), m_inQueue.begin() + len);
		}
		else {
			memset(buf, 0, sizeof(double) * len);		// input underrun
			m_lostSound = true;
		}
	}
	else if( !m_tx ){
		memset(buf, 0, sizeof(double) * len);
	}
	// else: transmitting with the input closed - the buffer still holds the previous TX block,
	// which is what the demodulator sees in internal loopback mode (as in the original).
	if( m_wav.mode() == WavFile::Play && (!m_tx) ){
		if( !m_wav.read(buf, len) ){
			m_wav.close();
			emit fileFinished();
		}
	}
	else if( m_wav.mode() == WavFile::Record ){
		m_wav.write(buf, len);
	}

	// ---- test generator: the modulator feeds the receiver -------------------
	if( m_test && !m_tx ){
		for( int i = 0; i < len; i++ ) buf[i] += m_mod->Do(1);
	}

	// ---- receive filters ---------------------------------------------------
	if( m_bpf || m_lmsbpf ){
		for( int i = 0; i < len; i++ ){
			if( m_bpf ) buf[i] = DoFIR(m_hBpf, m_zBpf, buf[i], m_bpfTap);
			if( m_lmsbpf ) buf[i] = m_lms.Do(buf[i]);
		}
	}
	if( m_fftSw ) m_fft.CollectFFT(buf, len);

	// ---- demodulate --------------------------------------------------------
	if( !m_tx || sys.m_echo ){
		for( int i = 0; i < len; i++ ) m_dem->Do(buf[i]);
	}

	// ---- modulate ----------------------------------------------------------
	if( m_tx ){
		if( m_wantRx && !m_reqTx ){
			// Draining: everything has been generated; wait until the sound card has played it
			// (or, for FSK, until the keying thread has sent the last edge).
			bool drained;
			if( m_abortOutput ) drained = true;
			else if( m_fskTx ) drained = FskKeyer::nowNs() >= m_fskEndNs && m_keyer.pending() == 0;
			else drained = !m_outDev || m_mono.nsecsElapsed() >= m_audioEndNs;
			if( drained ) switchToRx();
			return;
		}
		if( m_fskTx ){
			// Software FSK: every change of the modulator's output bit becomes a timed edge on the
			// serial line.  One modulator sample lasts exactly bit-time / ReCount, which removes the
			// rounding of the samples-per-bit count (the AFSK path compensates with the TX offset).
			const double sampleNs = 1e9 / (m_mod->GetBaudRate() * double(m_mod->m_ReCount));
			const qint64 now = FskKeyer::nowNs();
			if( m_fskT < double(now) ) m_fskT = double(now) + 20e6;		// fell behind: resynchronise
			std::vector<FskKeyer::Edge> edges;
			for( int i = 0; i < len; i++ ){
				buf[i] = m_mod->Do(sys.m_echo);
				const int out = m_mod->GetOut();
				// carrier off (-1) idles at mark; otherwise follow the modulator, honouring REV
				const bool mark = (out < 0) ? true : ((out != 0) != (m_mod->GetRev() != 0));
				if( mark != m_fskLast ){
					edges.push_back({qint64(m_fskT + i * sampleNs), mark});
					m_fskLast = mark;
				}
			}
			m_fskT += len * sampleNs;
			m_fskEndNs = qint64(m_fskT);
			m_keyer.push(edges);
			return;			// no audio output while keying the line
		}
		for( int i = 0; i < len; i++ ) buf[i] = m_mod->Do(sys.m_echo);
		if( m_outDev ) writeOutput(buf, len);
	}
	else if( m_reqTx ){
		switchToTx();
	}
}

void Engine::switchToRx()
{
	if( m_fskTx ){
		if( m_abortOutput ) m_keyer.abort(true);		// drop pending edges, line to mark
		m_fskTx = false;
	}
	if( m_outDev ){
		// let the card drain unless an abort was requested
		if( m_abortOutput && m_sink ) m_sink->reset();
	}
	closeOutput();
	m_fft.ClearBuf();
	if( !m_inDev && !m_cfg.nullAudio ) openInput();
	m_dem->m_OverFlow = 0;
	m_dem->ClearMode();
	m_tx = false;
	m_wantRx = false;
	m_abortOutput = false;
	setPttLine(false);
	updateRev();
	updateLimit();
	emit txStateChanged(false);
}

void Engine::switchToTx()
{
	if( sys.m_echo != 2 ){
		closeInput();
		m_fft.ClearBuf();
	}
	m_mod->InitPhase();
	m_mod->SetCount(m_block * 3);
	m_fskTx = m_fskOn;
	if( m_fskTx ){
		// The software clock paces the modulator.  Start with two blocks of credit so that the
		// edges handed to the keying thread are always 1-2 blocks (about 90-190 ms) in the future.
		m_fskLast = true;								// the line idles at mark
		m_fskT = double(FskKeyer::nowNs()) + 30e6;
		m_fskEndNs = qint64(m_fskT);
		m_lastClockNs = m_mono.nsecsElapsed();
		m_clockCredit = m_block * 2;
	}
	else if( m_wav.mode() != WavFile::Play ) openOutput();
	if( sys.m_echo == 2 && m_sink ){
		// prime the card so the loopback receiver stays in step
		std::vector<double> z(m_block, 0.0);
		for( int i = 0; i < 4 && outputHasRoom(); i++ ) writeOutput(z.data(), m_block);
	}
	if( sys.m_echo != 2 ){
		m_dem->m_OverFlow = 0;
		m_dem->ClearMode();
	}
	m_tx = m_reqTx;
	setPttLine(true);
	updateRev();
	updateLimit();
	emit txStateChanged(true);
}

void Engine::setPttLine(bool tx)
{
	m_ptt.setPtt(tx);
}

// Moves decoded characters to the GUI.
void Engine::deliverRx()
{
	QString text;
	int d;
	const int bits = m_dem->m_BitLen;
	while( (d = m_dem->GetData()) >= 0 ){
		if( bits == 7 || bits == 8 ){
			if( bits == 7 ) d &= 0x7f;
			if( m_rawRx ) emit rawChar(d);
			if( d ) text += QChar(ushort(d));
		}
		else {
			if( m_rawRx ) emit rawChar(d);
			const char c = m_rtty.ConvAscii(d);
			if( c ) text += QChar(ushort(uchar(c)));
		}
	}
	const bool fig = m_rtty.m_fig != 0;
	if( fig != m_figLast ){
		m_figLast = fig;
		emit figChanged(fig);
	}
	if( !text.isEmpty() ) emit rxText(text, m_tx);
}

// ===========================================================================
// control API (called from the GUI thread)
// ===========================================================================
void Engine::requestTx(bool carrierFirst)
{
	QMutexLocker l(&m_mutex);
	if( m_tx || m_reqTx ) return;
	m_wantRx = false;
	m_abortOutput = false;
	m_mod->ClearTXBuf();
	m_rtty.ClearTX();
	m_mod->SetBaudRate(m_dem->GetBaudRate());
	updateNet();
	m_reqTx = true;
	m_mod->m_Amp.Reset();
	m_mod->m_AmpVal = 1;
	m_mod->OutTone(carrierFirst ? 1 : 0, m_block);
	if( sys.m_echo != 2 ){
		m_dem->ClearRXBuf();
		m_bpfWasOn = m_bpf;
		m_bpf = false;
	}
	m_mod->SetDiddleTimer(int(SampFreq / 4));
	updateRev();
	updateLimit();
}

void Engine::requestRx(bool immediate)
{
	QMutexLocker l(&m_mutex);
	if( !m_tx && !m_reqTx ) return;
	m_mod->SetDiddleTimer(-1);
	if( immediate ){
		m_mod->DeleteTXBuf();
		m_abortOutput = true;
	}
	m_wantRx = true;
	m_reqTx = false;
	if( sys.m_echo != 2 ){
		m_rtty.ClearRX();
		m_dem->ClearRXBuf();
		m_bpf = m_bpfWasOn;
	}
	updateNet();
}

void Engine::putText(const QByteArray &ascii)
{
	QMutexLocker l(&m_mutex);
	switch( m_mod->m_BitLen ){
		case 7:
		case 8:
			for( char c : ascii ) m_mod->PutData(uchar(c));
			break;
		default: {
			QByteArray tmp(ascii.size() * 3 + 4, 0);
			QByteArray z = ascii;
			const int n = m_rtty.ConvRTTY(reinterpret_cast<BYTE *>(tmp.data()), z.constData());
			for( int i = 0; i < n; i++ ) m_mod->PutData(uchar(tmp[i]));
			break;
		}
	}
}

void Engine::putRawCode(int code)
{
	QMutexLocker l(&m_mutex);
	m_mod->PutData(code);
}

void Engine::setRawRx(bool on)
{
	QMutexLocker l(&m_mutex);
	m_rawRx = on;
}

void Engine::clearTxBuffer()
{
	QMutexLocker l(&m_mutex);
	m_mod->DeleteTXBuf();
}

bool Engine::fadeOutTx()
{
	QMutexLocker l(&m_mutex);
	if( m_mod->m_AmpVal == 1 ){
		m_mod->m_AmpVal = 0;
		return true;
	}
	return false;
}

void Engine::setDiddleTimer(int n)
{
	QMutexLocker l(&m_mutex);
	m_mod->SetDiddleTimer(n);
}

void Engine::setTxEcho(int echo)
{
	QMutexLocker l(&m_mutex);
	sys.m_echo = echo;
}

bool Engine::txIdle() const
{
	QMutexLocker l(&m_mutex);
	return m_mod->IsIdle() != 0;
}

int Engine::txBufferCount() const
{
	QMutexLocker l(&m_mutex);
	return m_mod->GetBufCount();
}

void Engine::updateRev()
{
	int txrev = sys.m_Rev;
	if( sys.m_TxDisRev ) txrev = 0;
	m_mod->SetRev(txrev);
	int rxrev = sys.m_Rev;
	if( m_reqTx && sys.m_echo != 2 ) rxrev = txrev;
	m_dem->SetRev(rxrev);
}

void Engine::updateLimit()
{
	if( m_reqTx && sys.m_echo != 2 ){
		m_dem->m_Limit = 0;
	}
	else {
		m_dem->m_Limit = 1;
		m_dem->m_LimitGain = sys.m_LimitGain;
	}
}

// TX frequency handling when switching between RX and TX (original TMmttyWd::UpdateNet).
void Engine::updateNet()
{
	if( m_reqTx ){
		if( sys.m_TxNet && !sys.m_TxFixShift ){
			m_mod->SetMarkFreq(m_dem->GetMarkFreq());
			m_mod->SetSpaceFreq(m_dem->GetSpaceFreq());
		}
		else {
			m_rxMark = m_dem->GetMarkFreq();
			m_rxSpace = m_dem->GetSpaceFreq();
			double mfq, sfq;
			if( sys.m_TxNet ){
				mfq = m_dem->GetMarkFreq();
				sfq = m_dem->GetSpaceFreq();
				mfq += ((sfq - mfq) - sys.m_DefShift) * 0.5;
			}
			else {
				mfq = sys.m_DefMarkFreq;
			}
			mfq = int(mfq);
			sfq = mfq + sys.m_DefShift;
			m_mod->SetMarkFreq(mfq);
			m_mod->SetSpaceFreq(sfq);
			m_dem->SetMarkFreq(mfq);
			m_dem->SetSpaceFreq(sfq);
			emit tuningChanged(mfq, sfq);
		}
	}
	else if( !sys.m_TxNet || sys.m_TxFixShift ){
		m_dem->SetMarkFreq(m_rxMark);
		m_dem->SetSpaceFreq(m_rxSpace);
		emit tuningChanged(m_rxMark, m_rxSpace);
	}
}

// ===========================================================================
// tuning / DSP parameters
// ===========================================================================
void Engine::setRxFrequencies(double mark, double space)
{
	QMutexLocker l(&m_mutex);
	m_dem->SetMarkFreq(mark);
	m_dem->SetSpaceFreq(space);
	if( !m_reqTx ){
		m_mod->SetMarkFreq(mark);
		m_mod->SetSpaceFreq(space);
	}
	if( m_bpf || m_lmsbpf ){
		MakeFilter(m_hBpf, m_bpfTap, ffBPF, SampFreq, m_dem->GetMarkFreq() - m_bpfFw, m_dem->GetSpaceFreq() + m_bpfFw, 60, 1.0);
		m_lms.SetWindow(m_dem->GetMarkFreq(), m_dem->GetSpaceFreq());
	}
}

void Engine::setBaudRate(double baud)
{
	QMutexLocker l(&m_mutex);
	m_dem->SetBaudRate(baud);
	if( !m_reqTx ) m_mod->SetBaudRate(baud);
}

void Engine::setReverse(bool rev)
{
	QMutexLocker l(&m_mutex);
	sys.m_Rev = rev ? 1 : 0;
	updateRev();
}

void Engine::setBitFormat(int dataBits, int stopSel, int parity)
{
	QMutexLocker l(&m_mutex);
	m_dem->m_BitLen = dataBits;
	m_mod->m_BitLen = dataBits;
	m_dem->m_Parity = parity;
	m_mod->m_Parity = parity;
	// 0 = 1 bit, 1 = 1.5 bit (RTTY default 1.42), 2 = 2 bit  ->  MMTTY stop-length codes
	const int code = stopSel == 0 ? 0 : (stopSel == 2 ? 2 : 4);
	m_dem->m_StopLen = code;
	m_mod->m_StopLen = code;
}

void Engine::setDemodType(int type)
{
	QMutexLocker l(&m_mutex);
	m_dem->m_type = type;
}

void Engine::setBandpass(bool on)
{
	QMutexLocker l(&m_mutex);
	m_bpf = on;
	m_bpfWasOn = on;
	if( on ) MakeFilter(m_hBpf, m_bpfTap, ffBPF, SampFreq, m_dem->GetMarkFreq() - m_bpfFw, m_dem->GetSpaceFreq() + m_bpfFw, 60, 1.0);
}

void Engine::setNotch(bool on)
{
	QMutexLocker l(&m_mutex);
	m_lmsbpf = on;
	if( on ){
		m_lms.SetWindow(m_dem->GetMarkFreq(), m_dem->GetSpaceFreq());
	}
}

void Engine::setNotchFrequency(double f)
{
	QMutexLocker l(&m_mutex);
	m_lms.m_lmsNotch2 = m_lms.m_lmsNotch;
	m_lms.m_lmsNotch = int(f + 0.5);
	m_lms.SetWindow(m_dem->GetMarkFreq(), m_dem->GetSpaceFreq());
}

void Engine::setTest(bool on)
{
	QMutexLocker l(&m_mutex);
	m_test = on;
	if( on ){
		m_rtty.ClearTX();
		m_mod->ClearTXBuf();
		m_mod->SetBaudRate(m_dem->GetBaudRate());
		m_mod->SetMarkFreq(m_dem->GetMarkFreq());
		m_mod->SetSpaceFreq(m_dem->GetSpaceFreq());
		m_mod->SetRev(m_dem->GetRev());
	}
}

void Engine::setAtc(bool on)
{
	QMutexLocker l(&m_mutex);
	m_dem->m_atc = on ? 1 : 0;
}

void Engine::setSquelch(bool on, double level)
{
	QMutexLocker l(&m_mutex);
	m_dem->SetSQ(on ? 1 : 0);
	m_dem->SetSQLevel(level);
}

void Engine::setUos(bool rxUos, bool txUos)
{
	QMutexLocker l(&m_mutex);
	m_rtty.m_uos = rxUos ? 1 : 0;
	sys.m_txuos = txUos ? 1 : 0;
}

void Engine::setTxNet(bool net, bool fixShift, double defMark, double defShift)
{
	QMutexLocker l(&m_mutex);
	sys.m_TxNet = net ? 1 : 0;
	sys.m_TxFixShift = fixShift ? 1 : 0;
	sys.m_DefMarkFreq = defMark;
	sys.m_DefShift = defShift;
	if( m_reqTx ) updateNet();
}

Engine::Level Engine::signalLevel() const
{
	QMutexLocker l(&m_mutex);
	Level r;
	r.limiter = m_dem->m_Limit != 0;
	r.avg = m_dem->m_avgdeff / (r.limiter ? 10.0 : 1.0);
	r.sqLevel = m_dem->GetSQLevel();
	r.sqOn = m_dem->GetSQ() != 0;
	r.open = r.sqLevel <= r.avg;
	return r;
}

// ===========================================================================
// scopes
// ===========================================================================
void Engine::setFftOptions(int gain, int resp, int range)
{
	QMutexLocker l(&m_mutex);
	sys.m_FFTGain = gain;
	sys.m_FFTResp = resp;
	m_fftFw = range;
}

void Engine::computeScopeRange(double &low, int &fm) const
{
	const double shift = fabs(m_dem->GetMarkFreq() - m_dem->GetSpaceFreq());
	const double center = (m_dem->GetMarkFreq() + m_dem->GetSpaceFreq()) / 2.0;
	double span;
	switch( m_fftFw ){
		case 0:  span = shift >= 800 ? 3000 : (shift >= 400 ? 2000 : 1000); break;
		case 1:  span = 500;  break;
		case 2:  span = 1000; break;
		case 3:  span = 1500; break;
		case 4:  span = 2000; break;
		default: span = 3000; break;
	}
	low = center - span / 2;
	fm = int(span * FFT_SIZE / SampFreq + 0.5);
}

double Engine::screenFrequency(int x, int width) const
{
	QMutexLocker l(&m_mutex);
	double low;
	int fm;
	computeScopeRange(low, fm);
	const double fw = fm * SampFreq / FFT_SIZE;
	return (x * fw) / double(width) + low;
}

bool Engine::fetchSpectrum(Spectrum &out)
{
	QMutexLocker l(&m_mutex);
	if( !m_fftSw ) return false;
	if( !m_fft.m_CollectFFT ) return false;

	double gain;
	switch( sys.m_FFTGain ){
		case 0: gain = 30.0; break;
		case 1: gain = 34.0; break;
		case 2: gain = 42.0; break;
		case 3: gain = 54.0; break;
		default: gain = (sys.m_echo != 2 && m_tx) ? 0.02 : 0.1; break;
	}
	m_fft.CalcFFT(m_fftWindow, gain, sys.m_FFTResp);
	m_fft.TrigFFT();

	out.window = m_fftWindow;
	out.bins.assign(m_fft.m_fft, m_fft.m_fft + m_fftWindow + 1);
	out.raw.assign(m_fft.m_fft, m_fft.m_fft + 2048);
	computeScopeRange(out.low, out.fm);
	out.mark = m_dem->GetMarkFreq();
	out.space = m_dem->GetSpaceFreq();
	out.fftGain = sys.m_FFTGain;
	out.notch1 = (m_lmsbpf && m_lms.m_Type) ? m_lms.m_lmsNotch : 0;
	out.notch2 = (m_lmsbpf && m_lms.m_Type && m_lms.m_twoNotch) ? m_lms.m_lmsNotch2 : 0;
	out.lostSound = m_lostSound;
	out.overflow = m_dem->m_OverFlow != 0;
	m_lostSound = false;
	m_dem->m_OverFlow = 0;
	return true;
}

void Engine::setXYEnabled(bool on)
{
	QMutexLocker l(&m_mutex);
	m_xyEnabled = on;
	m_dem->m_XYScopeMark.Collect(512);
	m_dem->m_XYScopeSpace.Collect(512);
	m_dem->m_XYScope = on ? 1 : 0;
}

bool Engine::fetchXY(std::vector<double> &mark, std::vector<double> &space)
{
	QMutexLocker l(&m_mutex);
	if( !m_xyEnabled || !m_dem->m_XYScope ) return false;
	if( !m_dem->m_XYScopeSpace.GetFlag() ) return false;
	mark.assign(m_dem->m_XYScopeMark.pScopeData, m_dem->m_XYScopeMark.pScopeData + 512);
	space.assign(m_dem->m_XYScopeSpace.pScopeData, m_dem->m_XYScopeSpace.pScopeData + 512);
	m_dem->m_XYScope = 0;
	m_dem->m_XYScopeMark.Collect(512);
	m_dem->m_XYScopeSpace.Collect(512);
	m_dem->m_XYScope = 1;
	return true;
}

// ===========================================================================
// AFC (port of TSound::DoAFC)
// ===========================================================================
void Engine::setAfcParams(int fixShift, int squelch, double time, double sweep)
{
	QMutexLocker l(&m_mutex);
	sys.m_FixShift = fixShift;
	sys.m_AFCSQ = squelch;
	sys.m_AFCTime = time;
	sys.m_AFCSweep = sweep;
}

bool Engine::doAfc()
{
	QMutexLocker lock(&m_mutex);
	if( !sys.m_AFC ) return false;
	if( sys.m_echo != 2 && m_tx ) return false;

	double mfq = m_dem->GetMarkFreq();
	double sfq = m_dem->GetSpaceFreq();
	double sft = sfq - mfq;
	const double sk = sft * 0.5;
	const int nb = (sft < 50.0) ? 1 : 0;
	const int xm = int(mfq * (FFT_SIZE / SampFreq));
	const int xs = int(sfq * (FFT_SIZE / SampFreq));
	const int xc = int((xs - xm) * sys.m_AFCSweep);
	int xe = int(xc * 1.2);
	if( nb ) xe = int(80 * (FFT_SIZE / SampFreq));
	int n = 0;
	int avg = 0;
	int x, xx, d;
	const int *fft = m_fft.m_fft;

	int max1H = -INT_MAX, m1H = 0;
	for( x = 0; x < xc; x++ ){
		xx = xm + x;
		if( xx >= 0 && xx < m_fftWindow ){
			d = fft[xx];
			if( max1H < d ){ max1H = d; m1H = xx; }
			else if( d + kAfcPeakDown < max1H ) break;
		}
	}
	int max1L = -INT_MAX, m1L = 0;
	for( x = 0; x < xe; x++ ){
		xx = xm - x;
		if( xx >= 0 && xx < m_fftWindow ){
			d = fft[xx];
			avg += d; n++;
			if( max1L < d ){ max1L = d; m1L = xx; }
			else if( d + kAfcPeakDown < max1L ) break;
		}
	}
	int max2H = -INT_MAX, m2H = 0;
	for( x = 0; x < xe; x++ ){
		xx = xs + x;
		if( xx >= 0 && xx < m_fftWindow ){
			d = fft[xx];
			avg += d; n++;
			if( max2H < d ){ max2H = d; m2H = xx; }
			else if( d + kAfcPeakDown < max2H ) break;
		}
	}
	int max2L = -INT_MAX, m2L = 0;
	for( x = 0; x < xc; x++ ){
		xx = xs - x;
		if( xx >= 0 && xx < m_fftWindow ){
			d = fft[xx];
			if( max2L < d ){ max2L = d; m2L = xx; }
			else if( d + kAfcPeakDown < max2L ) break;
		}
	}
	if( n ) avg /= n;

	if( nb ){
		if( max1L < max1H ){ max1L = max1H; m1L = m1H; }
		if( max1L < max2L ){ max1L = max2L; m1L = m2L; }
		if( max1L < max2H ){ max1L = max2H; m1L = m2H; }
		max2L = max1L;
		m2L = m1L;
	}
	else if( m1H == m2L ){
		if( max2H > max1L ){
			max1L = max1H; m1L = m1H;
			max2L = max2H; m2L = m2H;
		}
	}
	else {
		if( max1H > max1L ){ m1L = m1H; max1L = max1H; }
		if( max2H > max2L ){ m2L = m2H; max2L = max2H; }
	}
	if( !nb ){
		static const double kSq[] = {1.0, 1.2, 1.5, 1.8};
		const double k = (sys.m_FFTGain >= 0 && sys.m_FFTGain <= 3) ? kSq[sys.m_FFTGain] : 0.5;
		if( (max1L - avg) < sys.m_AFCSQ * k || (max2L - avg) < sys.m_AFCSQ * k ) return false;
	}
	else if( (max1L - avg) < sys.m_AFCSQ * 0.5 ){
		return false;
	}
	const int ns = m2L - m1L;
	if( !nb && ns < int(140.0 * (FFT_SIZE / SampFreq)) ) return false;
	if( ns > int(1500.0 * (FFT_SIZE / SampFreq)) ) return false;

	const int os = int(sft * (FFT_SIZE / SampFreq));
	double nmfq = m1L * (SampFreq / FFT_SIZE);
	double nsfq = m2L * (SampFreq / FFT_SIZE);
	const int ds = abs(ns - os);

	auto apply = [&](double m, double s) {
		m_dem->AFCMarkFreq(m);
		m_dem->AFCSpaceFreq(s);
		if( m_bpfAfc && m_dem->GetAFCMarkFreq() == m && m_dem->GetAFCSpaceFreq() == s ){
			MakeFilter(m_hBpf, m_bpfTap, ffBPF, SampFreq, m_dem->GetMarkFreq() - m_bpfFw, m_dem->GetSpaceFreq() + m_bpfFw, 60, 1.0);
			m_lms.SetWindow(m_dem->GetMarkFreq(), m_dem->GetSpaceFreq());
		}
		emit tuningChanged(m_dem->GetMarkFreq(), m_dem->GetSpaceFreq());
	};
	auto snapShift = [](double s) {
		if( s > 230.0 ) return 240.0;
		if( s > 210.0 ) return 220.0;
		if( s > 185.0 ) return 200.0;
		return 170.0;
	};

	switch( sys.m_FixShift ){
		case 0:		// Free
			if( nb ) goto fixed;
			if( ds <= os * 1.2 && (nsfq - nmfq) >= 15.0 ){
				if( fabs(nmfq - mfq) >= 2.0 ) mfq += (nmfq - mfq) / sys.m_AFCTime;
				if( fabs(nsfq - sfq) >= 2.0 ) sfq += (nsfq - sfq) / sys.m_AFCTime;
				mfq = double(int(mfq + 0.5));
				sfq = double(int(sfq + 0.5));
				if( mfq < MARKL ) mfq = MARKL;
				if( sfq > SPACEH ) sfq = SPACEH;
				apply(mfq, sfq);
				return true;
			}
			break;
		case 1:		// Fixed shift
		fixed:
			if( nb ){
				const double fq = (nmfq + nsfq) / 2;
				nmfq = fq - sft / 2;
				nsfq = fq + sft / 2;
			}
			if( (ds <= os * 0.25 || nb) && (nsfq - nmfq) >= 15.0 ){
				const double cfq = (nmfq + nsfq) / 2.0;
				nmfq = cfq - sk;
				if( fabs(nmfq - mfq) >= 2.0 ){
					if( nb && fabs(nmfq - mfq) < 10.0 ) mfq += (nmfq - mfq) / (sys.m_AFCTime * 4.0);
					else mfq += (nmfq - mfq) / sys.m_AFCTime;
					mfq = double(int(mfq + 0.5));
					if( mfq < MARKL ) mfq = MARKL;
					if( mfq + sft > SPACEH ) mfq = SPACEH - sft;
					sfq = mfq + sft;
					apply(mfq, sfq);
					return true;
				}
			}
			break;
		case 2:		// HAM
			if( nb ) goto fixed;
			if( ns >= 140.0 * FFT_SIZE / SampFreq && ns <= 260.0 * FFT_SIZE / SampFreq ){
				sft = snapShift(nsfq - nmfq);
				nsfq = nmfq + sft;
				if( fabs(nmfq - mfq) >= 2.0 ) mfq += (nmfq - mfq) / sys.m_AFCTime;
				if( fabs(nsfq - sfq) >= 2.0 ) sfq += (nsfq - sfq) / sys.m_AFCTime;
				mfq = double(int(mfq + 0.5));
				sfq = double(int(sfq + 0.5));
				if( mfq < MARKL ) mfq = MARKL;
				if( sfq > SPACEH ) sfq = SPACEH;
				apply(mfq, sfq);
				return true;
			}
			break;
		case 3:		// FSK
			if( ns >= 140.0 * FFT_SIZE / SampFreq && ns <= 260.0 * FFT_SIZE / SampFreq ){
				sft = snapShift(nsfq - nmfq);
				nsfq = nmfq + sft;
				if( fabs(nmfq - mfq) >= 2.0 ) nmfq = mfq + (nmfq - mfq) / sys.m_AFCTime;
				if( fabs(nsfq - sfq) >= 2.0 ) nsfq = sfq + (nsfq - sfq) / sys.m_AFCTime;
				sft = nsfq - nmfq;
				if( sft < 175 ) sft = 170;
				if( sft > 195 ) sft = 200;
				sft = double(int(sft * 0.5 + 0.5));
				nmfq = ((sfq + mfq) * 0.5) - sft;
				nsfq = ((sfq + mfq) * 0.5) + sft;
				mfq = double(int(nmfq + 0.5));
				sfq = double(int(nsfq + 0.5));
				if( mfq < MARKL ) mfq = MARKL;
				if( sfq > SPACEH ) sfq = SPACEH;
				apply(mfq, sfq);
				return true;
			}
			break;
	}
	return false;
}

// ===========================================================================
// sound files
// ===========================================================================
bool Engine::playFile(const QString &path, QString *error)
{
	QMutexLocker l(&m_mutex);
	if( m_tx ){
		if( error ) *error = tr("Cannot play a file while transmitting");
		return false;
	}
	m_wav.close();
	if( !m_wav.openPlay(path, SampFreq, error) ) return false;
	m_dem->ClearRXBuf();
	return true;
}

bool Engine::recordFile(const QString &path, QString *error)
{
	QMutexLocker l(&m_mutex);
	m_wav.close();
	return m_wav.openRecord(path, SampFreq, error);
}

void Engine::stopFile()
{
	QMutexLocker l(&m_mutex);
	m_wav.close();
}

double Engine::filePosition() const
{
	QMutexLocker l(&m_mutex);
	if( m_wav.mode() != WavFile::Play || m_wav.length() == 0 ) return 0.0;
	return double(m_wav.position()) / double(m_wav.length());
}

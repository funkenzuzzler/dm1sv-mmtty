// Engine: the real-time core of MMTTY.
//
// This is the Qt re-implementation of the original TSound thread
// (Sound.cpp / Wave.cpp / Comm.cpp).  It owns the demodulator, the modulator
// and the sound / PTT devices, runs in a dedicated worker thread and talks to
// the GUI through thread-safe accessors and queued signals.
//
// Pacing of the processing loop (one "block" = SampSize samples):
//   * receiving          - paced by the sound card input
//   * transmitting       - paced by free space in the sound card output buffer
//                          (or by input when the receiver stays open, echo == 2)
//   * file play / no I/O - paced by a software clock ("pseudo run" of the original)
#ifndef ENGINE_H
#define ENGINE_H

#include <QByteArray>
#include <QElapsedTimer>
#include <functional>
#include <QObject>
#include <QRecursiveMutex>
#include <QString>
#include <QThread>
#include <QTimer>
#include <memory>
#include <vector>

#include "AudioConvert.h"
#include "FskKeyer.h"
#include "PttPort.h"
#include "Rtty.h"
#include "Fft.h"
#include "WavFile.h"

class QAudioSource;
class QAudioSink;
class QIODevice;

class Engine : public QObject
{
	Q_OBJECT
public:
	struct Config {
		double     sampleRate   = 11025.0;  // DSP sampling frequency (SampFreq); applied at construction only
		double     txOffset     = 0.0;      // TX clock correction in Hz
		QByteArray inputDevice;             // QAudioDevice::id(), empty = system default
		QByteArray outputDevice;
		int        inputChannel = 0;        // 0 = left / mono, 1 = right
		int        fifoTxBlocks = 4;        // output buffer depth in blocks
		bool       nullAudio    = false;    // no sound card at all (testing / TX-loopback only)
		bool       realtime     = true;     // false: null audio runs as fast as possible (tests)
		PttPort::Config ptt;                // PTT lines and the optional software-FSK line
		// Test hook: called from the keying thread for every FSK line change (mark, steady-clock ns).
		// When set, FSK keying is active even without a serial port.
		std::function<void(bool, qint64)> fskTap;
	};

	struct Spectrum {
		std::vector<int> bins;      // 0 .. window
		int    window = 0;          // number of valid bins
		double low = 0;             // frequency at the left edge of the scope
		int    fm = 0;              // number of bins shown
		double mark = 0, space = 0;
		double notch1 = 0, notch2 = 0;
		bool   lostSound = false;
		bool   overflow = false;
		int    fftGain = 0;
		std::vector<int> raw;       // the complete FFT result (2048 entries); used by the remote API
	};

	explicit Engine(const Config &cfg, QObject *parent = nullptr);
	~Engine() override;

	// ---- lifecycle ----------------------------------------------------
	void start();					// starts the worker thread and opens the devices
	void stop();					// stops the worker thread (blocking)
	void applyConfig(const Config &cfg);	// re-open devices / PTT port (thread-safe)
	const Config &config() const { return m_cfg; }
	bool running() const { return m_started; }

	// ---- direct DSP access: always hold lock() while using these -----------
	QRecursiveMutex &lock() { return m_mutex; }
	CFSKDEM &dem() { return *m_dem; }
	CFSKMOD &mod() { return *m_mod; }
	CRTTY   &rtty() { return m_rtty; }
	CLMS    &lms()  { return m_lms; }
	int blockSize() const { return m_block; }
	bool fskActive() const { return m_fskOn; }		// transmit by keying a serial line instead of audio

	// ---- frequency / mode control (mirrors the original UI actions) --------
	void setRxFrequencies(double mark, double space);	// tunes RX (and TX when idle)
	void setBaudRate(double baud);
	void setReverse(bool rev);
	void setBitFormat(int dataBits, int stopSel, int parity);	// stopSel: 0=1, 1=1.5, 2=2 stop bits
	void setDemodType(int type);							// 0 IIR, 1 FIR, 2 PLL, 3 FFT
	void setBandpass(bool on);
	void setNotch(bool on);
	void setNotchFrequency(double f);
	void setTest(bool on);
	bool testMode() const { return m_test; }
	void setAtc(bool on);
	void setSquelch(bool on, double level);			// level in the 0..1024 scale of the level meter
	void setUos(bool rxUos, bool txUos);			// unshift on space
	void setTxNet(bool net, bool fixShift, double defMark, double defShift);

	struct Level {
		double avg = 0;			// demodulator signal quality (0..1024 scale)
		double sqLevel = 0;		// squelch threshold (same scale)
		bool   limiter = false;	// limiter active: avg is 10x scaled
		bool   open = false;	// squelch open
		bool   sqOn = false;
	};
	Level signalLevel() const;

	// ---- TX ------------------------------------------------------------
	bool isTx() const { return m_tx; }
	bool isTxRequested() const { return m_reqTx; }
	bool txIdle() const;							// everything sent, modulator idle
	int  txBufferCount() const;
	void requestTx(bool carrierFirst);				// start transmitting (PTT on)
	void requestRx(bool immediate);					// back to receive; !immediate waits until the audio is out
	void putText(const QByteArray &ascii);			// text with the MMTTY control characters (_ ~ [ ] %L %F)
	void putRawCode(int code);						// one character code, no ASCII -> Baudot conversion
	void clearTxBuffer();
	bool fadeOutTx();								// true when the carrier ramp was still on (call again until false, then requestRx)
	void setDiddleTimer(int n);						// -1 off, 0 on, n = on after n samples
	void setTxEcho(int echo);						// 0 direct, 1 sound loopback, 2 external

	// ---- Scopes ----------------------------------------------------------
	bool fetchSpectrum(Spectrum &out);				// true when a new FFT frame was available
	bool fetchXY(std::vector<double> &mark, std::vector<double> &space);
	void setXYEnabled(bool on);
	double screenFrequency(int x, int width) const;
	void setFftOptions(int gain, int resp, int range);	// gain 0-7, resp (averaging), range (0 auto, 1..5)
	int  fftRange() const { return m_fftFw; }

	// ---- AFC ---------------------------------------------------------------
	void setAfc(bool on) { sys.m_AFC = on ? 1 : 0; }
	bool doAfc();									// call from the GUI timer; true when tuning moved
	void setAfcParams(int fixShift, int squelch, double time, double sweep);

	// ---- sound files -------------------------------------------------------
	bool playFile(const QString &path, QString *error = nullptr);
	bool recordFile(const QString &path, QString *error = nullptr);
	void stopFile();
	int  fileMode() const { return m_wav.mode(); }
	void pauseFile(bool p) { QMutexLocker l(&m_mutex); m_wav.setPaused(p); }
	double filePosition() const;						// 0..1 for playback

	static QList<QPair<QString, QByteArray>> inputDevices();
	static QList<QPair<QString, QByteArray>> outputDevices();

	// ---- raw receive codes (remote API "-n": no Baudot -> ASCII translation) --------
	void setRawRx(bool on);

signals:
	void rxText(const QString &text, bool whileTx);
	void rawChar(int code);							// every received character code, only while setRawRx(true)
	void figChanged(bool fig);
	void txStateChanged(bool tx);
	void tuningChanged(double mark, double space);	// AFC / TX net switching moved the tuning
	void message(const QString &text);				// device errors etc.
	void fileFinished();

private slots:
	void init();
	void shutdown();
	void tick();
	void reconfigure(const Config &cfg);

private:
	// device handling (worker thread only)
	bool openInput();
	void closeInput();
	bool openOutput();
	void closeOutput();
	void pullInput();
	void writeOutput(const double *blk, int n);
	bool outputHasRoom() const;
	bool clockCredit();

	void processBlock();
	void deliverRx();
	void switchToTx();
	void switchToRx();
	void updateRev();
	void updateLimit();
	void updateNet();
	void computeScopeRange(double &low, int &fm) const;
	void setPttLine(bool tx);
	void setupFsk();

	Config  m_cfg;
	QThread m_thread;
	mutable QRecursiveMutex m_mutex;

	// DSP objects (heap: the original classes rely on zeroed allocation)
	std::unique_ptr<CFSKDEM> m_dem;
	std::unique_ptr<CFSKMOD> m_mod;
	CRTTY  m_rtty;
	CLMS   m_lms;
	CNoise m_noise;
	CFFT   m_fft;
	double m_zBpf[TAPMAX + 1];
	double m_hBpf[TAPMAX + 1];

	int    m_block = 1024;
	int    m_fftWindow = 0;
	std::vector<double> m_buf;

	// state shared with the GUI (guarded by m_mutex)
	bool   m_tx = false;
	bool   m_reqTx = false;
	bool   m_wantRx = false;		// graceful return to RX requested
	bool   m_abortOutput = false;
	bool   m_test = false;
	bool   m_bpf = false;
	bool   m_bpfWasOn = false;
	bool   m_lmsbpf = false;
	int    m_bpfTap = 56;
	double m_bpfFw = 100.0;
	bool   m_bpfAfc = true;
	int    m_fftSw = 1;
	int    m_fftFw = 0;
	bool   m_lostSound = false;
	bool   m_figLast = false;
	bool   m_rawRx = false;
	bool   m_xyEnabled = false;
	double m_rxMark = 2125, m_rxSpace = 2295;
	int    m_fftMax = 128, m_fftSumMax = 256;

	// audio devices (worker thread only)
	QAudioSource *m_source = nullptr;
	QAudioSink   *m_sink = nullptr;
	QIODevice    *m_inDev = nullptr;
	QIODevice    *m_outDev = nullptr;
	QAudioFormat  m_inFmt, m_outFmt;
	AudioConvert::Resampler m_inRs, m_outRs;
	std::vector<double> m_inQueue;
	std::vector<double> m_scratch;
	QElapsedTimer m_clock;
	double        m_clockCredit = 0;
	qint64        m_lastClockNs = 0;
	qint64        m_audioEndNs = 0;		// when everything written to the sink will have been played
	QElapsedTimer m_mono;
	PttPort       m_ptt;
	QTimer       *m_timer = nullptr;
	bool          m_started = false;
	int           m_errCount = 0;

	WavFile m_wav;

	// software FSK (worker thread, except where noted)
	FskKeyer m_keyer;
	bool     m_fskOn = false;		// keyer running and a line (or test tap) available
	bool     m_fskTx = false;		// the current transmission is keyed on the FSK line
	bool     m_fskLast = true;		// last line state handed to the keyer (true = mark)
	double   m_fskT = 0;			// steady-clock time (ns) of the next modulator sample
	qint64   m_fskEndNs = 0;		// time at which everything scheduled has been sent
};

#endif

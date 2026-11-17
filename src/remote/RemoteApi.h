// Engine remote mode: lets another program (N1MM Logger+, MixW, ...) drive MMTTY as its RTTY engine.
//
// RemoteApi implements the protocol on top of the MainWindow / Engine.  Everything that depends on the
// operating system (window messages, shared memory, window handles) is behind RemoteHost, which only the
// Windows build implements (RemoteWin).  The protocol logic itself is portable and covered by remote_test.
#ifndef REMOTEAPI_H
#define REMOTEAPI_H

#include <QObject>
#include <QTimer>
#include <memory>

#include "Engine.h"
#include "Rtty.h"
#include "RemoteProtocol.h"

class MainWindow;

class RemoteHost
{
public:
	virtual ~RemoteHost() = default;
	// PostApp(): posts to the application window, nothing while its handle is unknown.
	virtual void postToApp(unsigned message, qint64 lParam) = 0;
	// PostMessage(APP_HANDLE, ...): also while the handle is unknown (the message is then broadcast).
	virtual void postRaw(unsigned message, qint64 lParam) = 0;
	virtual bool appKnown() const = 0;
	virtual void setApp(quintptr handle) = 0;
	virtual quintptr windowHandle() const = 0;		// handle of the MMTTY window (TXM_HANDLE)
	virtual unsigned threadId() const = 0;			// TXM_THREAD
	virtual remote::ComArray *openShared(const QString &name) = 0;	// null when the application did not create it
	virtual void activateApp() = 0;					// bring the application window to the front
	virtual void enableApp(bool enable) = 0;		// disable / enable the application window (modal setup)
};

class RemoteApi : public QObject
{
	Q_OBJECT
public:
	RemoteApi(MainWindow *w, RemoteHost *host, const remote::StartOptions &opt, QObject *parent = nullptr);

	void start();									// startup notification + shared memory + timer
	void handle(unsigned command, qint64 lParam);	// one RXM_* message
	bool startHidden() const;						// -m / -d: the window stays hidden until RXM_SHOW
	unsigned flags() const { return m_flags; }
	remote::ComArray *shared() const { return m_shared; }

public slots:
	void tick();									// every 100 ms (handshake retries, time-out, state changes)

private slots:
	void onSpectrum(const Engine::Spectrum &sp);
	void onXY(const std::vector<double> &x, const std::vector<double> &y, bool limiter);
	void onLevel(const Engine::Level &lv);
	void onPtt(bool tx);
	void onRxText(const QString &text, bool whileTx);
	void onRawChar(int code);
	void onFig(bool fig);
	void onSettingsOpen(bool open);
	void onClosing();

private:
	void post(unsigned message, qint64 lParam);
	void quitApp();
	int  echoMode() const;
	void initStat();
	void stat();									// RemoteStat(): post what changed
	void freq();									// RemoteFreq()
	uint32_t switchWord() const;
	uint32_t viewWord() const;
	void applySwitch(uint32_t d);
	void applyView(uint32_t d);
	void firstFileMapping();
	void updateComarray();
	void pushKey(char key);
	void ham();
	void cmdPtt(int mode);
	void cmdChar(int c);
	void cmdSuspend(bool suspend);
	void cmdProfile(uint32_t flag);
	void showSetup(bool disableApp);
	void setTuning(double mark, double space);

	MainWindow *m_w;
	RemoteHost *m_host;
	remote::StartOptions m_opt;
	unsigned m_flags;
	QTimer m_timer;
	remote::ComArray *m_shared = nullptr;
	int m_timeout = 0;								// remaining 100 ms ticks of the start-up wait; 0 = none
	bool m_setupOpen = false;
	bool m_wordWrap = false;
	int m_xySize = 0, m_xyQuality = 0;
	int m_lostHold = 0, m_overHold = 0;				// lost sound / overflow flags stay up for a second
	int m_fskLine = 2;								// line used when RXM_PTTFSK enables FSK
	CRTTY m_check;									// character validity test for the key buffer (its TX state is private)

	// last values posted (-1 = nothing posted yet)
	qint64 m_cSwitch = -1, m_cView = -1, m_cBaud = -1, m_cHeight = -1, m_cReso = -1, m_cLpf = -1;
	qint64 m_cMark = -1, m_cSpace = -1, m_cNotch = -1, m_cDefShift = -1;
};

#endif

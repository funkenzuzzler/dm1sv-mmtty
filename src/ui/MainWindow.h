// Main window: the Qt replacement of the original TMmttyWd form.
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QSettings>
#include <QTimer>
#include <memory>
#include <vector>

#include "AppSettings.h"
#include "Engine.h"
#include "Macro.h"
#include "QsoLog.h"

class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QMenu;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QToolButton;
class LevelBar;
class RemoteApi;
class SpectrumView;
class WaterfallView;
class XYView;

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	struct Options {
		bool    nullAudio = false;	// do not open any sound card
		bool    freeRun = false;	// null audio runs as fast as possible (automated tests)
		QString iniPath;			// settings file; empty = per-user default location
		bool    remote = false;		// engine remote mode: keeps its own window geometry in the settings file
	};

	explicit MainWindow(const Options &opt, QWidget *parent = nullptr);
	~MainWindow() override;

	Engine *engine() { return m_engine.get(); }
	const AppSettings &settings() const { return m_s; }

	// ---- scripting / test access (same code paths as the buttons) ----
	void pressMacro(int index);				// 0..15
	void pressTx();
	void pressTxOff();
	void typeText(const QString &text);		// appends to the TX FIFO as if typed
	bool isTxDown() const { return m_txDown; }
	QString rxText() const;
	void applyRemoteLayout(bool controls, bool menu);	// engine remote mode: hide logging / macro / typing parts

protected:
	void closeEvent(QCloseEvent *e) override;

signals:
	// Data and events for the remote API module (RemoteApi); nothing else listens to these.
	void spectrumUpdated(const Engine::Spectrum &sp);
	void xyUpdated(const std::vector<double> &x, const std::vector<double> &y, bool limiter);
	void levelUpdated(const Engine::Level &lv);
	void pttEvent(bool tx);					// transmit requested (true) / receive again (false)
	void closing();							// the window is being closed
	void settingsOpen(bool open);			// the settings dialog was opened / closed

private slots:
	void onTimer();
	void onRxText(const QString &text, bool whileTx);
	void onTxState(bool tx);
	void onTuning(double mark, double space);
	void onFig(bool fig);
	void onMessage(const QString &text);
	void onFileFinished();
	void onTuningEdited();
	void onScopeClicked(int x, int width, Qt::MouseButton b);
	void openSettings();
	void logQso();

private:
	friend class RemoteApi;

	void buildUi();
	void buildMenus();
	void setupToggle(QToolButton *b, const QString &text, const QString &tip, bool on);
	void pushSettings();
	void updateTuningCombos();
	void updateMacroButtons();
	void updateTxUi();
	void updateMenuState();
	void showStatus(const QString &s);

	// ---- transmit state machine (ports of ToTX / ToRX / XMIT / OutputStr / MacBtnExec) ----
	void toTx(const QByteArray &firstText, bool haveText);
	void toRx(bool immediate);
	void outputStr(const QByteArray &str);
	void macBtnExec(int n);
	void writeFifo(const QString &text);
	int  fifoGet();						// next character of the TX FIFO, -1 when empty
	int  fifoLen() const;
	void pumpTx();
	void txButtonClicked();
	MacroContext macroContext() const;

	void saveSettings();
	void applyEngineConfig();				// re-open devices / PTT port after the settings changed
	QString windowKey() const;				// settings group of the window geometry
	void reportPtt(bool tx);
	bool fifoSendReady() const;				// "way to send" of the remote API: character, word or line

	Options m_opt;
	std::unique_ptr<QSettings> m_ini;
	AppSettings m_s;
	std::unique_ptr<Engine> m_engine;
	QsoLog m_log;
	QTimer m_timer;

	// widgets
	QWidget *m_row1Box = nullptr, *m_qsoBox = nullptr, *m_txBox = nullptr, *m_macroBox = nullptr;
	QMenu *m_menuFile = nullptr, *m_menuTools = nullptr, *m_menuHelp = nullptr;
	QAction *m_actSettings = nullptr;
	QPushButton *m_btnTx = nullptr, *m_btnTxOff = nullptr;
	QPushButton *m_macroBtn[AppSettings::kMacros] = {};
	QPushButton *m_inBtn[AppSettings::kInputButtons] = {};
	QToolButton *m_tAfc = nullptr, *m_tNet = nullptr, *m_tRev = nullptr, *m_tUos = nullptr,
	            *m_tBpf = nullptr, *m_tNotch = nullptr, *m_tAtc = nullptr, *m_tSq = nullptr;
	QComboBox *m_markCombo = nullptr, *m_shiftCombo = nullptr, *m_baudCombo = nullptr, *m_demCombo = nullptr;
	SpectrumView *m_spectrum = nullptr;
	WaterfallView *m_water = nullptr;
	XYView *m_xy = nullptr;
	LevelBar *m_level = nullptr;
	QPlainTextEdit *m_rxView = nullptr, *m_txEdit = nullptr;
	QLineEdit *m_hisCall = nullptr, *m_hisName = nullptr, *m_hisQth = nullptr, *m_hisRst = nullptr, *m_myRst = nullptr;
	QLabel *m_stTx = nullptr, *m_stFig = nullptr, *m_stTune = nullptr;
	QSplitter *m_vsplit = nullptr;
	QAction *m_actWater = nullptr, *m_actXY = nullptr, *m_actTest = nullptr, *m_actStopFile = nullptr;
	QActionGroup *m_gainGroup = nullptr, *m_rangeGroup = nullptr;

	// transmit state (original: SBTX->Down, m_Macro, m_MacroStat, m_ReqRecv, m_NowStr, m_FifoOutTimer)
	bool       m_txDown = false;
	bool       m_pendingTx = false;
	bool       m_reqRecv = false;
	int        m_macro = 0;
	int        m_macroStat = 0;
	int        m_fifoPause = 0;
	QByteArray m_nowStr;
	bool       m_pendingCr = false;
	int        m_disEvent = 0;

	// set by the remote API (defaults leave the normal behaviour unchanged)
	int            m_txDelayMs = 0;			// wait between the PTT event and the start of the audio
	int            m_sendWay = 0;			// 0 character, 1 word, 2 line
	int            m_pttTimerSec = 0;		// 0 = off
	QElapsedTimer  m_pttClock;
	bool           m_pttReported = false;
	bool           m_portsLocked = false;	// start option -p: the port settings cannot be changed
	bool           m_viaTx = false;			// remote "key buffer" mode: unsent text is dropped when going back to receive
};

#endif

#include "MainWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QTextBlock>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

#include "Macro.h"
#include "Scopes.h"
#include "SettingsDialog.h"
#include "compat.h"
#include "ui_MainWindow.h"

namespace {

const char kTestData[] =
	"\r\nRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRY\r\n"
	"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n"
	"-?:$!&'().,;/\"";

QString num(double v)
{
	return v == double(qint64(v)) ? QString::number(qint64(v)) : QString::number(v, 'f', 2);
}

} // namespace

// ===========================================================================
// construction
// ===========================================================================
MainWindow::MainWindow(const Options &opt, QWidget *parent)
	: QMainWindow(parent), m_opt(opt), ui(new Ui::MainWindow)
{
	if( opt.iniPath.isEmpty() )
		m_ini.reset(new QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("mmtty-qt"), QStringLiteral("mmtty")));
	else
		m_ini.reset(new QSettings(opt.iniPath, QSettings::IniFormat));
	m_s.load(*m_ini);

	Engine::Config cfg = m_s.engineConfig();
	cfg.nullAudio = opt.nullAudio;
	cfg.realtime = !opt.freeRun;
	m_engine.reset(new Engine(cfg));

	ui->setupUi(this);
	setupUi();

	connect(m_engine.get(), &Engine::rxText, this, &MainWindow::onRxText);
	connect(m_engine.get(), &Engine::txStateChanged, this, &MainWindow::onTxState);
	connect(m_engine.get(), &Engine::tuningChanged, this, &MainWindow::onTuning);
	connect(m_engine.get(), &Engine::figChanged, this, &MainWindow::onFig);
	connect(m_engine.get(), &Engine::message, this, &MainWindow::onMessage);
	connect(m_engine.get(), &Engine::fileFinished, this, &MainWindow::onFileFinished);

	pushSettings();
	updateTuningCombos();
	updateMacroButtons();
	updateTxUi();
	updateMenuState();
	m_engine->start();

	m_timer.setInterval(40);
	connect(&m_timer, &QTimer::timeout, this, &MainWindow::onTimer);
	m_timer.start();

	if( m_ini->contains(windowKey() + QStringLiteral("/geometry")) ){
		restoreGeometry(m_ini->value(windowKey() + QStringLiteral("/geometry")).toByteArray());
		ui->vsplit->restoreState(m_ini->value(windowKey() + QStringLiteral("/split")).toByteArray());
	}
	ui->txEdit->setFocus();
}

MainWindow::~MainWindow()
{
	m_timer.stop();
	if( m_engine ) m_engine->stop();
}

void MainWindow::setupUi()
{
	// ---- row 1: toggles, tuning, demodulator ----------------------------------------
	auto toggle = [&](QToolButton *b, std::function<void(bool)> fn) {
		connect(b, &QToolButton::clicked, this, [fn](bool c) { fn(c); });
	};
	toggle(ui->tAfc, [this](bool on) {
		m_s.afc = on;
		m_engine->setAfc(on);
	});
	toggle(ui->tNet, [this](bool on) {
		m_s.txNet = on;
		m_engine->setTxNet(on, m_s.txFixShift, m_s.defMark, m_s.defShift);
	});
	toggle(ui->tRev, [this](bool on) {
		m_s.reverse = on;
		m_engine->setReverse(on);
	});
	toggle(ui->tUos, [this](bool on) {
		m_s.uos = on;
		m_engine->setUos(on, m_s.txUos);
	});
	toggle(ui->tBpf, [this](bool on) {
		m_s.bpf = on;
		m_engine->setBandpass(on);
	});
	toggle(ui->tNotch, [this](bool on) {
		m_s.notch = on;
		m_engine->setNotch(on);
	});
	toggle(ui->tAtc, [this](bool on) {
		m_s.atc = on;
		m_engine->setAtc(on);
	});
	toggle(ui->tSq, [this](bool on) {
		m_s.squelch = on;
		m_engine->setSquelch(on, m_s.squelchLevel);
	});

	auto setupCombo = [&](QComboBox *c, const QStringList &items) {
		c->addItems(items);
		connect(c, QOverload<int>::of(&QComboBox::activated), this, [this] { onTuningEdited(); });
		connect(c->lineEdit(), &QLineEdit::editingFinished, this, &MainWindow::onTuningEdited);
	};
	setupCombo(ui->markCombo, m_s.markList);
	setupCombo(ui->shiftCombo, m_s.shiftList);
	setupCombo(ui->baudCombo, m_s.baudList);

	ui->demCombo->setCurrentIndex(m_s.demType);
	connect(ui->demCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
		m_s.demType = i;
		m_engine->setDemodType(i);
	});

	connect(ui->level, &LevelBar::squelchLevelChanged, this, [this](double lvl) {
		m_s.squelchLevel = lvl;
		m_engine->setSquelch(m_s.squelch, lvl);
	});

	// ---- scopes, text views -------------------------------------------------------------
	connect(ui->spectrum, &SpectrumView::clicked, this, &MainWindow::onScopeClicked);
	connect(ui->water, &WaterfallView::clicked, this, &MainWindow::onScopeClicked);
	ui->rxView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	ui->txEdit->setFont(ui->rxView->font());

	// ---- QSO fields, TX controls and macros ----------------------------------------------
	connect(ui->logBtn, &QPushButton::clicked, this, &MainWindow::logQso);
	connect(ui->btnTx, &QPushButton::clicked, this, &MainWindow::txButtonClicked);
	connect(ui->btnTxOff, &QPushButton::clicked, this, &MainWindow::pressTxOff);
	for( int i = 0; i < AppSettings::kInputButtons; i++ ){
		m_inBtn[i] = findChild<QPushButton *>(QStringLiteral("inBtn%1").arg(i + 1));
		connect(m_inBtn[i], &QPushButton::clicked, this, [this, i] { writeFifo(m_s.inButton[i]); });
		new QShortcut(QKeySequence(int(Qt::ALT) | (int(Qt::Key_1) + i)), this, [this, i] { writeFifo(m_s.inButton[i]); });
	}
	for( int i = 0; i < AppSettings::kMacros; i++ ){
		m_macroBtn[i] = findChild<QPushButton *>(QStringLiteral("macroBtn%1").arg(i + 1));
		connect(m_macroBtn[i], &QPushButton::clicked, this, [this, i] { macBtnExec(i); });
		const int key = i < 12 ? int(Qt::Key_F1) + i : (int(Qt::SHIFT) | (int(Qt::Key_F1) + (i - 12)));
		new QShortcut(QKeySequence(key), this, [this, i] { macBtnExec(i); });
	}
	new QShortcut(QKeySequence(int(Qt::CTRL) | int(Qt::Key_T)), this, [this] { txButtonClicked(); });
	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { pressTxOff(); });

	// ---- status bar ---------------------------------------------------------------------
	m_stTx = new QLabel(QStringLiteral("RX"));
	m_stTx->setMinimumWidth(30);
	m_stFig = new QLabel(QStringLiteral("LTR"));
	m_stFig->setMinimumWidth(30);
	m_stTune = new QLabel;
	statusBar()->addPermanentWidget(m_stTune);
	statusBar()->addPermanentWidget(m_stFig);
	statusBar()->addPermanentWidget(m_stTx);

	ui->water->setVisible(m_s.showWaterfall);
	ui->xy->setVisible(m_s.showXY);

	// ---- menus --------------------------------------------------------------------------
	connect(ui->actionPlayWav, &QAction::triggered, this, [this] {
		const QString p = QFileDialog::getOpenFileName(this, tr("Play WAV file"), QString(), tr("WAV files (*.wav)"));
		if( p.isEmpty() ) return;
		QString err;
		if( !m_engine->playFile(p, &err) ) showStatus(err);
	});
	connect(ui->actionRecordWav, &QAction::triggered, this, [this] {
		const QString p = QFileDialog::getSaveFileName(this, tr("Record WAV file"), QString(), tr("WAV files (*.wav)"));
		if( p.isEmpty() ) return;
		QString err;
		if( !m_engine->recordFile(p, &err) ) showStatus(err);
	});
	connect(ui->actionStopFile, &QAction::triggered, this, [this] { m_engine->stopFile(); });
	ui->actionSettings->setShortcut(QKeySequence::Preferences);
	connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::openSettings);
	ui->actionQuit->setShortcut(QKeySequence::Quit);
	connect(ui->actionQuit, &QAction::triggered, this, [this] { close(); });

	connect(ui->actionWaterfall, &QAction::triggered, this, [this](bool on) {
		m_s.showWaterfall = on;
		ui->water->setVisible(on);
	});
	connect(ui->actionXY, &QAction::triggered, this, [this](bool on) {
		m_s.showXY = on;
		ui->xy->setVisible(on);
		m_engine->setXYEnabled(on);
	});

	// The actions of the two choice groups are declared in the .ui file in the order of their values.
	auto choiceGroup = [&](QActionGroup *grp, std::function<void(int)> fn) {
		int v = 0;
		for( QAction *a : grp->actions() ){
			a->setData(v);
			connect(a, &QAction::triggered, this, [fn, v] { fn(v); });
			v++;
		}
	};
	choiceGroup(ui->gainGroup, [this](int v) { m_s.fftGain = v; m_engine->setFftOptions(m_s.fftGain, m_s.fftResp, m_s.fftRange); });
	choiceGroup(ui->rangeGroup, [this](int v) { m_s.fftRange = v; m_engine->setFftOptions(m_s.fftGain, m_s.fftResp, m_s.fftRange); });

	connect(ui->actionTest, &QAction::triggered, this, [this](bool on) { m_engine->setTest(on); });
	connect(ui->actionLogQso, &QAction::triggered, this, &MainWindow::logQso);
	connect(ui->actionAbout, &QAction::triggered, this, [this] {
		QMessageBox::about(this, tr("About MMTTY"),
			tr("<b>MMTTY (Qt)</b><br>RTTY engine and sound-card modem.<br><br>"
			   "Qt port of MMTTY by Makoto Mori, JE3HHT.<br>The demodulator, modulator and AFC "
			   "algorithms are those of the original program."));
	});
}

// ===========================================================================
// settings
// ===========================================================================
void MainWindow::pushSettings()
{
	Engine &e = *m_engine;
	e.setTxEcho(m_s.echo);
	e.setRxFrequencies(m_s.mark, m_s.mark + m_s.shift);
	e.setBaudRate(m_s.baud);
	e.setReverse(m_s.reverse);
	e.setBitFormat(m_s.dataBits, m_s.stopSel, m_s.parity);
	e.setDemodType(m_s.demType);
	e.setUos(m_s.uos, m_s.txUos);
	e.setTxNet(m_s.txNet, m_s.txFixShift, m_s.defMark, m_s.defShift);
	e.setAfcParams(m_s.afcMode, m_s.afcSquelch, m_s.afcTime, m_s.afcSweep);
	e.setAfc(m_s.afc);
	e.setFftOptions(m_s.fftGain, m_s.fftResp, m_s.fftRange);
	e.setXYEnabled(m_s.showXY);
	e.setAtc(m_s.atc);
	e.setSquelch(m_s.squelch, m_s.squelchLevel);
	e.setBandpass(m_s.bpf);
	e.setNotch(m_s.notch);
}

void MainWindow::updateMenuState()
{
	ui->actionWaterfall->setChecked(m_s.showWaterfall);
	ui->actionXY->setChecked(m_s.showXY);
	for( QAction *a : ui->gainGroup->actions() ) a->setChecked(a->data().toInt() == m_s.fftGain);
	for( QAction *a : ui->rangeGroup->actions() ) a->setChecked(a->data().toInt() == m_s.fftRange);
	ui->tAfc->setChecked(m_s.afc);
	ui->tNet->setChecked(m_s.txNet);
	ui->tRev->setChecked(m_s.reverse);
	ui->tUos->setChecked(m_s.uos);
	ui->tBpf->setChecked(m_s.bpf);
	ui->tNotch->setChecked(m_s.notch);
	ui->tAtc->setChecked(m_s.atc);
	ui->tSq->setChecked(m_s.squelch);
	ui->demCombo->setCurrentIndex(m_s.demType);
	ui->water->setVisible(m_s.showWaterfall);
	ui->xy->setVisible(m_s.showXY);
}

void MainWindow::openSettings()
{
	struct Notify {
		MainWindow *w;
		explicit Notify(MainWindow *win) : w(win) { emit w->settingsOpen(true); }
		~Notify() { emit w->settingsOpen(false); }
	} notify(this);
	SettingsDialog dlg(m_s, this);
	if( dlg.exec() != QDialog::Accepted ) return;
	const double oldRate = m_s.sampleRate;
	const AppSettings before = m_s;
	dlg.apply(m_s);
	if( m_portsLocked ){			// start option -p: the user may not change the port settings
		m_s.pttPort = before.pttPort;
		m_s.pttLines = before.pttLines;
		m_s.pttInvert = before.pttInvert;
		m_s.fskLine = before.fskLine;
		m_s.fskInvert = before.fskInvert;
	}

	applyEngineConfig();
	pushSettings();
	updateMenuState();
	updateMacroButtons();
	if( oldRate != m_s.sampleRate ) showStatus(tr("The new sampling rate takes effect after a restart."));
	saveSettings();
}

// Engine remote mode (original UpdateRemort): no QSO log, macros or typing; the control panel and the
// menu are optional.  Only the settings dialog remains in the File menu.
void MainWindow::applyRemoteLayout(bool controls, bool menu)
{
	ui->qsoBox->hide();
	ui->txBox->hide();
	ui->macroBox->hide();
	ui->txEdit->hide();
	ui->row1Box->setVisible(controls);
	for( QAction *a : ui->menuFile->actions() ) a->setVisible(a == ui->actionSettings);
	ui->menuTools->menuAction()->setVisible(false);
	ui->menuHelp->menuAction()->setVisible(false);
	menuBar()->setVisible(menu);
	for( QShortcut *s : findChildren<QShortcut *>() ) s->setEnabled(false);		// no keyboard transmit control
}

void MainWindow::applyEngineConfig()
{
	if( !m_engine->running() ) return;		// suspended by the remote API: applied when it resumes
	Engine::Config c = m_s.engineConfig();
	c.nullAudio = m_opt.nullAudio;
	c.realtime = !m_opt.freeRun;
	c.sampleRate = m_engine->config().sampleRate;
	m_engine->applyConfig(c);
}

// The engine remote mode has its own window geometry, like the original MMTTY.INI entries of the control panel.
QString MainWindow::windowKey() const
{
	return m_opt.remote ? QStringLiteral("RemoteWindow") : QStringLiteral("Window");
}

void MainWindow::saveSettings()
{
	m_s.save(*m_ini);
	m_ini->setValue(windowKey() + QStringLiteral("/geometry"), saveGeometry());
	m_ini->setValue(windowKey() + QStringLiteral("/split"), ui->vsplit->saveState());
	m_ini->sync();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	emit closing();
	m_timer.stop();
	saveSettings();
	m_engine->stop();
	e->accept();
}

// ===========================================================================
// tuning
// ===========================================================================
void MainWindow::updateTuningCombos()
{
	m_disEvent++;
	ui->markCombo->setEditText(num(m_s.mark));
	ui->shiftCombo->setEditText(num(m_s.shift));
	ui->baudCombo->setEditText(num(m_s.baud));
	m_disEvent--;
	m_stTune->setText(tr("Mark %1 Hz   Space %2 Hz").arg(num(m_s.mark), num(m_s.mark + m_s.shift)));
}

void MainWindow::onTuningEdited()
{
	if( m_disEvent ) return;
	bool ok1, ok2, ok3;
	const double mark = ui->markCombo->currentText().toDouble(&ok1);
	const double shift = ui->shiftCombo->currentText().toDouble(&ok2);
	const double baud = ui->baudCombo->currentText().toDouble(&ok3);
	if( !ok1 || !ok2 || !ok3 || mark < MARKL || shift < 10 || mark + shift > SPACEH || baud < 10 || baud > 400 ){
		showStatus(tr("Invalid mark / shift / baud value"));
		updateTuningCombos();
		return;
	}
	m_s.mark = mark;
	m_s.shift = shift;
	if( baud != m_s.baud ){
		m_s.baud = baud;
		m_engine->setBaudRate(baud);
	}
	m_engine->setRxFrequencies(mark, mark + shift);
	updateTuningCombos();
}

void MainWindow::onTuning(double mark, double space)
{
	m_s.mark = mark;
	m_s.shift = space - mark;
	if( !ui->markCombo->lineEdit()->hasFocus() && !ui->shiftCombo->lineEdit()->hasFocus() ) updateTuningCombos();
	else m_stTune->setText(tr("Mark %1 Hz   Space %2 Hz").arg(num(mark), num(space)));
}

void MainWindow::onScopeClicked(int x, int width, Qt::MouseButton b)
{
	if( m_engine->isTx() && m_s.echo != 2 ) return;			// as in the original: no tuning while transmitting
	const double f = m_engine->screenFrequency(x, width);
	if( b == Qt::RightButton ){
		m_engine->setNotchFrequency(f);
		if( !ui->tNotch->isChecked() ){
			ui->tNotch->setChecked(true);
			m_s.notch = true;
			m_engine->setNotch(true);
		}
		return;
	}
	const double mfq = double(int(f + 0.5));
	if( qAbs(mfq - m_s.mark) < 2 ) return;
	const double sfq = mfq + m_s.shift;
	if( mfq < MARKL || sfq > SPACEH ) return;
	m_s.mark = mfq;
	m_engine->setRxFrequencies(mfq, sfq);
	updateTuningCombos();
}

// ===========================================================================
// periodic work: scopes, level, AFC and the transmit FIFO
// ===========================================================================
void MainWindow::onTimer()
{
	Engine::Spectrum sp;
	if( m_engine->fetchSpectrum(sp) ){
		const int fm = m_engine->fileMode();
		ui->spectrum->setOverlay(fm == WavFile::Play ? tr("Play") : (fm == WavFile::Record ? tr("Rec.") : QString()));
		ui->spectrum->setFrame(sp);
		emit spectrumUpdated(sp);
		if( m_s.showWaterfall ) ui->water->addFrame(sp);
		const bool menuOpen = ui->markCombo->view()->isVisible() || ui->shiftCombo->view()->isVisible();
		if( m_s.afc && !menuOpen ) m_engine->doAfc();		// emits tuningChanged when the tuning moved
	}
	const Engine::Level lv = m_engine->signalLevel();
	ui->level->setLevel(lv);
	emit levelUpdated(lv);
	if( m_s.showXY ){
		std::vector<double> x, y;
		if( m_engine->fetchXY(x, y) ){
			ui->xy->setData(x, y, lv.limiter, m_s.demType == 1);
			emit xyUpdated(x, y, lv.limiter);
		}
	}
	ui->actionStopFile->setEnabled(m_engine->fileMode() != WavFile::Closed);
	pumpTx();
}

void MainWindow::onRxText(const QString &text, bool whileTx)
{
	QString out;
	for( QChar c : text ){
		if( c == QLatin1Char('\r') ){ m_pendingCr = true; continue; }
		if( c == QLatin1Char('\n') ){ out += QLatin1Char('\n'); m_pendingCr = false; continue; }
		if( m_pendingCr ){ out += QLatin1Char('\n'); m_pendingCr = false; }
		if( c.unicode() >= 0x20 ) out += c;
	}
	if( out.isEmpty() ) return;
	QScrollBar *sb = ui->rxView->verticalScrollBar();
	const bool atEnd = sb->value() >= sb->maximum() - 2;
	QTextCursor cur(ui->rxView->document());
	cur.movePosition(QTextCursor::End);
	QTextCharFormat fmt;
	fmt.setForeground(whileTx ? QColor(200, 30, 30) : ui->rxView->palette().text().color());
	cur.insertText(out, fmt);
	if( atEnd ) sb->setValue(sb->maximum());
}

void MainWindow::onTxState(bool tx)
{
	m_stTx->setText(tx ? QStringLiteral("TX") : QStringLiteral("RX"));
	m_stTx->setStyleSheet(tx ? QStringLiteral("QLabel{background:#c62828;color:white;font-weight:bold;padding:0 4px;}") : QString());
	if( !tx ) reportPtt(false);
	if( !tx && m_txDown && !m_engine->isTxRequested() ){
		// the engine left transmit by itself (e.g. an output device error)
		m_txDown = false;
		m_reqRecv = false;
		m_macro = 0;
		m_macroStat = 0;
		updateTxUi();
	}
}

void MainWindow::onFig(bool fig) { m_stFig->setText(fig ? QStringLiteral("FIG") : QStringLiteral("LTR")); }
void MainWindow::onMessage(const QString &text) { showStatus(text); }
void MainWindow::onFileFinished() { showStatus(tr("Sound file finished")); }
void MainWindow::showStatus(const QString &s) { statusBar()->showMessage(s, 6000); }
QString MainWindow::rxText() const { return ui->rxView->toPlainText(); }

// ===========================================================================
// transmit state machine (ports of ToTX / ToRX / OutputStr / MacBtnExec / TimerTimer)
// ===========================================================================
MacroContext MainWindow::macroContext() const
{
	MacroContext c;
	c.myCall = m_s.call;
	c.hisCall = ui->hisCall->text().trimmed().toUpper();
	c.hisName = ui->hisName->text().trimmed();
	c.hisQth = ui->hisQth->text().trimmed();
	c.hisRst = ui->hisRst->text().trimmed();
	c.myRst = ui->myRst->text().trimmed();
	return c;
}

void MainWindow::updateTxUi()
{
	ui->btnTx->setChecked(m_txDown);
	ui->btnTx->setText(m_txDown && m_reqRecv ? tr("WAIT") : tr("TX"));
	ui->btnTx->setStyleSheet(m_txDown ? QStringLiteral("QPushButton{background:#c62828;color:white;font-weight:bold;}") : QString());
}

void MainWindow::updateMacroButtons()
{
	for( int i = 0; i < AppSettings::kMacros; i++ ){
		const QString name = m_s.macroName[i].isEmpty() ? QStringLiteral("M%1").arg(i + 1) : m_s.macroName[i];
		m_macroBtn[i]->setText(name);
		const QString key = i < 12 ? QStringLiteral("F%1").arg(i + 1) : QStringLiteral("Shift+F%1").arg(i - 11);
		m_macroBtn[i]->setToolTip(QStringLiteral("%1: %2").arg(key, AppSettings::escape(m_s.macro[i])));
	}
	for( int i = 0; i < AppSettings::kInputButtons; i++ ){
		m_inBtn[i]->setText(m_s.inButtonName[i].isEmpty() ? QStringLiteral("IN%1").arg(i + 1) : m_s.inButtonName[i]);
		m_inBtn[i]->setToolTip(QStringLiteral("Alt+%1: %2").arg(i + 1).arg(AppSettings::escape(m_s.inButton[i])));
	}
}

void MainWindow::toTx(const QByteArray &text, bool haveText)
{
	if( m_txDown ) return;
	if( m_engine->isTx() && !m_engine->isTxRequested() ){
		showStatus(tr("Still finishing the previous transmission"));
		return;
	}
	m_reqRecv = false;
	m_macro = 0;
	m_macroStat = 0;
	m_fifoPause = 0;
	bool carrierFirst = true;
	if( haveText ){
		for( int i = 0; i < text.size(); i++ ){
			const char c = text[i];
			if( c == '~' || (c == '%' && i + 1 < text.size() && text[i + 1] == '{') ){
				carrierFirst = false;
				break;
			}
			if( c != '[' && c != ']' && c != '\\' && c != '_' ) break;
		}
	}
	m_txDown = true;
	m_pttClock.restart();
	reportPtt(true);
	if( m_txDelayMs > 0 ) QThread::msleep(ulong(m_txDelayMs));		// as in the original XMIT(): the remote application keys the radio meanwhile
	m_engine->requestTx(carrierFirst);
	updateTxUi();
}

// Tells the remote API about TX/RX changes; repeated reports of the same state are dropped.
void MainWindow::reportPtt(bool tx)
{
	if( m_pttReported == tx ) return;
	m_pttReported = tx;
	emit pttEvent(tx);
}

// Remote API "way to send" (RXM_SETSWITCH b17-b18): hold back the text until a word or a line is complete.
bool MainWindow::fifoSendReady() const
{
	if( m_sendWay == 0 || m_reqRecv ) return true;
	QTextDocument *d = ui->txEdit->document();
	if( d->blockCount() > 1 ) return true;					// a complete line is waiting
	const QString line = d->firstBlock().text();
	if( line.isEmpty() || line.contains(QLatin1Char('\\')) ) return true;
	return m_sendWay == 1 && line.contains(QLatin1Char(' '));
}

void MainWindow::toRx(bool immediate)
{
	if( !m_txDown ) return;
	m_txDown = false;
	m_reqRecv = false;
	if( m_viaTx ) ui->txEdit->clear();
	m_macro = 0;
	m_macroStat = 0;
	m_fifoPause = 0;
	m_engine->requestRx(immediate);
	if( !m_engine->isTx() ) reportPtt(false);		// never got on the air: the engine will not report a change
	updateTxUi();
}

void MainWindow::outputStr(const QByteArray &str)
{
	if( str.isEmpty() ) return;
	m_nowStr = str;
	QByteArray bf = Macro::expand(QString::fromLatin1(str), macroContext(), false);
	while( bf.endsWith('#') ) bf.chop(1);
	if( !bf.isEmpty() ) m_engine->putText(bf);

	// A trailing "\" returns to receive when everything is sent, a trailing "#" repeats the text.
	for( int i = str.size() - 1; i >= 0; i-- ){
		const char c = str[i];
		if( c == '\\' ){
			m_macroStat = 1;
			m_macro = 17;
			if( m_txDown ) m_engine->setDiddleTimer(-1);
		}
		else if( c == '#' ){
			m_macroStat = 2;
			m_macro = 17;
			m_engine->setDiddleTimer(0);
		}
		else if( c != '\r' && c != '\n' ){
			break;
		}
	}
}

void MainWindow::macBtnExec(int n)
{
	if( n < 0 || n >= AppSettings::kMacros ) return;
	const QString p = m_s.macro[n];
	if( p.isEmpty() ) return;
	if( p[0] == QLatin1Char('#') ){
		writeFifo(p.mid(1));
		return;
	}
	const QByteArray raw = p.toLatin1();
	toTx(raw, true);
	if( !m_txDown ) return;
	if( p[0] == QLatin1Char('\\') ){
		writeFifo(p.mid(1));
	}
	else {
		outputStr(raw);
		m_macro = n + 1;
	}
}

void MainWindow::writeFifo(const QString &text)
{
	QString s = text;
	while( !s.isEmpty() && (s[0] == QLatin1Char('\\') || s[0] == QLatin1Char('#')) ){
		if( s[0] == QLatin1Char('\\') ) ui->txEdit->clear();
		else if( !m_txDown ) toTx(s.toLatin1(), true);
		s.remove(0, 1);
	}
	const int e = s.indexOf(QLatin1String("%E"));
	if( e >= 0 ) s.truncate(e);
	QTextCursor cur(ui->txEdit->document());
	cur.movePosition(QTextCursor::End);
	cur.insertText(s);
}

void MainWindow::typeText(const QString &text) { writeFifo(text); }

int MainWindow::fifoLen() const
{
	return qMax(0, ui->txEdit->document()->characterCount() - 1);
}

int MainWindow::fifoGet()
{
	QTextDocument *d = ui->txEdit->document();
	if( fifoLen() == 0 ) return -1;
	const QChar ch = d->characterAt(0);
	QTextCursor cur(d);
	cur.setPosition(0);
	cur.deleteChar();
	return ch.unicode();
}

void MainWindow::pumpTx()
{
	if( m_pttTimerSec > 0 ){
		if( !m_txDown ){
			m_pttClock.restart();
		}
		else if( m_pttClock.elapsed() >= qint64(m_pttTimerSec) * 1000 ){
			toRx(false);			// PTT timer expired (remote API RXM_TIMER)
			return;
		}
	}
	if( !m_txDown ){
		if( ui->actionTest->isChecked() && !m_engine->isTx() && m_engine->txBufferCount() < 3 )
			m_engine->putText(QByteArray(kTestData));
		return;
	}
	if( m_engine->txIdle() && m_macro ){
		switch( m_macroStat ){
			case 1:				// back to receive
				toRx(false);
				return;
			case 2:				// repeat
				outputStr(QByteArray(m_nowStr));
				break;
			default:
				m_macroStat = 0;
				m_macro = 0;
				break;
		}
	}
	if( m_macro ) return;
	if( m_fifoPause ){
		m_fifoPause--;
		return;
	}

	while( m_txDown && m_engine->txBufferCount() <= 2 ){
		if( !fifoSendReady() ) return;
		bool again;
		do {
			again = false;
			const int c = fifoGet();
			if( c < 0 ){
				if( m_reqRecv && m_engine->txIdle() ){
					if( !m_engine->fadeOutTx() ) toRx(false);
				}
				return;
			}
			if( c == 0x2029 || c == 0x2028 || c == '\r' ){
				outputStr("\r");
				outputStr("\n");
			}
			else if( c == '\n' || c == '#' || c == 0 ){
				// '#' is only meaningful inside macro text, as in the original
			}
			else if( c == '%' ){
				QByteArray seq("%");
				const int c2 = fifoGet();
				if( c2 >= 0 ){
					seq += char(c2);
					if( c2 == '{' ){
						while( fifoLen() ){
							const int k = fifoGet();
							seq += char(k);
							if( k == '}' ) break;
						}
					}
				}
				outputStr(seq);
			}
			else if( c == '^' ){
				m_fifoPause = 6;
				return;
			}
			else {
				const char ch = QChar(ushort(c)).toLatin1();
				if( ch ){
					outputStr(QByteArray(1, ch));
					if( (ch == '_' || ch == '~') && m_engine->txBufferCount() <= 10 ) again = true;
				}
			}
		} while( again && !m_macro );
		if( m_macro || fifoLen() < 2 ) break;
	}
}

void MainWindow::txButtonClicked()
{
	if( !m_txDown ){
		toTx(QByteArray(), false);
	}
	else if( fifoLen() > 0 || (!m_engine->txIdle() && !m_macro) ){
		m_reqRecv = !m_reqRecv;			// finish the text, then receive (second click cancels)
	}
	else {
		toRx(false);
	}
	updateTxUi();
}

void MainWindow::pressTx() { txButtonClicked(); }
void MainWindow::pressTxOff() { toRx(true); }
void MainWindow::pressMacro(int index) { macBtnExec(index); }

// ===========================================================================
// QSO log
// ===========================================================================
void MainWindow::logQso()
{
	QsoLog::Entry e;
	e.call = ui->hisCall->text().trimmed().toUpper();
	if( e.call.isEmpty() ){
		showStatus(tr("Enter the other station's call sign first"));
		return;
	}
	e.utc = QDateTime::currentDateTimeUtc();
	e.name = ui->hisName->text().trimmed();
	e.qth = ui->hisQth->text().trimmed();
	e.rstSent = ui->myRst->text().trimmed();
	e.rstRcvd = ui->hisRst->text().trimmed();
	e.myCall = m_s.call;
	e.audioMark = m_s.mark;
	QString err;
	if( !m_log.append(e, &err) ){
		showStatus(tr("Cannot write log: %1").arg(err));
		return;
	}
	showStatus(tr("Logged %1 to %2").arg(e.call, m_log.path()));
	ui->hisCall->clear();
	ui->hisName->clear();
	ui->hisQth->clear();
}

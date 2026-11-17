#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

constexpr int kRows = AppSettings::kMacros + AppSettings::kInputButtons;

QDoubleSpinBox *dspin(double lo, double hi, double step, int dec, double v)
{
	auto *s = new QDoubleSpinBox;
	s->setRange(lo, hi);
	s->setSingleStep(step);
	s->setDecimals(dec);
	s->setValue(v);
	return s;
}

} // namespace

SettingsDialog::SettingsDialog(const AppSettings &s, QWidget *parent) : QDialog(parent)
{
	setWindowTitle(tr("MMTTY Settings"));
	auto *tabs = new QTabWidget;
	tabs->addTab(buildStationPage(), tr("Station"));
	tabs->addTab(buildSoundPage(), tr("Sound && PTT"));
	tabs->addTab(buildTxPage(), tr("Transmit && AFC"));
	tabs->addTab(buildMacroPage(), tr("Macros"));

	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto *lay = new QVBoxLayout(this);
	lay->addWidget(tabs);
	lay->addWidget(bb);
	resize(720, 520);

	// ---- load values ----
	m_call->setText(s.call);

	m_in->addItem(tr("System default"), QByteArray());
	for( const auto &d : Engine::inputDevices() ) m_in->addItem(d.first, d.second);
	m_out->addItem(tr("System default"), QByteArray());
	for( const auto &d : Engine::outputDevices() ) m_out->addItem(d.first, d.second);
	m_in->setCurrentIndex(std::max(0, m_in->findData(s.inputDevice)));
	m_out->setCurrentIndex(std::max(0, m_out->findData(s.outputDevice)));
	m_channel->setCurrentIndex(s.inputChannel ? 1 : 0);
	m_rate->setCurrentIndex(std::max(0, m_rate->findData(int(s.sampleRate))));
	m_txOffset->setValue(s.txOffset);
	refreshPorts();
	if( !s.pttPort.isEmpty() && m_pttPort->findData(s.pttPort) < 0 ) m_pttPort->addItem(s.pttPort, s.pttPort);
	m_pttPort->setCurrentIndex(std::max(0, m_pttPort->findData(s.pttPort)));
	m_pttLines->setCurrentIndex(std::max(0, m_pttLines->findData(s.pttLines)));
	m_pttInvert->setChecked(s.pttInvert);
	m_fskLine->setCurrentIndex(std::max(0, m_fskLine->findData(s.fskLine)));
	m_fskInvert->setChecked(s.fskInvert);

	m_echo->setCurrentIndex(std::clamp(s.echo, 0, 2));
	m_dataBits->setCurrentIndex(s.dataBits - 5);
	m_stop->setCurrentIndex(std::clamp(s.stopSel, 0, 2));
	m_parity->setCurrentIndex(std::clamp(s.parity, 0, 4));
	m_txNet->setChecked(s.txNet);
	m_txFix->setChecked(s.txFixShift);
	m_txUos->setChecked(s.txUos);
	m_rxUos->setChecked(s.uos);
	m_defMark->setValue(s.defMark);
	m_defShift->setValue(s.defShift);
	m_afcMode->setCurrentIndex(std::clamp(s.afcMode, 0, 2));
	m_afcSq->setValue(s.afcSquelch);
	m_afcTime->setValue(s.afcTime);
	m_afcSweep->setValue(s.afcSweep);

	for( int i = 0; i < AppSettings::kMacros; i++ ){
		m_macros->setItem(i, 0, new QTableWidgetItem(s.macroName[i]));
		m_macros->setItem(i, 1, new QTableWidgetItem(AppSettings::escape(s.macro[i])));
	}
	for( int i = 0; i < AppSettings::kInputButtons; i++ ){
		const int r = AppSettings::kMacros + i;
		m_macros->setItem(r, 0, new QTableWidgetItem(s.inButtonName[i]));
		m_macros->setItem(r, 1, new QTableWidgetItem(AppSettings::escape(s.inButton[i])));
	}
}

QWidget *SettingsDialog::buildStationPage()
{
	auto *w = new QWidget;
	auto *f = new QFormLayout(w);
	m_call = new QLineEdit;
	m_call->setMaxLength(16);
	f->addRow(tr("My call sign"), m_call);
	return w;
}

QWidget *SettingsDialog::buildSoundPage()
{
	auto *w = new QWidget;
	auto *f = new QFormLayout(w);
	m_in = new QComboBox;
	m_out = new QComboBox;
	m_channel = new QComboBox;
	m_channel->addItems({tr("Left / mono"), tr("Right")});
	m_rate = new QComboBox;
	m_rate->addItem(QStringLiteral("11025 Hz"), 11025);
	m_rate->addItem(QStringLiteral("12000 Hz"), 12000);
	m_rate->addItem(QStringLiteral("8000 Hz"), 8000);
	m_rate->addItem(QStringLiteral("6000 Hz"), 6000);
	m_txOffset = dspin(-1000.0, 1000.0, 0.1, 1, 0.0);
	f->addRow(tr("Sound input"), m_in);
	f->addRow(tr("Sound output"), m_out);
	f->addRow(tr("Input channel"), m_channel);
	f->addRow(tr("Sampling rate (needs restart)"), m_rate);
	f->addRow(tr("TX clock correction (Hz)"), m_txOffset);

	m_pttPort = new QComboBox;
	m_pttPort->setEditable(false);
	m_pttLines = new QComboBox;
	m_pttLines->addItem(tr("None"), 0);
	m_pttLines->addItem(QStringLiteral("RTS"), 1);
	m_pttLines->addItem(QStringLiteral("DTR"), 2);
	m_pttLines->addItem(QStringLiteral("RTS + DTR"), 3);
	m_pttInvert = new QCheckBox(tr("Invert PTT polarity"));
	auto *refresh = new QPushButton(tr("Rescan ports"));
	connect(refresh, &QPushButton::clicked, this, &SettingsDialog::refreshPorts);
	m_fskLine = new QComboBox;
	m_fskLine->addItem(tr("Off (AFSK through the sound card)"), 0);
	m_fskLine->addItem(tr("DTR"), 2);
	m_fskLine->addItem(tr("RTS"), 1);
	m_fskLine->setToolTip(tr("Software FSK: the selected control line is toggled with exact bit timing instead of "
	                         "sending audio. Wire it to the radio's FSK input. It cannot also be a PTT line."));
	m_fskInvert = new QCheckBox(tr("Invert FSK polarity (mark = line off)"));
	f->addRow(tr("PTT / FSK serial port"), m_pttPort);
	f->addRow(tr("PTT line"), m_pttLines);
	f->addRow(QString(), m_pttInvert);
	f->addRow(tr("FSK line"), m_fskLine);
	f->addRow(QString(), m_fskInvert);
	f->addRow(QString(), refresh);
	return w;
}

void SettingsDialog::refreshPorts()
{
	const QVariant cur = m_pttPort->currentData();
	m_pttPort->clear();
	m_pttPort->addItem(tr("None (VOX / no PTT)"), QString());
	for( const QString &p : PttPort::availablePorts() ) m_pttPort->addItem(p, p);
	const int i = m_pttPort->findData(cur);
	if( i >= 0 ) m_pttPort->setCurrentIndex(i);
}

QWidget *SettingsDialog::buildTxPage()
{
	auto *w = new QWidget;
	auto *f = new QFormLayout(w);

	m_echo = new QComboBox;
	m_echo->addItems({tr("Direct (no echo)"), tr("Sound loopback echo"), tr("External echo (receiver stays open)")});
	m_dataBits = new QComboBox;
	m_dataBits->addItems({"5", "6", "7", "8"});
	m_stop = new QComboBox;
	m_stop->addItems({"1", "1.5", "2"});
	m_parity = new QComboBox;
	m_parity->addItems({tr("None"), tr("Even"), tr("Odd"), tr("Mark"), tr("Space")});
	m_txNet = new QCheckBox(tr("TX frequency follows RX (NET)"));
	m_txFix = new QCheckBox(tr("Use the fixed shift below when transmitting"));
	m_txUos = new QCheckBox(tr("Unshift on space (transmit)"));
	m_rxUos = new QCheckBox(tr("Unshift on space (receive)"));
	m_defMark = dspin(300, 2700, 1, 0, 2125);
	m_defShift = dspin(10, 2000, 1, 0, 170);
	f->addRow(tr("Echo"), m_echo);
	f->addRow(tr("Data bits"), m_dataBits);
	f->addRow(tr("Stop bits"), m_stop);
	f->addRow(tr("Parity"), m_parity);
	f->addRow(m_txNet);
	f->addRow(m_txFix);
	f->addRow(tr("Default TX mark (Hz)"), m_defMark);
	f->addRow(tr("Default TX shift (Hz)"), m_defShift);
	f->addRow(m_txUos);
	f->addRow(m_rxUos);

	m_afcMode = new QComboBox;
	m_afcMode->addItems({tr("Free shift"), tr("Fixed shift"), tr("Ham shifts (170/200/220/240)")});
	m_afcSq = new QSpinBox;
	m_afcSq->setRange(1, 255);
	m_afcTime = dspin(1.0, 64.0, 0.5, 1, 8.0);
	m_afcSweep = dspin(0.5, 3.0, 0.1, 1, 1.0);
	f->addRow(tr("AFC mode"), m_afcMode);
	f->addRow(tr("AFC squelch"), m_afcSq);
	f->addRow(tr("AFC time constant"), m_afcTime);
	f->addRow(tr("AFC sweep range"), m_afcSweep);
	return w;
}

QWidget *SettingsDialog::buildMacroPage()
{
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);
	auto *help = new QLabel(tr(
		"%m my call  %c his call  %n name  %q QTH  %r his RST  %s my RST  %L LTRS  %F FIGS  %D/%T/%t date/time  %{TEXT} CW ID\n"
		"_ mark  ~ carrier off  [ ] diddle off/on  trailing \\ = back to receive  trailing # = repeat.   \\r \\n \\\\ are escapes."));
	help->setWordWrap(true);
	lay->addWidget(help);
	m_macros = new QTableWidget(kRows, 2);
	m_macros->setHorizontalHeaderLabels({tr("Name"), tr("Text")});
	QStringList rows;
	for( int i = 0; i < AppSettings::kMacros; i++ ) rows << QStringLiteral("M%1").arg(i + 1);
	for( int i = 0; i < AppSettings::kInputButtons; i++ ) rows << QStringLiteral("IN%1").arg(i + 1);
	m_macros->setVerticalHeaderLabels(rows);
	m_macros->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_macros->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	lay->addWidget(m_macros);
	return w;
}

void SettingsDialog::apply(AppSettings &s) const
{
	s.call = m_call->text().trimmed().toUpper();
	if( s.call.isEmpty() ) s.call = QStringLiteral("NOCALL");

	s.inputDevice = m_in->currentData().toByteArray();
	s.outputDevice = m_out->currentData().toByteArray();
	s.inputChannel = m_channel->currentIndex();
	s.sampleRate = m_rate->currentData().toInt();
	s.txOffset = m_txOffset->value();
	s.pttPort = m_pttPort->currentData().toString();
	s.pttLines = m_pttLines->currentData().toInt();
	s.pttInvert = m_pttInvert->isChecked();
	s.fskLine = m_fskLine->currentData().toInt();
	s.fskInvert = m_fskInvert->isChecked();
	if( s.fskLine ) s.pttLines &= ~s.fskLine;		// one line cannot do both jobs

	s.echo = m_echo->currentIndex();
	s.dataBits = m_dataBits->currentIndex() + 5;
	s.stopSel = m_stop->currentIndex();
	s.parity = m_parity->currentIndex();
	s.txNet = m_txNet->isChecked();
	s.txFixShift = m_txFix->isChecked();
	s.txUos = m_txUos->isChecked();
	s.uos = m_rxUos->isChecked();
	s.defMark = m_defMark->value();
	s.defShift = m_defShift->value();
	s.afcMode = m_afcMode->currentIndex();
	s.afcSquelch = m_afcSq->value();
	s.afcTime = m_afcTime->value();
	s.afcSweep = m_afcSweep->value();

	auto text = [this](int r, int c) {
		const QTableWidgetItem *it = m_macros->item(r, c);
		return it ? it->text() : QString();
	};
	for( int i = 0; i < AppSettings::kMacros; i++ ){
		s.macroName[i] = text(i, 0);
		s.macro[i] = AppSettings::unescape(text(i, 1));
	}
	for( int i = 0; i < AppSettings::kInputButtons; i++ ){
		s.inButtonName[i] = text(AppSettings::kMacros + i, 0);
		s.inButton[i] = AppSettings::unescape(text(AppSettings::kMacros + i, 1));
	}
}

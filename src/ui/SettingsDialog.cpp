#include "SettingsDialog.h"

#include <QHeaderView>
#include <QTableWidget>

#include "ui_SettingsDialog.h"

namespace {

constexpr int kRows = AppSettings::kMacros + AppSettings::kInputButtons;

} // namespace

SettingsDialog::SettingsDialog(const AppSettings &s, QWidget *parent) : QDialog(parent), ui(new Ui::SettingsDialog)
{
	ui->setupUi(this);
	connect(ui->rescanButton, &QPushButton::clicked, this, &SettingsDialog::refreshPorts);
	ui->macroTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

	// item data that the .ui file cannot express
	const int rates[] = {11025, 12000, 8000, 6000};
	for( int i = 0; i < 4; i++ ) ui->rate->setItemData(i, rates[i]);
	const int lines[] = {0, 1, 2, 3};								// none, RTS, DTR, RTS + DTR
	for( int i = 0; i < 4; i++ ) ui->pttLines->setItemData(i, lines[i]);
	const int fsk[] = {0, 2, 1};									// off, DTR, RTS
	for( int i = 0; i < 3; i++ ) ui->fskLine->setItemData(i, fsk[i]);
	static_assert(kRows == 20, "macroTable in SettingsDialog.ui has 20 rows");

	// ---- load values ----
	ui->call->setText(s.call);

	ui->inputDevice->addItem(tr("System default"), QByteArray());
	for( const auto &d : Engine::inputDevices() ) ui->inputDevice->addItem(d.first, d.second);
	ui->outputDevice->addItem(tr("System default"), QByteArray());
	for( const auto &d : Engine::outputDevices() ) ui->outputDevice->addItem(d.first, d.second);
	ui->inputDevice->setCurrentIndex(std::max(0, ui->inputDevice->findData(s.inputDevice)));
	ui->outputDevice->setCurrentIndex(std::max(0, ui->outputDevice->findData(s.outputDevice)));
	ui->channel->setCurrentIndex(s.inputChannel ? 1 : 0);
	ui->rate->setCurrentIndex(std::max(0, ui->rate->findData(int(s.sampleRate))));
	ui->txOffset->setValue(s.txOffset);
	refreshPorts();
	if( !s.pttPort.isEmpty() && ui->pttPort->findData(s.pttPort) < 0 ) ui->pttPort->addItem(s.pttPort, s.pttPort);
	ui->pttPort->setCurrentIndex(std::max(0, ui->pttPort->findData(s.pttPort)));
	ui->pttLines->setCurrentIndex(std::max(0, ui->pttLines->findData(s.pttLines)));
	ui->pttInvert->setChecked(s.pttInvert);
	ui->fskLine->setCurrentIndex(std::max(0, ui->fskLine->findData(s.fskLine)));
	ui->fskInvert->setChecked(s.fskInvert);

	ui->echo->setCurrentIndex(std::clamp(s.echo, 0, 2));
	ui->dataBits->setCurrentIndex(s.dataBits - 5);
	ui->stopBits->setCurrentIndex(std::clamp(s.stopSel, 0, 2));
	ui->parity->setCurrentIndex(std::clamp(s.parity, 0, 4));
	ui->txNet->setChecked(s.txNet);
	ui->txFix->setChecked(s.txFixShift);
	ui->txUos->setChecked(s.txUos);
	ui->rxUos->setChecked(s.uos);
	ui->defMark->setValue(s.defMark);
	ui->defShift->setValue(s.defShift);
	ui->afcMode->setCurrentIndex(std::clamp(s.afcMode, 0, 2));
	ui->afcSquelch->setValue(s.afcSquelch);
	ui->afcTime->setValue(s.afcTime);
	ui->afcSweep->setValue(s.afcSweep);

	for( int i = 0; i < AppSettings::kMacros; i++ ){
		ui->macroTable->setItem(i, 0, new QTableWidgetItem(s.macroName[i]));
		ui->macroTable->setItem(i, 1, new QTableWidgetItem(AppSettings::escape(s.macro[i])));
	}
	for( int i = 0; i < AppSettings::kInputButtons; i++ ){
		const int r = AppSettings::kMacros + i;
		ui->macroTable->setItem(r, 0, new QTableWidgetItem(s.inButtonName[i]));
		ui->macroTable->setItem(r, 1, new QTableWidgetItem(AppSettings::escape(s.inButton[i])));
	}
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::refreshPorts()
{
	const QVariant cur = ui->pttPort->currentData();
	ui->pttPort->clear();
	ui->pttPort->addItem(tr("None (VOX / no PTT)"), QString());
	for( const QString &p : PttPort::availablePorts() ) ui->pttPort->addItem(p, p);
	const int i = ui->pttPort->findData(cur);
	if( i >= 0 ) ui->pttPort->setCurrentIndex(i);
}

void SettingsDialog::apply(AppSettings &s) const
{
	s.call = ui->call->text().trimmed().toUpper();
	if( s.call.isEmpty() ) s.call = QStringLiteral("NOCALL");

	s.inputDevice = ui->inputDevice->currentData().toByteArray();
	s.outputDevice = ui->outputDevice->currentData().toByteArray();
	s.inputChannel = ui->channel->currentIndex();
	s.sampleRate = ui->rate->currentData().toInt();
	s.txOffset = ui->txOffset->value();
	s.pttPort = ui->pttPort->currentData().toString();
	s.pttLines = ui->pttLines->currentData().toInt();
	s.pttInvert = ui->pttInvert->isChecked();
	s.fskLine = ui->fskLine->currentData().toInt();
	s.fskInvert = ui->fskInvert->isChecked();
	if( s.fskLine ) s.pttLines &= ~s.fskLine;		// one line cannot do both jobs

	s.echo = ui->echo->currentIndex();
	s.dataBits = ui->dataBits->currentIndex() + 5;
	s.stopSel = ui->stopBits->currentIndex();
	s.parity = ui->parity->currentIndex();
	s.txNet = ui->txNet->isChecked();
	s.txFixShift = ui->txFix->isChecked();
	s.txUos = ui->txUos->isChecked();
	s.uos = ui->rxUos->isChecked();
	s.defMark = ui->defMark->value();
	s.defShift = ui->defShift->value();
	s.afcMode = ui->afcMode->currentIndex();
	s.afcSquelch = ui->afcSquelch->value();
	s.afcTime = ui->afcTime->value();
	s.afcSweep = ui->afcSweep->value();

	auto text = [this](int r, int c) {
		const QTableWidgetItem *it = ui->macroTable->item(r, c);
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

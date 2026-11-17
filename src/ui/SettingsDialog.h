// Settings dialog: station, sound card, PTT, transmit / AFC options, character format and the macro editor.
#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

#include "AppSettings.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;

class SettingsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit SettingsDialog(const AppSettings &s, QWidget *parent = nullptr);

	// Copies the dialog values into 's' (only the fields this dialog owns).
	void apply(AppSettings &s) const;

private:
	QWidget *buildStationPage();
	QWidget *buildSoundPage();
	QWidget *buildTxPage();
	QWidget *buildMacroPage();
	void refreshPorts();

	// station
	QLineEdit *m_call;
	// sound
	QComboBox *m_in, *m_out, *m_channel, *m_rate;
	QDoubleSpinBox *m_txOffset;
	QComboBox *m_pttPort, *m_pttLines, *m_fskLine;
	QCheckBox *m_pttInvert, *m_fskInvert;
	// tx / afc / format
	QComboBox *m_echo, *m_dataBits, *m_stop, *m_parity, *m_afcMode;
	QCheckBox *m_txNet, *m_txFix, *m_txUos, *m_rxUos;
	QDoubleSpinBox *m_defMark, *m_defShift, *m_afcTime, *m_afcSweep;
	QSpinBox *m_afcSq;
	// macros
	QTableWidget *m_macros;
};

#endif

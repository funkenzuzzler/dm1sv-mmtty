// Persistent application settings (the Qt replacement of MMTTY's .ini file handling).
#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "Engine.h"

class AppSettings
{
public:
	static constexpr int kMacros = 16;
	static constexpr int kInputButtons = 4;

	AppSettings();				// defaults taken from the original "Mmtty English.ini"

	void load(QSettings &s);
	void save(QSettings &s) const;

	Engine::Config engineConfig() const;

	// C-style escaping used for macro text: \r \n \\ (a trailing "\" is a macro control character).
	static QString escape(const QString &raw);
	static QString unescape(const QString &esc);

	// ---- station ----
	QString call = QStringLiteral("NOCALL");

	// ---- tuning / demodulator ----
	double mark = 2125.0;
	double shift = 170.0;
	double baud = 45.45;
	int    demType = 1;			// 0 IIR, 1 FIR, 2 PLL, 3 FFT
	bool   afc = true;
	bool   reverse = false;
	bool   uos = false;			// receive: unshift on space
	bool   txUos = true;		// transmit: unshift on space
	bool   bpf = false;
	bool   notch = false;
	bool   atc = false;
	bool   squelch = false;
	double squelchLevel = 600.0;

	// ---- character format ----
	int dataBits = 5;
	int stopSel = 1;			// 0: 1 bit, 1: 1.5 bit, 2: 2 bit
	int parity = 0;

	// ---- TX frequency handling ----
	bool   txNet = true;
	bool   txFixShift = false;
	double defMark = 2125.0;
	double defShift = 170.0;
	int    echo = 1;			// 0 direct, 1 sound loopback, 2 external

	// ---- AFC ----
	int    afcMode = 1;			// 0 free, 1 fixed shift, 2 ham
	int    afcSquelch = 32;
	double afcTime = 8.0;
	double afcSweep = 1.0;

	// ---- spectrum ----
	int  fftGain = 1;
	int  fftResp = 2;
	int  fftRange = 0;
	bool showWaterfall = true;
	bool showXY = true;

	// ---- sound / PTT ----
	QByteArray inputDevice;
	QByteArray outputDevice;
	int    inputChannel = 0;
	double sampleRate = 11025.0;	// takes effect after a restart
	double txOffset = 0.0;
	QString pttPort;
	int     pttLines = 1;			// 0 none, 1 RTS, 2 DTR, 3 both
	bool    pttInvert = false;
	int     fskLine = 0;			// software FSK on the same port: 0 off (AFSK via sound card), 1 RTS, 2 DTR
	bool    fskInvert = false;		// false: mark = line on

	// ---- macros ----
	QString macro[kMacros];			// raw text (control characters unescaped)
	QString macroName[kMacros];
	QString inButton[kInputButtons];
	QString inButtonName[kInputButtons];

	// ---- combo lists ----
	QStringList markList, shiftList, baudList;
};

#endif

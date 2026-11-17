#include "AppSettings.h"

namespace {

// Default macros: taken from the original "Mmtty English.ini" ([Macro], [MacroName], [InBtn]).
const char *const kDefMacro[AppSettings::kMacros] = {
	R"(__\r\n%c DE %m %m K\r\n_\)",
	R"(__\r\n%c %c DE %m %m %m K\r\n_\)",
	R"(__\r\nDE %m %m %m K\r\n_\)",
	R"(__\r\nQSL UR 599-599 TU\r\n_\)",
	R"(__\r\nOK TNX %n\r\nUR %r %r %c DE %m TU SK\r\n_\)",
	"", "", "",
	R"(\\\r\n73 TU SK SK\r\n%{TU}\)",
	R"([__~~__~\)",
	"", "",
	R"(\r\nRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRY#)",
	"",
	R"(__\r\nRYRY CQ CQ CQ DE %m %m %m\r\nRYRY CQ CQ CQ DE %m %m %m PSE K\r\n_\)",
	R"(__\r\nRYRY CQ CQ CQ DE %m %m %m PSE K\r\n_\)",
};
const char *const kDefMacroName[AppSettings::kMacros] = {
	"1X2", "2X3", "DE3", "UR599", "QANS", "M6", "M7", "M8",
	"SK", "EE", "M11", "M12", "RY", "M14", "CQ2", "CQ1",
};
const char *const kDefIn[AppSettings::kInputButtons] = {
	R"(%c DE %m)",
	R"(DEAR %n)",
	R"(__\r\nRGR %c DE %m ALL OK DEAR %n\r\n)",
	R"(BTU %c DE %m KN\r\n_\)",
};
const char *const kDefInName[AppSettings::kInputButtons] = {"1X1", "DEAR", "ANS", "BTU"};

QByteArray fromB64(const QString &s) { return QByteArray::fromBase64(s.toLatin1()); }
QString toB64(const QByteArray &b) { return QString::fromLatin1(b.toBase64()); }

} // namespace

QString AppSettings::escape(const QString &raw)
{
	QString r;
	for( QChar c : raw ){
		if( c == QLatin1Char('\r') ) r += QLatin1String("\\r");
		else if( c == QLatin1Char('\n') ) r += QLatin1String("\\n");
		else if( c == QLatin1Char('\\') ) r += QLatin1String("\\\\");
		else r += c;
	}
	return r;
}

QString AppSettings::unescape(const QString &esc)
{
	QString r;
	for( int i = 0; i < esc.size(); i++ ){
		const QChar c = esc[i];
		if( c == QLatin1Char('\\') && i + 1 < esc.size() ){
			const QChar n = esc[i + 1];
			if( n == QLatin1Char('r') ){ r += QLatin1Char('\r'); i++; continue; }
			if( n == QLatin1Char('n') ){ r += QLatin1Char('\n'); i++; continue; }
			if( n == QLatin1Char('\\') ){ r += QLatin1Char('\\'); i++; continue; }
		}
		r += c;
	}
	return r;
}

AppSettings::AppSettings()
{
	for( int i = 0; i < kMacros; i++ ){
		macro[i] = unescape(QString::fromLatin1(kDefMacro[i]));
		macroName[i] = QString::fromLatin1(kDefMacroName[i]);
	}
	for( int i = 0; i < kInputButtons; i++ ){
		inButton[i] = unescape(QString::fromLatin1(kDefIn[i]));
		inButtonName[i] = QString::fromLatin1(kDefInName[i]);
	}
	markList = QStringLiteral("2125,2000,1700,1445,1275,1170,1000,915").split(',');
	shiftList = QStringLiteral("23,85,160,170,182,200,240,350,425,850").split(',');
	baudList = QStringLiteral("22,45,45.45,50,56,75,100,110,150,200,300").split(',');
}

Engine::Config AppSettings::engineConfig() const
{
	Engine::Config c;
	c.sampleRate = sampleRate;
	c.txOffset = txOffset;
	c.inputDevice = inputDevice;
	c.outputDevice = outputDevice;
	c.inputChannel = inputChannel;
	c.ptt.portName = pttPort;
	c.ptt.lines = pttLines;
	c.ptt.invert = pttInvert;
	c.ptt.fskLine = fskLine;
	c.ptt.fskInvert = fskInvert;
	return c;
}

void AppSettings::load(QSettings &s)
{
	s.beginGroup(QStringLiteral("Station"));
	call = s.value("call", call).toString();
	s.endGroup();

	s.beginGroup(QStringLiteral("Radio"));
	mark = s.value("mark", mark).toDouble();
	shift = s.value("shift", shift).toDouble();
	baud = s.value("baud", baud).toDouble();
	demType = s.value("demType", demType).toInt();
	afc = s.value("afc", afc).toBool();
	reverse = s.value("reverse", reverse).toBool();
	uos = s.value("uos", uos).toBool();
	txUos = s.value("txUos", txUos).toBool();
	bpf = s.value("bpf", bpf).toBool();
	notch = s.value("notch", notch).toBool();
	atc = s.value("atc", atc).toBool();
	squelch = s.value("squelch", squelch).toBool();
	squelchLevel = s.value("squelchLevel", squelchLevel).toDouble();
	dataBits = s.value("dataBits", dataBits).toInt();
	stopSel = s.value("stopSel", stopSel).toInt();
	parity = s.value("parity", parity).toInt();
	txNet = s.value("txNet", txNet).toBool();
	txFixShift = s.value("txFixShift", txFixShift).toBool();
	defMark = s.value("defMark", defMark).toDouble();
	defShift = s.value("defShift", defShift).toDouble();
	echo = s.value("echo", echo).toInt();
	afcMode = s.value("afcMode", afcMode).toInt();
	afcSquelch = s.value("afcSquelch", afcSquelch).toInt();
	afcTime = s.value("afcTime", afcTime).toDouble();
	afcSweep = s.value("afcSweep", afcSweep).toDouble();
	s.endGroup();

	s.beginGroup(QStringLiteral("Spectrum"));
	fftGain = s.value("gain", fftGain).toInt();
	fftResp = s.value("resp", fftResp).toInt();
	fftRange = s.value("range", fftRange).toInt();
	showWaterfall = s.value("waterfall", showWaterfall).toBool();
	showXY = s.value("xy", showXY).toBool();
	s.endGroup();

	s.beginGroup(QStringLiteral("Sound"));
	inputDevice = fromB64(s.value("input").toString());
	outputDevice = fromB64(s.value("output").toString());
	inputChannel = s.value("channel", inputChannel).toInt();
	sampleRate = s.value("sampleRate", sampleRate).toDouble();
	txOffset = s.value("txOffset", txOffset).toDouble();
	pttPort = s.value("pttPort", pttPort).toString();
	pttLines = s.value("pttLines", pttLines).toInt();
	pttInvert = s.value("pttInvert", pttInvert).toBool();
	fskLine = s.value("fskLine", fskLine).toInt();
	fskInvert = s.value("fskInvert", fskInvert).toBool();
	s.endGroup();

	s.beginGroup(QStringLiteral("Macro"));
	for( int i = 0; i < kMacros; i++ ){
		macro[i] = unescape(s.value(QStringLiteral("text%1").arg(i + 1), escape(macro[i])).toString());
		macroName[i] = s.value(QStringLiteral("name%1").arg(i + 1), macroName[i]).toString();
	}
	for( int i = 0; i < kInputButtons; i++ ){
		inButton[i] = unescape(s.value(QStringLiteral("in%1").arg(i + 1), escape(inButton[i])).toString());
		inButtonName[i] = s.value(QStringLiteral("inName%1").arg(i + 1), inButtonName[i]).toString();
	}
	s.endGroup();

	s.beginGroup(QStringLiteral("Lists"));
	markList = s.value("mark", markList).toStringList();
	shiftList = s.value("shift", shiftList).toStringList();
	baudList = s.value("baud", baudList).toStringList();
	s.endGroup();

	if( fskLine < 0 || fskLine > 2 ) fskLine = 0;
	if( pttLines < 0 || pttLines > 3 ) pttLines = 1;
	if( demType < 0 || demType > 3 ) demType = 1;
	if( dataBits < 5 || dataBits > 8 ) dataBits = 5;
	if( shift < 1 ) shift = 170;
	if( mark < 300 ) mark = 2125;
	if( baud < 10 ) baud = 45.45;
}

void AppSettings::save(QSettings &s) const
{
	s.beginGroup(QStringLiteral("Station"));
	s.setValue("call", call);
	s.endGroup();

	s.beginGroup(QStringLiteral("Radio"));
	s.setValue("mark", mark);
	s.setValue("shift", shift);
	s.setValue("baud", baud);
	s.setValue("demType", demType);
	s.setValue("afc", afc);
	s.setValue("reverse", reverse);
	s.setValue("uos", uos);
	s.setValue("txUos", txUos);
	s.setValue("bpf", bpf);
	s.setValue("notch", notch);
	s.setValue("atc", atc);
	s.setValue("squelch", squelch);
	s.setValue("squelchLevel", squelchLevel);
	s.setValue("dataBits", dataBits);
	s.setValue("stopSel", stopSel);
	s.setValue("parity", parity);
	s.setValue("txNet", txNet);
	s.setValue("txFixShift", txFixShift);
	s.setValue("defMark", defMark);
	s.setValue("defShift", defShift);
	s.setValue("echo", echo);
	s.setValue("afcMode", afcMode);
	s.setValue("afcSquelch", afcSquelch);
	s.setValue("afcTime", afcTime);
	s.setValue("afcSweep", afcSweep);
	s.endGroup();

	s.beginGroup(QStringLiteral("Spectrum"));
	s.setValue("gain", fftGain);
	s.setValue("resp", fftResp);
	s.setValue("range", fftRange);
	s.setValue("waterfall", showWaterfall);
	s.setValue("xy", showXY);
	s.endGroup();

	s.beginGroup(QStringLiteral("Sound"));
	s.setValue("input", toB64(inputDevice));
	s.setValue("output", toB64(outputDevice));
	s.setValue("channel", inputChannel);
	s.setValue("sampleRate", sampleRate);
	s.setValue("txOffset", txOffset);
	s.setValue("pttPort", pttPort);
	s.setValue("pttLines", pttLines);
	s.setValue("pttInvert", pttInvert);
	s.setValue("fskLine", fskLine);
	s.setValue("fskInvert", fskInvert);
	s.endGroup();

	s.beginGroup(QStringLiteral("Macro"));
	for( int i = 0; i < kMacros; i++ ){
		s.setValue(QStringLiteral("text%1").arg(i + 1), escape(macro[i]));
		s.setValue(QStringLiteral("name%1").arg(i + 1), macroName[i]);
	}
	for( int i = 0; i < kInputButtons; i++ ){
		s.setValue(QStringLiteral("in%1").arg(i + 1), escape(inButton[i]));
		s.setValue(QStringLiteral("inName%1").arg(i + 1), inButtonName[i]);
	}
	s.endGroup();

	s.beginGroup(QStringLiteral("Lists"));
	s.setValue("mark", markList);
	s.setValue("shift", shiftList);
	s.setValue("baud", baudList);
	s.endGroup();
}

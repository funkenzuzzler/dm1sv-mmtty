// Macro text expansion (port of TMmttyWd::ConvString / StoreCWID from the original MMTTY).
//
// Macro text may contain:
//   %m  my call            %c  his call           %n  his name (OM if empty)
//   %q  his QTH            %r  his RST/report     %s  my RST/report
//   %R  first 3 chars of his report   %N  rest of his report   %M  rest of my report
//   %x/%y  his report contest number split at '-'
//   %g  greeting           %f  short greeting
//   %L  LTR shift          %F  FIG shift          %E  end of macro text
//   %D  UTC date           %T  UTC hh:mm          %t  UTC hhmm
//   %{TEXT}  TEXT sent as on-off keyed CW (carrier on/off) with the diddle disabled
// Control characters understood by the transmitter:
//   _  mark (3 bit times)   ~  carrier off (3 bit times)   [  diddle off   ]  diddle on
//   \  (trailing) switch to receive when finished        #  (trailing) repeat while idle
#ifndef MACRO_H
#define MACRO_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

struct MacroContext {
	QString   myCall = QStringLiteral("NOCALL");
	QString   hisCall;
	QString   hisName;
	QString   hisQth;
	QString   hisRst;
	QString   myRst;
	QDateTime utc;		// invalid => QDateTime::currentDateTimeUtc()
};

namespace Macro {

// literalControls == true keeps '\\', "%L" and "%F" in the output so that the string
// can be interpreted later while it is pulled out of the TX FIFO (original "sw" argument).
QByteArray expand(const QString &text, const MacroContext &ctx, bool literalControls = false);

// CW-ID encoding of one character into the _ ~ keying alphabet.
QByteArray cwId(char c);

} // namespace Macro

#endif

#include "Macro.h"

namespace {

// Dot/dash tables from the original: low byte = length, high bits = pattern (1 = dash).
const quint16 kCwTable[] = {
	// 0       1       2       3       4       5       6       7
	0x0005, 0x8005, 0xc005, 0xe005, 0xf005, 0xf805, 0x7805, 0x3805,
	// 8       9       :       ;       <       =       >       ?
	0x1805, 0x0805, 0x0000, 0x0000, 0xe806, 0x7005, 0xA805, 0xcc06,
	// @       A       B       C       D       E       F       G
	0x0000, 0x8002, 0x7004, 0x5004, 0x6003, 0x8001, 0xd004, 0x2003,
	// H       I       J       K       L       M       N       O
	0xf004, 0xc002, 0x8004, 0x4003, 0xb004, 0x0002, 0x4002, 0x0003,
	// P       Q       R       S       T       U       V       W
	0x9004, 0x2004, 0xa003, 0xe003, 0x0001, 0xc003, 0xe004, 0x8003,
	// X       Y       Z
	0x6004, 0x4004, 0x3004, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

const char *kMonths[] = {"", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

QByteArray latin(const QString &s) { return s.toLatin1(); }

} // namespace

QByteArray Macro::cwId(char c)
{
	QByteArray r;
	c = char(toupper(c)) & 0x7f;
	int d, nn;
	if( c == '/' ){
		d = 0x6805;
		nn = d & 0x00ff;
	}
	else if( c == '@' ){
		d = -1;
		nn = 3;
	}
	else if( c >= '0' && c <= 'Z' ){
		d = kCwTable[c - '0'];
		nn = d & 0x00ff;
	}
	else {
		d = -1;
		nn = 5;
	}
	if( d == -1 ){
		r.append(QByteArray(nn, '~'));
	}
	else {
		for( ; nn; nn-- ){
			r.append('_');
			if( !(d & 0x8000) ) r.append("__");
			r.append('~');
			d = (d << 1) & 0xffff;
		}
	}
	r.append("~~");
	return r;
}

QByteArray Macro::expand(const QString &text, const MacroContext &ctx, bool literal)
{
	const QDateTime now = ctx.utc.isValid() ? ctx.utc : QDateTime::currentDateTimeUtc();
	const QByteArray his = latin(ctx.hisRst);
	const QByteArray my = latin(ctx.myRst);
	QByteArray out;

	const QByteArray src = text.toLatin1();
	for( int i = 0; i < src.size(); i++ ){
		const char ch = src[i];
		if( ch != '%' ){
			if( literal || ch != '\\' ) out.append(ch);
			continue;
		}
		if( ++i >= src.size() ) break;
		switch( src[i] ){
			case 'm': out += latin(ctx.myCall); break;
			case 'c': out += latin(ctx.hisCall); break;
			case 'n': out += ctx.hisName.isEmpty() ? QByteArray("OM") : latin(ctx.hisName); break;
			case 'q': out += latin(ctx.hisQth); break;
			case 'r': out += his; break;
			case 's': out += my; break;
			case 'R': out += his.size() >= 3 ? his.left(3) : QByteArray("599"); break;
			case 'N': if( his.size() > 3 ) out += his.mid(3); break;
			case 'M': if( my.size() > 3 ) out += my.mid(3); break;
			case 'g': out += "HELLO"; break;
			case 'f': break;
			case 'L': out += literal ? QByteArray("%L") : QByteArray(1, char(0x1f)); break;
			case 'F': out += literal ? QByteArray("%F") : QByteArray(1, char(0x1b)); break;
			case 'E': return out;
			case 'D':
				out += QString::asprintf("%04d-%s-%02d", now.date().year(), kMonths[now.date().month()], now.date().day()).toLatin1();
				break;
			case 'T': out += QString::asprintf("%02d:%02d", now.time().hour(), now.time().minute()).toLatin1(); break;
			case 't': out += QString::asprintf("%02d%02d", now.time().hour(), now.time().minute()).toLatin1(); break;
			case 'x': {
				const int dash = his.indexOf('-', 3);
				if( his.size() > 3 && dash >= 0 ) out += his.mid(3, dash - 3);
				else if( his.size() > 3 ) out += his.mid(3);
				break;
			}
			case 'y': {
				const int dash = his.indexOf('-', 3);
				if( his.size() > 3 && dash >= 0 ) out += his.mid(dash + 1);
				break;
			}
			case '{': {
				out += "[~";			// disable diddle, carrier off
				for( i++; i < src.size() && src[i] != '}'; i++ ){
					if( src[i] == '%' && i + 1 < src.size() ){
						const QByteArray sub = expand(QString::fromLatin1(src.mid(i, 2)), ctx, false);
						i++;
						for( char c : sub ) out += cwId(c);
					}
					else {
						out += cwId(src[i]);
					}
				}
				break;
			}
			default: out += "%%"; break;
		}
	}
	return out;
}

#include "RemoteProtocol.h"

#include <cstring>

namespace remote {

uint32_t encodeSwitch(const SwitchState &s)
{
	uint32_t d = uint32_t(s.demType) & 0x03;
	if( s.afc ) d |= 0x0004;
	if( s.net ) d |= 0x0008;
	if( s.atc ) d |= 0x0010;
	if( s.bpf ) d |= 0x0020;
	if( s.notch ) d |= 0x0040;
	if( s.sq ) d |= 0x0080;
	if( s.rev ) d |= 0x0100;
	if( s.uos ) d |= 0x0200;
	d |= (uint32_t(s.fixShift) & 0x03) << 10;
	if( s.lpfIir ) d |= 0x1000;
	if( s.lmsNotch ) d |= 0x2000;
	if( s.twoNotch ) d |= 0x4000;
	if( s.viaTx ) d |= 0x8000;
	if( s.wordWrap ) d |= 0x10000;
	d |= (uint32_t(s.sendWay) & 0x03) << 17;
	if( s.dualPeak ) d |= 0x80000;
	return d;
}

SwitchState decodeSwitch(uint32_t d)
{
	SwitchState s;
	s.demType = d & 0x03;
	s.afc = d & 0x0004;
	s.net = d & 0x0008;
	s.atc = d & 0x0010;
	s.bpf = d & 0x0020;
	s.notch = d & 0x0040;
	s.sq = d & 0x0080;
	s.rev = d & 0x0100;
	s.uos = d & 0x0200;
	s.fixShift = (d >> 10) & 0x03;
	s.lpfIir = d & 0x1000;
	s.lmsNotch = d & 0x2000;
	s.twoNotch = d & 0x4000;
	s.viaTx = d & 0x8000;
	s.wordWrap = d & 0x10000;
	s.sendWay = (d >> 17) & 0x03;
	s.dualPeak = d & 0x80000;
	return s;
}

uint32_t encodeView(const ViewState &v)
{
	uint32_t d = uint32_t(v.fftRange) & 0x07;
	d |= (uint32_t(v.fftGain) & 0x03) << 4;
	d |= (uint32_t(v.fftResp - 1) & 0x03) << 6;
	d |= (uint32_t(v.xySize) & 0x01) << 8;
	d |= (uint32_t(v.xyQuality) & 0x03) << 10;
	if( v.xyOn ) d |= 0x1000;
	d |= (uint32_t(v.fftGain) & 0x0c) << 11;
	return d;
}

ViewState decodeView(uint32_t d)
{
	ViewState v;
	v.fftRange = d & 0x07;
	v.fftGain = ((d >> 4) & 0x03) | ((d >> 11) & 0x0c);
	v.fftResp = ((d >> 6) & 0x03) + 1;
	v.xySize = (d >> 8) & 0x01;
	v.xyQuality = (d >> 10) & 0x03;
	v.xyOn = (d & 0x1000) != 0;
	return v;
}

// Port of TMmttyWd::SetRemoteFFT.  With the "auto" gains (4..7) the values are scaled by 0.1.
void exportFft(ComArray &m, const int *fft, int fftSize, double sampFreq, int sampType, int fftGain)
{
	const double scale = fftGain >= 4 ? 0.1 : 1.0;
	double smp = m.smpFFT ? 8000 : 11025;
	auto resample = [&](double rate) {
		for( int i = 0; i < kFftPoints; i++ ){
			const int x = int((i * rate / sampFreq) + 0.5);
			if( x < fftSize ){
				m.arrayFFT[i] = int32_t(fft[x] * scale);
			}
			else {
				for( ; i < kFftPoints; i++ ) m.arrayFFT[i] = 0;
			}
		}
		m.smpFreq = m.smpFFT ? 8000 : 11025;
	};
	if( fftSize < kFftPoints ){
		resample(smp * 0.5);
	}
	else if( sampType || m.smpFFT ){
		resample(smp);
	}
	else {
		for( int i = 0; i < kFftPoints; i++ ) m.arrayFFT[i] = int32_t(fft[i] * scale);
		m.smpFreq = int32_t(sampFreq);
	}
	m.flagFFT = 1;
}

void exportXY(ComArray &m, const double *x, const double *y, bool limiter)
{
	for( int i = 0; i < kXyPoints; i++ ){
		if( limiter ){
			m.arrayX[i] = int32_t(x[i]);
			m.arrayY[i] = int32_t(y[i]);
		}
		else {
			m.arrayX[i] = int32_t(x[i]) * 16384 / 2048;
			m.arrayY[i] = int32_t(y[i]) * 16384 / 2048;
		}
	}
	m.flagXY = 1;
}

int32_t clockErrorPpm(int sampType, double sampFreq)
{
	double nominal;
	switch( sampType ){
		case 1:  nominal = 8000.0; break;
		case 2:  nominal = 6000.0; break;
		case 3:  nominal = 12000.0; break;
		default: nominal = 11025.0; break;
	}
	return int32_t((sampFreq - nominal) * 1.0e6 / nominal + 0.5);
}

QString readString(const char *src, size_t size)
{
	size_t n = 0;
	while( n < size && src[n] ) n++;
	return QString::fromLatin1(src, qsizetype(n));
}

void writeString(char *dst, size_t size, const QString &s)
{
	const QByteArray b = s.toLatin1();
	const size_t n = qMin(size_t(b.size()), size - 1);
	memcpy(dst, b.constData(), n);
	memset(dst + n, 0, size - n);
}

StartOptions parseStartOptions(const QStringList &args)
{
	StartOptions o;
	for( const QString &a : args ){
		bool used = true;
		if( a == QLatin1String("-r") ){
			o.flags = REM_ACTIVE | REM_MENU | REM_CTRL;
			o.clientWidth = 640;
			o.clientHeight = 180;
		}
		else if( a == QLatin1String("-u") ){
			o.flags = REM_ACTIVE | REM_CTRL;
			o.clientWidth = 640;
			o.clientHeight = 150;
		}
		else if( a == QLatin1String("-s") ){
			o.flags = REM_ACTIVE | REM_MENU;
			o.clientWidth = 300;
			o.clientHeight = 180;
		}
		else if( a == QLatin1String("-t") ){
			o.flags = REM_ACTIVE;
			o.clientWidth = 300;
			o.clientHeight = 150;
		}
		else if( a == QLatin1String("-f") ){
			o.fixedBorder = true;
		}
		else if( a == QLatin1String("-m") ){
			o.flags = REM_ACTIVE | REM_SHOWOFF;
		}
		else if( a == QLatin1String("-d") ){
			if( o.active() ){
				o.frameless = true;
				o.flags |= REM_NOMOVE;
			}
		}
		else if( a == QLatin1String("-p") ){
			if( o.active() ) o.flags |= REM_NOPTT;
		}
		else if( a == QLatin1String("-n") ){
			if( o.active() ) o.flags |= REM_NOCNV;
		}
		else if( a == QLatin1String("-a") ){
			if( o.active() ) o.flags |= REM_DISSTAYONTOP;
		}
		else if( a == QLatin1String("-Z") ){
			o.allowMultiple = true;
		}
		else if( a.startsWith(QLatin1String("-h")) && a.size() > 2 && QChar(a[2]).isLetterOrNumber() ){
			bool ok = false;
			int n = 2;
			while( n < a.size() && (a[n].isDigit() || (a[n].toLower() >= QLatin1Char('a') && a[n].toLower() <= QLatin1Char('f'))) ) n++;
			const uint h = a.mid(2, n - 2).toUInt(&ok, 16);
			if( ok ) o.appHandle = h;
			else used = false;
		}
		else if( a.startsWith(QLatin1String("-C")) ){
			if( a.size() > 2 ) o.sharedName = a.mid(2);
		}
		else if( a.startsWith(QLatin1String("-T")) ){
			if( a.size() > 2 ) o.timeoutTicks = a.mid(2).toInt() * 10;
		}
		else {
			used = false;
		}
		if( !used ) o.rest << a;
	}
	return o;
}

} // namespace remote

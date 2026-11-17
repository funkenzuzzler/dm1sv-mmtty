#include "WavFile.h"
#include "AudioConvert.h"

#include <QAudioFormat>
#include <QtEndian>
#include <cstring>

namespace {

struct FmtInfo {
	quint16 tag = 0;
	quint16 channels = 0;
	quint32 rate = 0;
	quint16 bits = 0;
};

bool fail(QString *e, const QString &msg)
{
	if( e ) *e = msg;
	return false;
}

} // namespace

bool WavFile::openPlay(const QString &path, double targetRate, QString *error)
{
	close();
	QFile f(path);
	if( !f.open(QIODevice::ReadOnly) ) return fail(error, QObject::tr("Cannot open %1").arg(path));
	const QByteArray all = f.readAll();
	f.close();
	if( all.size() < 44 || memcmp(all.constData(), "RIFF", 4) != 0 || memcmp(all.constData() + 8, "WAVE", 4) != 0 ){
		return fail(error, QObject::tr("Not a WAV file"));
	}

	FmtInfo fmt;
	bool haveFmt = false;
	const char *data = nullptr;
	qint64 dataLen = 0;
	qint64 off = 12;
	while( off + 8 <= all.size() ){
		const char *ck = all.constData() + off;
		qint64 len = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(ck + 4));
		const char *body = ck + 8;
		if( memcmp(ck, "fmt ", 4) == 0 && len >= 16 ){
			const uchar *b = reinterpret_cast<const uchar *>(body);
			fmt.tag = qFromLittleEndian<quint16>(b);
			fmt.channels = qFromLittleEndian<quint16>(b + 2);
			fmt.rate = qFromLittleEndian<quint32>(b + 4);
			fmt.bits = qFromLittleEndian<quint16>(b + 14);
			if( fmt.tag == 0xFFFE && len >= 26 ) fmt.tag = qFromLittleEndian<quint16>(b + 24);	// WAVE_FORMAT_EXTENSIBLE
			haveFmt = true;
		}
		else if( memcmp(ck, "data", 4) == 0 ){
			data = body;
			dataLen = qMin<qint64>(len, all.size() - (off + 8));
			break;
		}
		off += 8 + len + (len & 1);
	}
	if( !haveFmt || !data || fmt.channels < 1 || fmt.rate == 0 ) return fail(error, QObject::tr("Unsupported WAV layout"));

	QAudioFormat af;
	af.setChannelCount(fmt.channels);
	if( fmt.tag == 1 && fmt.bits == 16 ) af.setSampleFormat(QAudioFormat::Int16);
	else if( fmt.tag == 1 && fmt.bits == 8 ) af.setSampleFormat(QAudioFormat::UInt8);
	else if( fmt.tag == 1 && fmt.bits == 32 ) af.setSampleFormat(QAudioFormat::Int32);
	else if( fmt.tag == 3 && fmt.bits == 32 ) af.setSampleFormat(QAudioFormat::Float);
	else return fail(error, QObject::tr("Unsupported WAV sample format (tag %1, %2 bit)").arg(fmt.tag).arg(fmt.bits));

	std::vector<double> raw;
	AudioConvert::decode(QByteArray::fromRawData(data, int(dataLen)), af, 0, raw);

	m_data.clear();
	if( qAbs(double(fmt.rate) - targetRate) < 0.5 ){
		m_data = std::move(raw);
	}
	else {
		AudioConvert::Resampler rs;
		rs.setRates(double(fmt.rate), targetRate);
		m_data.reserve(size_t(double(raw.size()) * targetRate / fmt.rate) + 16);
		rs.process(raw.data(), raw.size(), m_data);
	}
	m_mode = Play;
	m_pos = 0;
	m_pause = false;
	m_path = path;
	m_rate = targetRate;
	return true;
}

bool WavFile::openRecord(const QString &path, double rate, QString *error)
{
	close();
	m_file.setFileName(path);
	if( !m_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ) return fail(error, QObject::tr("Cannot create %1").arg(path));
	m_rate = rate;
	m_written = 0;
	m_pos = 0;
	m_pause = false;
	m_path = path;
	writeHeader();
	m_mode = Record;
	return true;
}

void WavFile::writeHeader()
{
	const quint32 dataBytes = quint32(m_written * 2);
	const quint32 rate = quint32(m_rate + 0.5);
	uchar h[44];
	memcpy(h, "RIFF", 4);
	qToLittleEndian<quint32>(36 + dataBytes, h + 4);
	memcpy(h + 8, "WAVEfmt ", 8);
	qToLittleEndian<quint32>(16, h + 16);
	qToLittleEndian<quint16>(1, h + 20);
	qToLittleEndian<quint16>(1, h + 22);
	qToLittleEndian<quint32>(rate, h + 24);
	qToLittleEndian<quint32>(rate * 2, h + 28);
	qToLittleEndian<quint16>(2, h + 32);
	qToLittleEndian<quint16>(16, h + 34);
	memcpy(h + 36, "data", 4);
	qToLittleEndian<quint32>(dataBytes, h + 40);
	m_file.seek(0);
	m_file.write(reinterpret_cast<const char *>(h), 44);
	m_file.seek(44 + qint64(dataBytes));
}

void WavFile::close()
{
	if( m_mode == Record && m_file.isOpen() ){
		writeHeader();
		m_file.close();
	}
	m_mode = Closed;
	m_data.clear();
	m_data.shrink_to_fit();
	m_pos = 0;
	m_pause = false;
}

bool WavFile::read(double *dst, int n)
{
	if( m_mode != Play ) return false;
	if( m_pause ){
		memset(dst, 0, sizeof(double) * n);
		return true;
	}
	const qint64 left = qint64(m_data.size()) - m_pos;
	if( left <= 0 ){
		memset(dst, 0, sizeof(double) * n);
		return false;
	}
	const int c = int(qMin<qint64>(left, n));
	memcpy(dst, m_data.data() + m_pos, sizeof(double) * c);
	if( c < n ) memset(dst + c, 0, sizeof(double) * (n - c));
	m_pos += c;
	return true;
}

void WavFile::write(const double *src, int n)
{
	if( m_mode != Record || m_pause ) return;
	QByteArray out;
	out.resize(n * 2);
	for( int i = 0; i < n; i++ ){
		double v = src[i];
		if( v > 32767.0 ) v = 32767.0;
		if( v < -32768.0 ) v = -32768.0;
		qToLittleEndian<qint16>(qint16(v), reinterpret_cast<uchar *>(out.data()) + i * 2);
	}
	m_file.write(out);
	m_written += n;
	m_pos = m_written;
}

void WavFile::seek(qint64 pos)
{
	if( m_mode != Play ) return;
	m_pos = qBound<qint64>(0, pos, qint64(m_data.size()));
}

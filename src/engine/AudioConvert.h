// Sample format helpers used when the sound device cannot be opened in the
// native MMTTY format (11025 Hz / mono / 16 bit) and has to be driven in its
// own preferred format (typically 44.1/48 kHz, stereo, float or int16).
#ifndef AUDIOCONVERT_H
#define AUDIOCONVERT_H

#include <QAudioFormat>
#include <QByteArray>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace AudioConvert {

// Rate converter (single channel).  A 127-tap windowed-sinc low-pass is applied
// on the high-rate side (before decimation or after interpolation) to keep
// aliasing / imaging out of the audio band, then linear interpolation does the
// fractional resampling.
class Resampler {
public:
	void setRates(double rin, double rout)
	{
		m_rin = rin;
		m_rout = rout;
		m_step = rin / rout;
		m_passthrough = std::fabs(rin - rout) < 0.01;
		const double fcRef = 0.42 * (rin < rout ? rin : rout);
		const double fsLP = rin > rout ? rin : rout;
		m_preFilter = rin > rout;
		buildLowpass(fcRef / fsLP);
		reset();
	}
	bool passthrough() const { return m_passthrough; }

	void reset()
	{
		m_t = 0.0;
		m_prev = 0.0;
		m_first = true;
		m_hist.assign(m_h.size(), 0.0);
		m_hp = 0;
	}

	void process(const double *in, size_t n, std::vector<double> &out)
	{
		if( m_passthrough ){
			out.insert(out.end(), in, in + n);
			return;
		}
		if( m_preFilter ){
			for( size_t i = 0; i < n; i++ ) interpolate(lowpass(in[i]), out);
		}
		else {
			const size_t before = out.size();
			for( size_t i = 0; i < n; i++ ) interpolate(in[i], out);
			for( size_t i = before; i < out.size(); i++ ) out[i] = lowpass(out[i]);
		}
	}

private:
	void buildLowpass(double fc)	// fc: cutoff as a fraction of the filter's sample rate
	{
		const int taps = 127;
		m_h.assign(taps, 0.0);
		const double M = taps - 1;
		double sum = 0.0;
		for( int i = 0; i < taps; i++ ){
			const double x = i - M / 2.0;
			const double s = (x == 0.0) ? 2.0 * fc : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
			const double w = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / M);
			m_h[i] = s * w;
			sum += m_h[i];
		}
		for( double &v : m_h ) v /= sum;
	}
	double lowpass(double x)
	{
		const size_t n = m_h.size();
		m_hist[m_hp] = x;
		double acc = 0.0;
		size_t p = m_hp;
		for( size_t i = 0; i < n; i++ ){
			acc += m_h[i] * m_hist[p];
			p = (p == 0) ? n - 1 : p - 1;
		}
		m_hp = (m_hp + 1 == n) ? 0 : m_hp + 1;
		return acc;
	}
	void interpolate(double cur, std::vector<double> &out)
	{
		if( m_first ){
			m_prev = cur;
			m_first = false;
			return;
		}
		while( m_t < 1.0 ){
			out.push_back(m_prev + (cur - m_prev) * m_t);
			m_t += m_step;
		}
		m_t -= 1.0;
		m_prev = cur;
	}

	double m_rin = 1.0, m_rout = 1.0, m_step = 1.0;
	bool   m_passthrough = true;
	bool   m_preFilter = true;
	double m_t = 0.0, m_prev = 0.0;
	bool   m_first = true;
	std::vector<double> m_h, m_hist;
	size_t m_hp = 0;
};

inline int bytesPerSample(QAudioFormat::SampleFormat f)
{
	switch( f ){
		case QAudioFormat::UInt8: return 1;
		case QAudioFormat::Int16: return 2;
		case QAudioFormat::Int32:
		case QAudioFormat::Float: return 4;
		default: return 0;
	}
}

// Decode interleaved device samples to doubles in the +-32768 range, keeping
// only channel `channel`.
inline void decode(const QByteArray &raw, const QAudioFormat &fmt, int channel, std::vector<double> &out)
{
	const int ch = fmt.channelCount();
	const int bps = bytesPerSample(fmt.sampleFormat());
	if( ch < 1 || bps == 0 ) return;
	if( channel >= ch ) channel = 0;
	const int frame = ch * bps;
	const int frames = raw.size() / frame;
	const char *p = raw.constData() + channel * bps;
	for( int i = 0; i < frames; i++, p += frame ){
		double v = 0.0;
		switch( fmt.sampleFormat() ){
			case QAudioFormat::UInt8:
				v = (double(*reinterpret_cast<const uint8_t *>(p)) - 128.0) * 256.0;
				break;
			case QAudioFormat::Int16: {
				int16_t s; memcpy(&s, p, 2); v = s; break; }
			case QAudioFormat::Int32: {
				int32_t s; memcpy(&s, p, 4); v = double(s) / 65536.0; break; }
			case QAudioFormat::Float: {
				float s; memcpy(&s, p, 4); v = double(s) * 32768.0; break; }
			default: break;
		}
		out.push_back(v);
	}
}

// Encode doubles (+-32768) into interleaved device samples (same sample on every channel).
inline void encode(const double *in, size_t n, const QAudioFormat &fmt, QByteArray &out)
{
	const int ch = fmt.channelCount();
	const int bps = bytesPerSample(fmt.sampleFormat());
	if( ch < 1 || bps == 0 ) return;
	const int base = out.size();
	out.resize(base + int(n) * ch * bps);
	char *p = out.data() + base;
	for( size_t i = 0; i < n; i++ ){
		double v = in[i];
		if( v > 32767.0 ) v = 32767.0;
		if( v < -32768.0 ) v = -32768.0;
		for( int c = 0; c < ch; c++, p += bps ){
			switch( fmt.sampleFormat() ){
				case QAudioFormat::UInt8: {
					uint8_t s = uint8_t(int(v / 256.0) + 128); memcpy(p, &s, 1); break; }
				case QAudioFormat::Int16: {
					int16_t s = int16_t(v); memcpy(p, &s, 2); break; }
				case QAudioFormat::Int32: {
					int32_t s = int32_t(v * 65536.0); memcpy(p, &s, 4); break; }
				case QAudioFormat::Float: {
					float s = float(v / 32768.0); memcpy(p, &s, 4); break; }
				default: break;
			}
		}
	}
}

} // namespace AudioConvert

#endif

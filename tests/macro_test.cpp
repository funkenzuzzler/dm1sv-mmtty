// Unit test for macro expansion and the audio resampler.
#include <QByteArray>
#include <QDebug>
#include <cmath>

#include "AudioConvert.h"
#include "Macro.h"
#include "WavFile.h"
#include <QTemporaryDir>

static int failures = 0;
#define CHECK(cond, msg) do { if( !(cond) ){ qWarning("FAIL: %s", msg); failures++; } else qInfo("PASS: %s", msg); } while(0)

int main()
{
	MacroContext c;
	c.myCall = "JA7UDE";
	c.hisCall = "W1AW";
	c.hisName = "";
	c.hisRst = "599001-042";
	c.myRst = "579001";
	c.utc = QDateTime(QDate(2024, 3, 5), QTime(7, 9), QTimeZone::UTC);

	CHECK(Macro::expand("%c DE %m %m K", c) == "W1AW DE JA7UDE JA7UDE K", "basic substitution");
	CHECK(Macro::expand("DEAR %n", c) == "DEAR OM", "empty name falls back to OM");
	c.hisName = "JOE";
	CHECK(Macro::expand("%n", c) == "JOE", "name");
	CHECK(Macro::expand("%R %N", c) == "599 001-042", "report split");
	CHECK(Macro::expand("%x/%y", c) == "001/042", "contest number split at '-'");
	CHECK(Macro::expand("%D %T %t", c) == "2024-MAR-05 07:09 0709", "date and time macros");
	CHECK(Macro::expand("A\\B", c) == "AB", "backslash stripped");
	CHECK(Macro::expand("A\\B", c, true) == "A\\B", "backslash kept in literal mode");
	CHECK(Macro::expand("X%LY%FZ", c) == QByteArray("X\x1f" "Y\x1b" "Z"), "LTR/FIG shift chars");
	CHECK(Macro::expand("X%LY", c, true) == "X%LY", "LTR kept in literal mode");
	CHECK(Macro::expand("AB%ECD", c) == "AB", "%E ends the text");
	CHECK(Macro::expand("100%%", c) == "100%%", "unknown macro passthrough");
	CHECK(Macro::cwId('E') == "_~~~", "CW id for E is one dot");
	CHECK(Macro::cwId('T') == "___~~~", "CW id for T is one dash");
	CHECK(Macro::cwId('A') == "_~___~~~", "CW id for A is dot dash");

	// resampler: 1 kHz tone through 48k -> 11025 -> 48k keeps frequency and amplitude
	{
		const double fin = 48000, fmid = 11025;
		std::vector<double> in(48000), mid, out;
		for( size_t i = 0; i < in.size(); i++ ) in[i] = 10000.0 * sin(2 * M_PI * 1000.0 * i / fin);
		AudioConvert::Resampler a, b;
		a.setRates(fin, fmid);
		b.setRates(fmid, fin);
		a.process(in.data(), in.size(), mid);
		b.process(mid.data(), mid.size(), out);
		CHECK(std::abs(double(mid.size()) - 11025.0) < 4, "downsample produces expected sample count");
		double peak = 0;
		for( size_t i = out.size() / 2; i < out.size() / 2 + 2000; i++ ) peak = std::max(peak, std::abs(out[i]));
		qInfo() << "round trip peak" << peak;
		CHECK(peak > 9000 && peak < 11000, "round-trip amplitude preserved");
		// zero crossings in 1 s ~ 2000
		int zc = 0;
		for( size_t i = 1; i < out.size(); i++ ) if( (out[i-1] < 0) != (out[i] < 0) ) zc++;
		CHECK(std::abs(zc - 2000) < 20, "round-trip frequency preserved");
		// alias check: 7 kHz tone at 48k must be removed when decimating to 11025
		std::vector<double> hi(48000), res;
		for( size_t i = 0; i < hi.size(); i++ ) hi[i] = 10000.0 * sin(2 * M_PI * 7000.0 * i / fin);
		AudioConvert::Resampler c2;
		c2.setRates(fin, fmid);
		c2.process(hi.data(), hi.size(), res);
		double rms = 0;
		for( size_t i = 2000; i < res.size(); i++ ) rms += res[i] * res[i];
		rms = std::sqrt(rms / double(res.size() - 2000));
		qInfo() << "7 kHz alias rms" << rms;
		CHECK(rms < 300, "out-of-band tone is attenuated before decimation");
	}

	// WAV file round trip
	{
		QTemporaryDir dir;
		const QString p = dir.filePath("x.wav");
		WavFile w;
		QString err;
		CHECK(w.openRecord(p, 11025, &err), "wav record");
		std::vector<double> s(11025);
		for( size_t i = 0; i < s.size(); i++ ) s[i] = 8000.0 * sin(2 * M_PI * 1000.0 * i / 11025.0);
		w.write(s.data(), int(s.size()));
		w.close();
		WavFile r;
		CHECK(r.openPlay(p, 11025, &err), "wav play");
		std::vector<double> d(11025);
		r.read(d.data(), int(d.size()));
		double e = 0;
		for( size_t i = 0; i < s.size(); i++ ) e = std::max(e, std::abs(d[i] - s[i]));
		CHECK(e < 1.0, "wav samples identical after round trip");
		WavFile r2;
		CHECK(r2.openPlay(p, 48000, &err), "wav play with resample");
		CHECK(std::abs(double(r2.length()) - 48000.0) < 8, "wav resampled length");
	}
	return failures;
}

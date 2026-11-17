// Loopback test for the ported MMTTY DSP core: text -> Baudot -> AFSK audio -> demodulator -> text.
#include "Rtty.h"
#include <string>
#include <cstdio>

static std::string runLoopback(int demType, const std::string &text, double noiseAmp, double freqErr)
{
	CRTTY rtty;
	CFSKMOD *pmod = new CFSKMOD;
	CFSKDEM *pdem = new CFSKDEM;
	CFSKMOD &mod = *pmod;
	CFSKDEM &dem = *pdem;
	CNoise noise;
	rtty.ClearTX();
	rtty.ClearRX();

	dem.m_type = demType;
	dem.SetBaudRate(45.45);
	dem.SetMarkFreq(2125 + freqErr);
	dem.SetSpaceFreq(2295 + freqErr);
	mod.SetBaudRate(45.45);
	mod.SetMarkFreq(2125);
	mod.SetSpaceFreq(2295);
	mod.ClearTXBuf();
	mod.m_Amp.Reset();
	mod.m_AmpVal = 1;
	mod.OutTone(0, 1024);

	BYTE buf[2048];
	int n = rtty.ConvRTTY(buf, text.c_str());
	for( int i = 0; i < n; i++ ) mod.PutData(buf[i]);

	std::string out;
	const long total = long(SampFreq * (text.size() * 0.18 + 3.0));
	for( long i = 0; i < total; i++ ){
		double s = mod.Do(1);
		if( noiseAmp > 0 ) s += noise.GetNoise() * noiseAmp;
		dem.Do(s);
		int d;
		while( (d = dem.GetData()) >= 0 ){
			char c = rtty.ConvAscii(d);
			if( c && c != '\r' ) out += c;
		}
	}
	delete pmod;
	delete pdem;
	return out;
}

int main()
{
	SampFreq = 11025.0;
	sys.m_SampFreq = SampFreq;
	InitSampType();
	const char *names[] = {"IIR", "FIR", "PLL", "FFT"};
	const std::string text = "RYRYRYRY CQ CQ DE JA7UDE JA7UDE K\r\nTHE QUICK BROWN FOX 0123456789\r\n";
	int fail = 0;
	for( int t = 0; t < 4; t++ ){
		std::string r = runLoopback(t, text, 0.0, 0.0);
		// the receiver needs a few characters to sync; require the tail to be intact
		const std::string tail = "THE QUICK BROWN FOX 0123456789";
		bool ok = r.find(tail) != std::string::npos;
		printf("[%s] %-4s decoded: \"%s\"\n", ok ? "PASS" : "FAIL", names[t], r.c_str());
		if( !ok ) fail++;
	}
	return fail;
}

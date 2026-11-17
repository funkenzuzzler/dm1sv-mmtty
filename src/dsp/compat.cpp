#include "compat.h"

#ifndef _WIN32
#include <chrono>
#include <thread>
void Sleep(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
#endif

SYSSET sys;

double SampFreq = 11025.0;
double SampBase = 11025.0;
double DemSamp  = 11025.0 * 0.5;
int    SampType = 0;
int    DemOver  = 1;
int    FFT_SIZE = 2048;
int    SampSize = 1024;

int FSKCount = 0, FSKCount1 = 0, FSKCount2 = 0, FSKDeff = 0;

void InitSampType(void)
{
	if( SampFreq >= 11600.0 ){
		SampType = 3;
		SampBase = 12000.0;
		DemSamp = SampFreq * 0.5;
		DemOver = 1;
		FFT_SIZE = 2048;
		SampSize = (12000*1024)/11025;
	}
	else if( SampFreq >= 10000.0 ){
		SampType = 0;
		SampBase = 11025.0;
		DemSamp = SampFreq * 0.5;
		DemOver = 1;
		FFT_SIZE = 2048;
		SampSize = 1024;
	}
	else if( SampFreq >= 7000.0 ){
		SampType = 1;
		SampBase = 8000.0;
		DemSamp = SampFreq;
		DemOver = 0;
		FFT_SIZE = 1024;
		SampSize = (8000*1024)/11025;
	}
	else if( SampFreq >= 5000.0 ){
		SampType = 2;
		SampBase = 6000.0;
		DemSamp = SampFreq;
		DemOver = 0;
		FFT_SIZE = 1024;
		SampSize = (6000*1024)/11025;
	}
}

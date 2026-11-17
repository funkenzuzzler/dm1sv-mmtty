// Compatibility layer between the original MMTTY (Borland VCL / Win32) DSP
// sources and a portable Qt build.  Provides Win32 type names and the small
// set of global parameters the DSP classes read.
#ifndef DSP_COMPAT_H
#define DSP_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdint.h>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
typedef int            BOOL;
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef uint32_t       DWORD;
typedef const char    *LPCSTR;
#  ifndef TRUE
#    define TRUE  1
#    define FALSE 0
#  endif
// No-ops: memory locking is not required for the DSP tables on desktop OSes.
inline BOOL VirtualLock(void *, size_t)   { return 1; }
inline BOOL VirtualUnlock(void *, size_t) { return 1; }
void Sleep(unsigned ms);
#endif

// The original classes lived inside a VCL TThread (TObject), which zero-fills
// every instance before the constructor runs.  Several DSP members are never
// explicitly initialised and rely on that.  Classes that are used as top-level
// objects declare DSP_ZERO_ALLOC so that heap allocation (new) behaves the same.
// They must therefore always be created with `new`.
#define DSP_ZERO_ALLOC \
	public: \
	static void *operator new(size_t n){ void *p = ::operator new(n); memset(p, 0, n); return p; } \
	static void operator delete(void *p){ ::operator delete(p); }

#ifndef MAXDOUBLE
#define MAXDOUBLE DBL_MAX
#endif

// TX port selection (as in the original ComLib.h)
enum {
	txSound,
	txTXD,
	txTXDOnly,
};

// Subset of the original SYSSET struct that is consumed by the DSP classes and the engine.
struct SYSSET {
	int    m_TxPort     = txSound;
	int    m_CodeSet    = 0;      // 1: Japanese (J-BELL) code set
	int    m_txuos      = 1;      // unshift on space (TX)
	int    m_dblsft     = 0;      // send FIG/LTR twice
	int    m_LWait      = 0;
	int    m_FFTGain    = 1;
	int    m_FFTResp    = 2;
	double m_SampFreq   = 11025.0;
	double m_TxOffset   = 0.0;

	int    m_echo       = 1;      // 0: direct echo, 1: internal sound loopback, 2: external (RX stays open)
	int    m_AFC        = 1;
	int    m_FixShift   = 1;      // 0 free, 1 fixed shift, 2 HAM, 3 FSK
	int    m_AFCSQ      = 32;
	double m_AFCTime    = 8.0;
	double m_AFCSweep   = 1.0;
	int    m_AFCNet     = 0;

	int    m_TxNet      = 1;      // TX frequency follows RX
	int    m_TxFixShift = 0;
	int    m_TxDisRev   = 0;
	int    m_Rev        = 0;
	double m_DefMarkFreq = 2125.0;
	double m_DefShift   = 170.0;
	double m_LimitGain  = 200.0;
};
extern SYSSET sys;

#define MARKL  300
#define SPACEH 2700

extern double SampFreq;   // sampling frequency used by the demodulator front end
extern double SampBase;   // sound device sampling frequency
extern double DemSamp;    // demodulator sampling frequency
extern int    SampType;
extern int    DemOver;    // 1: decimate by 2 before the demodulator
extern int    FFT_SIZE;
extern int    SampSize;   // block size in samples

extern int FSKCount, FSKCount1, FSKCount2, FSKDeff;

// Selects SampBase/DemSamp/FFT_SIZE/SampSize from SampFreq (same table as the original).
void InitSampType(void);

#endif

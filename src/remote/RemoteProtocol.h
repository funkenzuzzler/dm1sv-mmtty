// "MMTTY Engine Remote Mode API": message numbers, shared memory layout and the bit packing of the
// switch / view words.  Platform independent; the Windows transport lives in RemoteWin.
//
// Reference: eremote.txt of the original MMTTY distribution.
#ifndef REMOTEPROTOCOL_H
#define REMOTEPROTOCOL_H

#include <QString>
#include <QStringList>
#include <cstddef>
#include <cstdint>

namespace remote {

// ---- messages: PostMessage(hwnd, RegisterWindowMessage("MMTTY"), wParam = message, lParam = value) ----
enum Rxm : unsigned {		// application -> MMTTY
	RXM_HANDLE = 0,
	RXM_REQHANDLE,
	RXM_EXIT,
	RXM_PTT,
	RXM_CHAR,
	RXM_WINPOS,				// 5
	RXM_WIDTH,
	RXM_REQPARA,
	RXM_SETBAUD,
	RXM_SETMARK,
	RXM_SETSPACE,			// 10
	RXM_SETSWITCH,
	RXM_SETHAM,
	RXM_SHOWSETUP,
	RXM_SETVIEW,
	RXM_SETSQLVL,			// 15
	RXM_SHOW,
	RXM_SETFIG,
	RXM_SETRESO,
	RXM_SETLPF,
	RXM_SETTXDELAY,			// 20
	RXM_UPDATECOM,
	RXM_SUSPEND,
	RXM_NOTCH,
	RXM_PROFILE,
	RXM_TIMER,				// 25
	RXM_ENBFOCUS,
	RXM_SETDEFFREQ,
	RXM_SETLENGTH,
	RXM_ENBSHARED,
	RXM_PTTFSK,				// 30
	RXM_SOUNDSOURCE,		// 31
};

enum Txm : unsigned {		// MMTTY -> application
	TXM_HANDLE = 0x8000,
	TXM_REQHANDLE,
	TXM_START,
	TXM_CHAR,
	TXM_PTTEVENT,
	TXM_HEIGHT,
	TXM_BAUD,
	TXM_MARK,
	TXM_SPACE,
	TXM_SWITCH,
	TXM_VIEW,
	TXM_LEVEL,
	TXM_FIGEVENT,
	TXM_RESO,
	TXM_LPF,
	TXM_THREAD,
	TXM_PROFILE,
	TXM_NOTCH,
	TXM_DEFSHIFT,
	TXM_RADIOFREQ,
	TXM_SHOWSETUP,
	TXM_SHOWPROFILE,
};

// ---- remote mode flags (original ComLib.h REM*) ----
enum : unsigned {
	REM_ACTIVE = 0x80000000u,
	REM_MENU = 1,
	REM_CTRL = 2,
	REM_SHOWOFF = 4,
	REM_NOMOVE = 8,
	REM_NOPTT = 16,
	REM_NOCNV = 32,
	REM_VIATX = 64,
	REM_DISFOCUS = 128,
	REM_DISSHARED = 256,
	REM_DISSTAYONTOP = 512,
};

constexpr int kFftPoints = 2048;
constexpr int kXyPoints = 512;
constexpr int kProfiles = 16;
constexpr int kProfileNameLen = 64;

// ---- shared memory, created by the application and opened by MMTTY ----
#pragma pack(push, 1)
struct ComArray {
	int32_t smpFreq;			// FFT sampling frequency (-> app)
	char    title[128];			// control panel title (-> MMTTY)
	char    comName[16];		// PTT / FSK port name (-> MMTTY)
	int32_t smpFFT;				// 0: 11025 Hz, 1: 8000 Hz base (-> MMTTY)
	int32_t flagFFT;			// 0: MMTTY may write arrayFFT, 1: new data for the application
	int32_t arrayFFT[kFftPoints];
	int32_t flagXY;
	int32_t arrayX[kXyPoints];
	int32_t arrayY[kXyPoints];
	char    verMMTTY[16];
	char    comRadio[16];
	int32_t flagLostSound;
	int32_t flagOverflow;
	int32_t errorClock;			// ppm
	int32_t smpDemFreq;
	int32_t txBuffCount;
	char    ProfileName[kProfiles][kProfileNameLen];
};
#pragma pack(pop)

static_assert(offsetof(ComArray, title) == 4, "ComArray layout");
static_assert(offsetof(ComArray, comName) == 132, "ComArray layout");
static_assert(offsetof(ComArray, smpFFT) == 148, "ComArray layout");
static_assert(offsetof(ComArray, flagFFT) == 152, "ComArray layout");
static_assert(offsetof(ComArray, arrayFFT) == 156, "ComArray layout");
static_assert(offsetof(ComArray, flagXY) == 8348, "ComArray layout");
static_assert(offsetof(ComArray, verMMTTY) == 12448, "ComArray layout");
static_assert(offsetof(ComArray, ProfileName) == 12500, "ComArray layout");
static_assert(sizeof(ComArray) == 13524, "ComArray layout");

// ---- switch word (RXM_SETSWITCH / TXM_SWITCH) ----
struct SwitchState {
	int  demType = 0;			// b0-b1
	bool afc = false;			// b2
	bool net = false;			// b3
	bool atc = false;			// b4
	bool bpf = false;			// b5
	bool notch = false;			// b6  LMS / notch filter on
	bool sq = false;			// b7
	bool rev = false;			// b8
	bool uos = false;			// b9
	int  fixShift = 0;			// b10-b11  AFC shift algorithm
	bool lpfIir = false;		// b12  integrator: 0 FIR, 1 IIR
	bool lmsNotch = false;		// b13  0 LMS, 1 notch
	bool twoNotch = false;		// b14
	bool viaTx = false;			// b15  RXM_CHAR goes through the key buffer
	bool wordWrap = false;		// b16
	int  sendWay = 0;			// b17-b18  0 character, 1 word, 2 line
	bool dualPeak = false;		// b19
};
uint32_t encodeSwitch(const SwitchState &s);
SwitchState decodeSwitch(uint32_t d);

// ---- view word (RXM_SETVIEW / TXM_VIEW) ----
struct ViewState {
	int  fftRange = 0;			// b0-b2
	int  fftGain = 0;			// b4-b5 and b13-b14
	int  fftResp = 1;			// b6-b7  (stored minus one)
	int  xySize = 0;			// b8
	int  xyQuality = 0;			// b10-b11
	bool xyOn = false;			// b12
};
uint32_t encodeView(const ViewState &v);
ViewState decodeView(uint32_t d);

// ---- shared memory helpers ----
// Copies the FFT result into arrayFFT (resampled to the 11025 / 8000 Hz base the application asked for) and sets flagFFT.
void exportFft(ComArray &m, const int *fft, int fftSize, double sampFreq, int sampType, int fftGain);
// Copies the XY scope points into arrayX / arrayY and sets flagXY.
void exportXY(ComArray &m, const double *x, const double *y, bool limiter);
// Sound card clock error in ppm against the nominal rate of the sampling type.
int32_t clockErrorPpm(int sampType, double sampFreq);
QString readString(const char *src, size_t size);
void writeString(char *dst, size_t size, const QString &s);

// ---- command line (single dash, multi-letter options: not handled by QCommandLineParser) ----
struct StartOptions {
	unsigned flags = 0;			// REM_* ; REM_ACTIVE set = engine remote mode
	int      clientWidth = 0;	// 0: not specified
	int      clientHeight = 0;
	bool     fixedBorder = false;	// -f
	bool     frameless = false;		// -d (remote mode only)
	bool     allowMultiple = false;	// -Z
	unsigned appHandle = 0;			// -hXXXX (hex); 0 = broadcast
	QString  sharedName = QStringLiteral("MMTTY");	// -Cname
	int      timeoutTicks = 100;	// -Tsec, in 100 ms ticks; 0 = never time out
	QStringList rest;				// arguments that are not remote options

	bool active() const { return (flags & REM_ACTIVE) != 0; }
};
// args: the command line without the program name.  Options are applied in order, as in the original
// (-d -p -n -a only count after one of -r -u -s -t -m, and each of those resets the flags).
StartOptions parseStartOptions(const QStringList &args);

} // namespace remote

#endif

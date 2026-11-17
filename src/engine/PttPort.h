// Serial-port control lines: PTT and software FSK keying.
//
//  * PTT:  RTS and/or DTR are switched when the transmitter is keyed (as before).
//  * FSK:  one control line (DTR or RTS) is toggled by software with accurate timing
//          (see FskKeyer) and the radio's FSK input is wired to it.  The FSK line is
//          excluded from the PTT lines, so DTR = FSK / RTS = PTT is possible on one port.
#ifndef PTTPORT_H
#define PTTPORT_H

#include <QSerialPort>
#include <QString>
#include <QStringList>

class PttPort
{
public:
	enum Line { None = 0, Rts = 1, Dtr = 2, RtsDtr = 3 };

	struct Config {
		QString portName;       // "COM3", "/dev/ttyUSB0" ...; empty = no serial port
		int     lines = Rts;    // PTT lines (Line flags, None = no PTT line)
		bool    invert = false; // true: PTT line is low while transmitting
		int     fskLine = None; // None = AFSK through the sound card, Rts / Dtr = software FSK on that line
		bool    fskInvert = false; // false: mark = line ON, space = line OFF
	};

	bool open(const Config &cfg, QString *error = nullptr);
	void close();
	bool isOpen() const { return m_port.isOpen(); }
	void setPtt(bool tx);

	// Sets the FSK line.  Safe to call from any thread while the port is open (no QSerialPort
	// involved: it uses the native handle), but not while open()/close() run.
	void setFsk(bool mark) const;
	bool fskConfigured() const { return m_cfg.fskLine == Rts || m_cfg.fskLine == Dtr; }

	static QStringList availablePorts();

private:
	QSerialPort m_port;
	Config      m_cfg;
	qintptr     m_handle = -1;		// native descriptor / HANDLE of the open port
};

#endif

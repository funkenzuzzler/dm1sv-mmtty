#include "PttPort.h"

#include <QSerialPortInfo>

#if defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/ioctl.h>
#endif

bool PttPort::open(const Config &cfg, QString *error)
{
	close();
	m_cfg = cfg;
	const bool fsk = cfg.fskLine == Rts || cfg.fskLine == Dtr;
	if( cfg.portName.isEmpty() || (cfg.lines == None && !fsk) ) return true;	// disabled: not an error

	m_port.setPortName(cfg.portName);
	if( !m_port.open(QIODevice::ReadWrite) ){
		if( error ){
			QString msg = QObject::tr("Cannot open PTT port %1: %2").arg(cfg.portName, m_port.errorString());
#ifdef Q_OS_LINUX
			if( m_port.error() == QSerialPort::PermissionError )
				msg += QObject::tr(" (add your user to the 'dialout' group)");
#endif
			*error = msg;
		}
		return false;
	}
	m_port.setFlowControl(QSerialPort::NoFlowControl);
	m_handle = qintptr(m_port.handle());
	setPtt(false);		// opening a port usually raises DTR/RTS: force them to the idle state
	if( fsk ) setFsk(true);	// FSK line idles at mark
	return true;
}

void PttPort::close()
{
	if( m_port.isOpen() ){
		setPtt(false);
		setFsk(true);			// leave the FSK line at mark
		m_port.close();
	}
	m_handle = -1;
}

void PttPort::setPtt(bool tx)
{
	if( !m_port.isOpen() ) return;
	const bool level = m_cfg.invert ? !tx : tx;
	const int lines = m_cfg.lines & ~m_cfg.fskLine;		// the FSK line is never used for PTT
	if( lines & Rts ) m_port.setRequestToSend(level);
	if( lines & Dtr ) m_port.setDataTerminalReady(level);
}

void PttPort::setFsk(bool mark) const
{
	if( m_handle < 0 || !fskConfigured() ) return;
	const bool on = m_cfg.fskInvert ? !mark : mark;
#if defined(Q_OS_WIN)
	const DWORD fn = (m_cfg.fskLine == Dtr) ? (on ? SETDTR : CLRDTR) : (on ? SETRTS : CLRRTS);
	EscapeCommFunction(HANDLE(m_handle), fn);
#else
	int bit = (m_cfg.fskLine == Dtr) ? TIOCM_DTR : TIOCM_RTS;
	ioctl(int(m_handle), on ? TIOCMBIS : TIOCMBIC, &bit);
#endif
}

QStringList PttPort::availablePorts()
{
	QStringList l;
	for( const QSerialPortInfo &i : QSerialPortInfo::availablePorts() ) l << i.portName();
	return l;
}

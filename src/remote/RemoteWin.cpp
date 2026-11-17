#include "RemoteWin.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

RemoteWin::RemoteWin(QWidget *window, unsigned appHandle)
	: m_window(window), m_app(appHandle)
{
	m_message = ::RegisterWindowMessageA("MMTTY");
}

RemoteWin::~RemoteWin()
{
	if( m_view ) ::UnmapViewOfFile(m_view);
	if( m_map ) ::CloseHandle(static_cast<HANDLE>(m_map));
}

// Called for window messages and for thread messages (PostThreadMessage, hwnd == nullptr) alike.
bool RemoteWin::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
	if( !m_api || m_message == 0 ) return false;
	if( eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG" ) return false;
	const MSG *msg = static_cast<const MSG *>(message);
	if( msg->message != m_message ) return false;
	m_api->handle(unsigned(msg->wParam), qint64(msg->lParam));
	if( result ) *result = 0;
	return true;
}

bool RemoteWin::anotherInstanceRunning()
{
	// The handle is intentionally never closed: it lives as long as this process.
	HANDLE h = ::CreateMutexA(nullptr, FALSE, "Local\\mmtty-qt-engine-remote");
	return h != nullptr && ::GetLastError() == ERROR_ALREADY_EXISTS;
}

void RemoteWin::postToApp(unsigned message, qint64 lParam)
{
	if( m_app ) ::PostMessageA(reinterpret_cast<HWND>(m_app), m_message, WPARAM(message), LPARAM(lParam));
}

void RemoteWin::postRaw(unsigned message, qint64 lParam)
{
	::PostMessageA(m_app ? reinterpret_cast<HWND>(m_app) : HWND_BROADCAST, m_message, WPARAM(message), LPARAM(lParam));
}

bool RemoteWin::appKnown() const
{
	return m_app != 0;
}

void RemoteWin::setApp(quintptr handle)
{
	m_app = handle;
}

quintptr RemoteWin::windowHandle() const
{
	return quintptr(m_window->winId());
}

unsigned RemoteWin::threadId() const
{
	return unsigned(::GetCurrentThreadId());
}

remote::ComArray *RemoteWin::openShared(const QString &name)
{
	if( m_view ) return static_cast<remote::ComArray *>(m_view);
	HANDLE map = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.toLatin1().constData());
	if( !map ) return nullptr;
	void *view = ::MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	MEMORY_BASIC_INFORMATION mbi;
	if( !view || !::VirtualQuery(view, &mbi, sizeof(mbi)) || mbi.RegionSize < sizeof(remote::ComArray) ){
		if( view ) ::UnmapViewOfFile(view);		// too small to be an MMTTY block: do not write into it
		::CloseHandle(map);
		return nullptr;
	}
	m_map = map;
	m_view = view;
	return static_cast<remote::ComArray *>(view);
}

void RemoteWin::activateApp()
{
	if( m_app ) ::SetForegroundWindow(reinterpret_cast<HWND>(m_app));
}

void RemoteWin::enableApp(bool enable)
{
	if( m_app ) ::EnableWindow(reinterpret_cast<HWND>(m_app), enable ? TRUE : FALSE);
}

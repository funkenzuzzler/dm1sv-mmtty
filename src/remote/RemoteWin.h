// Windows transport of the engine remote mode: registered window message "MMTTY", window handles and the
// shared memory block.  Only built on Windows.
#ifndef REMOTEWIN_H
#define REMOTEWIN_H

#include <QAbstractNativeEventFilter>
#include <QWidget>

#include "RemoteApi.h"

class RemoteWin : public QAbstractNativeEventFilter, public RemoteHost
{
public:
	// appHandle: the -hXXXX option (0 = broadcast to all top-level windows until RXM_HANDLE arrives)
	RemoteWin(QWidget *window, unsigned appHandle);
	~RemoteWin() override;

	void attach(RemoteApi *api) { m_api = api; }
	bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

	// Without -Z only one engine remote instance may run (the original refuses to start next to a hidden one).
	static bool anotherInstanceRunning();

	// RemoteHost
	void postToApp(unsigned message, qint64 lParam) override;
	void postRaw(unsigned message, qint64 lParam) override;
	bool appKnown() const override;
	void setApp(quintptr handle) override;
	quintptr windowHandle() const override;
	unsigned threadId() const override;
	remote::ComArray *openShared(const QString &name) override;
	void activateApp() override;
	void enableApp(bool enable) override;

private:
	QWidget *m_window;
	RemoteApi *m_api = nullptr;
	unsigned m_message = 0;		// RegisterWindowMessage("MMTTY")
	quintptr m_app = 0;			// 0 = HWND_BROADCAST
	void *m_map = nullptr;		// file mapping handle
	void *m_view = nullptr;
};

#endif

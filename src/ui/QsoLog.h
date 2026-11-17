// Minimal QSO log: appends records to an ADIF (.adi) file, the interchange format
// understood by all common ham-radio logging programs.
#ifndef QSOLOG_H
#define QSOLOG_H

#include <QDateTime>
#include <QString>

class QsoLog
{
public:
	struct Entry {
		QDateTime utc;
		QString call, name, qth, rstSent, rstRcvd, myCall;
		double  audioMark = 0;		// RTTY mark tone, informational only
	};

	explicit QsoLog(const QString &path = defaultPath()) : m_path(path) {}

	static QString defaultPath();
	const QString &path() const { return m_path; }
	bool append(const Entry &e, QString *error = nullptr) const;

	// "<TAG:len>value" ADIF field
	static QString field(const char *tag, const QString &value);

private:
	QString m_path;
};

#endif

#include "QsoLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

QString QsoLog::defaultPath()
{
	QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if( dir.isEmpty() ) dir = QDir::homePath();
	return QDir(dir).filePath(QStringLiteral("mmtty.adi"));
}

QString QsoLog::field(const char *tag, const QString &value)
{
	if( value.isEmpty() ) return QString();
	return QStringLiteral("<%1:%2>%3 ").arg(QLatin1String(tag)).arg(value.toUtf8().size()).arg(value);
}

bool QsoLog::append(const Entry &e, QString *error) const
{
	QDir().mkpath(QFileInfo(m_path).absolutePath());
	const bool isNew = !QFileInfo::exists(m_path) || QFileInfo(m_path).size() == 0;
	QFile f(m_path);
	if( !f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) ){
		if( error ) *error = f.errorString();
		return false;
	}
	QTextStream out(&f);
	out.setEncoding(QStringConverter::Utf8);
	if( isNew ){
		out << "MMTTY (Qt) QSO log\n"
		    << field("ADIF_VER", QStringLiteral("3.1.0"))
		    << field("PROGRAMID", QStringLiteral("MMTTY-Qt")) << "<EOH>\n";
	}
	const QDateTime t = e.utc.isValid() ? e.utc : QDateTime::currentDateTimeUtc();
	out << field("CALL", e.call)
	    << field("QSO_DATE", t.toString(QStringLiteral("yyyyMMdd")))
	    << field("TIME_ON", t.toString(QStringLiteral("HHmmss")))
	    << field("MODE", QStringLiteral("RTTY"))
	    << field("RST_SENT", e.rstSent)
	    << field("RST_RCVD", e.rstRcvd)
	    << field("NAME", e.name)
	    << field("QTH", e.qth)
	    << field("STATION_CALLSIGN", e.myCall)
	    << "<EOR>\n";
	out.flush();
	if( f.error() != QFileDevice::NoError ){
		if( error ) *error = f.errorString();
		return false;
	}
	return true;
}

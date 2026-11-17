// Minimal WAV reader / writer used for sound-file playback and recording.
#ifndef WAVFILE_H
#define WAVFILE_H

#include <QFile>
#include <QString>
#include <vector>

class WavFile
{
public:
	enum Mode { Closed = 0, Play = 1, Record = 2 };

	// Loads the whole file and converts it (first channel only) to `targetRate`.
	bool openPlay(const QString &path, double targetRate, QString *error = nullptr);
	bool openRecord(const QString &path, double rate, QString *error = nullptr);
	void close();

	Mode mode() const { return m_mode; }
	bool paused() const { return m_pause; }
	void setPaused(bool p) { m_pause = p; }

	// Play: fills dst with n samples (zero padded at the end). Returns false once the
	// end of the file has been reached and the block contained no more data.
	bool read(double *dst, int n);
	// Record: appends n samples.
	void write(const double *src, int n);

	qint64 position() const { return m_pos; }
	qint64 length() const { return m_mode == Play ? qint64(m_data.size()) : m_written; }
	void seek(qint64 pos);
	const QString &path() const { return m_path; }

private:
	void writeHeader();

	Mode m_mode = Closed;
	bool m_pause = false;
	QString m_path;
	std::vector<double> m_data;
	qint64 m_pos = 0;
	QFile m_file;
	qint64 m_written = 0;
	double m_rate = 11025.0;
};

#endif

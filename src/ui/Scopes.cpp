#include "Scopes.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <algorithm>
#include <cmath>

#include "compat.h"

namespace {

constexpr double kSqScale = 1024.0;		// SQSCALE of the original

// x position (pixel) of a frequency, same mapping as the original DrawFFT()
int freqToX(double f, const Engine::Spectrum &sp, int width)
{
	if( sp.fm <= 0 ) return 0;
	return int(((f - sp.low) * FFT_SIZE * double(width)) / (SampFreq * sp.fm) + 0.5);
}

void drawNotch(QPainter &p, int x, int yb)
{
	p.setPen(Qt::yellow);
	p.setBrush(Qt::yellow);
	const QPoint tri[3] = {QPoint(x, yb), QPoint(x - 3, yb - 5), QPoint(x + 3, yb - 5)};
	p.drawPolygon(tri, 3);
}

// Waterfall palette: black - blue - cyan - yellow - red
QRgb waterColor(int v)
{
	v = std::clamp(v, 0, 255);
	int r, g, b;
	if( v < 64 )       { r = 0;                g = 0;                b = v * 4; }
	else if( v < 128 ) { r = 0;                g = (v - 64) * 4;     b = 255; }
	else if( v < 192 ) { r = (v - 128) * 4;    g = 255;              b = 255 - (v - 128) * 4; }
	else               { r = 255;              g = 255 - (v - 192) * 4; b = 0; }
	return qRgb(r, g, b);
}

} // namespace

// ===========================================================================
SpectrumView::SpectrumView(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(60);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setCursor(Qt::CrossCursor);
	setToolTip(tr("Left click: tune mark frequency\nRight click: place notch filter"));
}

void SpectrumView::setFrame(const Engine::Spectrum &sp)
{
	m_sp = sp;
	update();
}

void SpectrumView::setOverlay(const QString &text)
{
	if( text == m_overlay ) return;
	m_overlay = text;
	update();
}

void SpectrumView::mousePressEvent(QMouseEvent *e)
{
	emit clicked(int(e->position().x()), width(), e->button());
}

void SpectrumView::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), Qt::black);
	const int W = width(), H = height();
	const int yb = H - 1;
	if( m_sp.window <= 0 || m_sp.fm <= 0 ) return;

	// mark / space cursors
	p.setPen(Qt::yellow);
	int x = freqToX(m_sp.mark, m_sp, W);
	p.drawLine(x, 0, x, yb);
	x = freqToX(m_sp.space, m_sp, W);
	p.drawLine(x, 0, x, yb);

	// spectrum trace
	const int offx = int((m_sp.low * FFT_SIZE / SampFreq) + ((m_sp.fm / double(W)) / 2.0) + 0.5);
	double k;
	switch( m_sp.fftGain ){
		case 4:  k = double(yb) * 0.85 / m_fftMax; break;
		case 5:  k = double(yb) * 1.0  / m_fftMax; break;
		case 6:  k = double(yb) * 1.2  / m_fftMax; break;
		case 7:  k = double(yb) * 1.5  / m_fftMax; break;
		default: k = double(yb) / 256.0; break;
	}
	QPolygonF line;
	line.reserve(W);
	int maxv = 0;
	for( int i = 0; i < W; i++ ){
		const int bin = (i * m_sp.fm) / W + offx;
		double d = 0;
		if( bin >= 0 && bin < m_sp.window && bin < int(m_sp.bins.size()) ){
			const int v = m_sp.bins[bin];
			if( v > maxv ) maxv = v;
			d = v * k;
		}
		if( d >= yb ) d = yb - 1;
		if( d < 0 ) d = 0;
		line << QPointF(i, yb - d);
	}
	p.setPen(Qt::white);
	p.drawPolyline(line);
	if( m_sp.fftGain >= 4 ){		// auto gain, as in the original
		m_fftSumMax = m_fftSumMax - m_fftMax + maxv;
		if( m_fftSumMax < 64 ) m_fftSumMax = 64;
		m_fftMax = std::max(1, m_fftSumMax / 2);
	}

	if( m_sp.notch1 > 0 ) drawNotch(p, freqToX(m_sp.notch1, m_sp, W), yb);
	if( m_sp.notch2 > 0 ) drawNotch(p, freqToX(m_sp.notch2, m_sp, W), yb);

	p.setPen(Qt::white);
	if( !m_overlay.isEmpty() ) p.drawText(4, 14, m_overlay);
	if( m_sp.lostSound ) p.drawText(W - 90, 14, tr("Lost sound"));
	else if( m_sp.overflow ) p.drawText(W - 70, 14, tr("Overflow"));
}

// ===========================================================================
WaterfallView::WaterfallView(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(40);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setCursor(Qt::CrossCursor);
	setToolTip(tr("Left click: tune mark frequency"));
}

void WaterfallView::resizeEvent(QResizeEvent *)
{
	QImage n(std::max(1, width()), std::max(1, height()), QImage::Format_RGB32);
	n.fill(Qt::black);
	m_img = n;
}

void WaterfallView::addFrame(const Engine::Spectrum &sp)
{
	m_last = sp;
	if( m_img.isNull() || sp.window <= 0 || sp.fm <= 0 ) return;
	const int W = m_img.width(), H = m_img.height();
	// scroll down one line
	for( int y = H - 1; y > 0; y-- ) memcpy(m_img.scanLine(y), m_img.scanLine(y - 1), size_t(W) * 4);
	QRgb *row = reinterpret_cast<QRgb *>(m_img.scanLine(0));
	const int offx = int((sp.low * FFT_SIZE / SampFreq) + ((sp.fm / double(W)) / 2.0) + 0.5);
	for( int i = 0; i < W; i++ ){
		const int bin = (i * sp.fm) / W + offx;
		int v = 0;
		if( bin >= 0 && bin < sp.window && bin < int(sp.bins.size()) ) v = sp.bins[bin];
		row[i] = waterColor(v);
	}
	update();
}

void WaterfallView::mousePressEvent(QMouseEvent *e)
{
	emit clicked(int(e->position().x()), width(), e->button());
}

void WaterfallView::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), Qt::black);
	if( !m_img.isNull() ) p.drawImage(0, 0, m_img);
	if( m_last.fm > 0 ){
		p.setPen(Qt::yellow);
		const int h = height();
		int x = freqToX(m_last.mark, m_last, width());
		p.drawLine(x, 0, x, h);
		x = freqToX(m_last.space, m_last, width());
		p.drawLine(x, 0, x, h);
	}
}

// ===========================================================================
XYView::XYView(QWidget *parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	setToolTip(tr("XY scope: mark (X) against space (Y)"));
}

void XYView::clear()
{
	m_x.clear();
	m_y.clear();
	update();
}

void XYView::setData(const std::vector<double> &mark, const std::vector<double> &space, bool limiter, bool invert)
{
	m_x = mark;
	m_y = space;
	m_invert = invert;
	double dmax = 0;
	for( double v : m_x ) dmax = std::max(dmax, std::fabs(v));
	for( double v : m_y ) dmax = std::max(dmax, std::fabs(v));
	m_dmax = std::max(dmax, limiter ? 16384.0 : 2048.0);
	update();
}

void XYView::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), Qt::black);
	const int W = width(), H = height();
	const int xc = W / 2, yc = H / 2;
	p.setPen(QColor(60, 60, 60));
	p.drawLine(0, yc, W, yc);
	p.drawLine(xc, 0, xc, H);
	const size_t n = std::min(m_x.size(), m_y.size());
	if( n == 0 ) return;
	const double sx = (xc * 0.8) / m_dmax, sy = (yc * 0.8) / m_dmax;
	p.setPen(QColor(0, 255, 0));
	for( size_t i = 0; i < n; i++ ){
		const int x = xc + int(m_x[i] * sx);
		const int y = m_invert ? yc - int(m_y[i] * sy) : yc + int(m_y[i] * sy);
		p.drawPoint(x, y);
	}
}

// ===========================================================================
LevelBar::LevelBar(QWidget *parent) : QWidget(parent)
{
	setFixedHeight(14);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setToolTip(tr("Signal quality. Click or drag to set the squelch threshold."));
}

void LevelBar::setLevel(const Engine::Level &lv)
{
	m_lv = lv;
	update();
}

void LevelBar::setFromX(int x)
{
	const double lvl = std::clamp(double(x) / std::max(1, width() - 1), 0.0, 1.0) * kSqScale;
	emit squelchLevelChanged(lvl);
}

void LevelBar::mousePressEvent(QMouseEvent *e) { setFromX(int(e->position().x())); }
void LevelBar::mouseMoveEvent(QMouseEvent *e)
{
	if( e->buttons() & Qt::LeftButton ) setFromX(int(e->position().x()));
}

void LevelBar::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), Qt::black);
	const int W = width() - 1;
	int w = int(m_lv.avg * W / kSqScale);
	w = std::clamp(w, 0, W);
	QColor c = (!m_lv.sqOn || m_lv.open) ? QColor(0, 220, 0) : QColor(128, 128, 128);
	p.fillRect(0, 0, w, height(), c);
	const int sx = std::clamp(int(m_lv.sqLevel * W / kSqScale), 0, W);
	p.setPen(m_lv.sqOn ? Qt::yellow : QColor(90, 90, 90));
	p.drawLine(sx, 0, sx, height());
}

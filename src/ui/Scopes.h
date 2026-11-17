// Scope widgets: FFT spectrum, waterfall, XY (mark/space) scope and the signal level bar.
// These are the Qt replacements of the original PBoxFFTIN / PBoxWater / PBoxXY / PBoxSQ paint boxes.
#ifndef SCOPES_H
#define SCOPES_H

#include <QImage>
#include <QWidget>
#include <vector>

#include "Engine.h"

class SpectrumView : public QWidget
{
	Q_OBJECT
public:
	explicit SpectrumView(QWidget *parent = nullptr);
	void setFrame(const Engine::Spectrum &sp);
	void setOverlay(const QString &text);		// "Play", "Rec." ...
	QSize sizeHint() const override { return {512, 110}; }
	QSize minimumSizeHint() const override { return {160, 60}; }

signals:
	void clicked(int x, int width, Qt::MouseButton button);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	Engine::Spectrum m_sp;
	QString m_overlay;
	int m_fftMax = 128;
	int m_fftSumMax = 256;
};

class WaterfallView : public QWidget
{
	Q_OBJECT
public:
	explicit WaterfallView(QWidget *parent = nullptr);
	void addFrame(const Engine::Spectrum &sp);
	QSize sizeHint() const override { return {512, 110}; }
	QSize minimumSizeHint() const override { return {160, 40}; }

signals:
	void clicked(int x, int width, Qt::MouseButton button);

protected:
	void paintEvent(QPaintEvent *) override;
	void resizeEvent(QResizeEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	QImage m_img;
	Engine::Spectrum m_last;
};

class XYView : public QWidget
{
	Q_OBJECT
public:
	explicit XYView(QWidget *parent = nullptr);
	void setData(const std::vector<double> &mark, const std::vector<double> &space, bool limiter, bool invert);
	void clear();
	QSize sizeHint() const override { return {110, 110}; }
	QSize minimumSizeHint() const override { return {60, 60}; }

protected:
	void paintEvent(QPaintEvent *) override;

private:
	std::vector<double> m_x, m_y;
	double m_dmax = 2048.0;
	bool m_invert = false;
};

class LevelBar : public QWidget
{
	Q_OBJECT
public:
	explicit LevelBar(QWidget *parent = nullptr);
	void setLevel(const Engine::Level &lv);
	QSize sizeHint() const override { return {120, 14}; }
	QSize minimumSizeHint() const override { return {40, 10}; }

signals:
	void squelchLevelChanged(double level);		// user dragged the squelch marker (0..1024 scale)

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void setFromX(int x);
	Engine::Level m_lv;
};

#endif

/*
 * Copyright (C) 2024
 *
 * This file is part of LumosLiDARViewer.
 *
 * LumosLiDARViewer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * LumosLiDARViewer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with LumosLiDARViewer.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QtWidgets>
#include <QtGui>
#include <QtCore>
class CLumoMap : public QWidget
{
    Q_OBJECT

public:
    CLumoMap(QWidget *parent = nullptr)
        : QWidget(parent) {
        setMouseTracking(true);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setMinimumSize(800, 600);

        penThin = QPen(lineThin.color, lineThin.thickness, lineThin.pattern);
        penThick = QPen(lineThick.color, lineThick.thickness, lineThick.pattern);
        penGrid = QPen(lineThin.color, lineThin.thickness, lineThick.pattern);
    }
    ~CLumoMap() {}

    void lumos(const QVector<QPointF>& lidarPoints)
    {
        m_lidarPoints = lidarPoints;
        update();
    }
    void CLumoMap::setSettings(float pixelsPerMeter, int maxConcCircles)
    {
        m_pixelsPerMeter = pixelsPerMeter;
        m_maxConcCircles = maxConcCircles;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Clear the background
        painter.fillRect(rect(), Qt::black);

        painter.translate(m_centerPoint + m_centerOffset);
        painter.scale(m_zoomRate, m_zoomRate);

        drawCrosshair(painter);
        drawConcCircles(painter);
        drawLidarPoints(painter);

    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_lastMousePos = event->pos();
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            m_centerOffset += (event->pos() - m_lastMousePos);
            m_lastMousePos = event->pos();
            update();
        }
    }
    void wheelEvent(QWheelEvent *event) override
    {
        float numSteps = event->delta() / (8.0f * 20.0f);
        float scaleFactor = std::pow(1.125f, numSteps);

        m_zoomRate *= scaleFactor;

        penThin = QPen(lineThin.color, lineThin.thickness / m_zoomRate, lineThin.pattern);
        penThick = QPen(lineThick.color, lineThick.thickness / m_zoomRate, lineThick.pattern);
        penGrid = QPen(lineThin.color, lineThin.thickness / m_zoomRate, lineThick.pattern);
        update();
    }
    void resizeEvent(QResizeEvent* event) override
    {
        m_centerPoint = QPointF(width() / 2, height() / 2);
        update();
    }

private:
    void drawLidarPoints(QPainter &painter)
    {
        painter.setPen(QPen(Qt::green, m_PointSize / m_zoomRate));

        for (const QPointF &point : qAsConst(m_lidarPoints)) {
            painter.drawPoint(point);
        }
    }
    void drawCrosshair(QPainter &painter)
    {
        painter.setPen(penGrid);
        m_lidarPos = (m_centerPoint + m_centerOffset) / m_zoomRate;
        m_sceneSize = QPointF(width() / m_zoomRate, height() / m_zoomRate);

        painter.drawLine(-m_lidarPos.rx(), 0, m_sceneSize.rx() - m_lidarPos.rx(), 0);
        painter.drawLine(0, -m_lidarPos.ry(), 0, m_sceneSize.ry() - m_lidarPos.ry());
    }
    void drawConcCircles(QPainter &painter)
    {
        int numCircles = std::min(m_maxConcCircles, int(m_sceneSize.rx() / m_pixelsPerMeter));

        for (int i = 1; i <= numCircles; ++i) {
            float radius = i * m_pixelsPerMeter;
            int gridType = (i % m_concCircleStep);
            if (gridType == 0) {
                painter.setPen(penThick);
            } else if (gridType == 1) {
                painter.setPen(penThin);
            }
            painter.drawEllipse(QPointF(0, 0), radius, radius);
            if (radius > m_sceneSize.rx()) break;
        }
    }

    QPointF m_sceneSize;
    QPointF m_lidarPos;
    QVector<QPointF> m_lidarPoints;
    QPointF m_centerOffset;
    QPointF m_centerPoint;
    QPoint  m_lastMousePos;
    int     m_PointSize = 2;


    double  m_zoomRate = 1.000f;
    float   m_pixelsPerMeter = 100.0f;
    int     m_maxConcCircles = 100;
    int     m_concCircleStep = 5;

    struct lineInfo{
        double thickness;
        Qt::GlobalColor color;
        Qt::PenStyle pattern;
    };
    lineInfo lineThin = {0.5, Qt::gray, Qt::SolidLine};
    lineInfo lineThick = {1, Qt::darkRed, Qt::SolidLine};
    QPen penThin;
    QPen penThick;
    QPen penGrid;

};

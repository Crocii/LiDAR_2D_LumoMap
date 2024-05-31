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

#ifndef CCLOUDPOINTS_H
#define CCLOUDPOINTS_H


#include <QObject>
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <QtMath>

class CCloudPoints : public QObject {
    Q_OBJECT

public:
    CCloudPoints(QObject *parent = nullptr)
        : QObject(parent), m_resolution(0.3f), m_measureCnt(360 / m_resolution),
        m_maxPoints(m_measureCnt), m_pixelsPerMeter(100), m_scale(m_pixelsPerMeter / 1000)
    {
        // connect(&timer, &QTimer::timeout, this, &CCloudPoints::generateVirtualData);
        // timer.start(1000 / 10); // rps
    }

    QVector<QPointF> getPoints() const {
        return m_points;
    }

    QVector<QPointF> setPoints(QVector<QPointF> points) {
        m_points.append(points);
        int excess = m_points.size() - m_maxPoints;
        if (excess > 0) {
            for (int i = 0; i < excess; i++) {
                m_points.removeFirst();
            }
        }
    }

    void setPoint(float angle, float distance) {
        float radian = angle * M_PI / 180.0;

        distance *= m_scale;
        float x = distance * std::cos(radian);
        float y = distance * std::sin(radian);
        m_points.append(QPointF(x, y));

        int excess = m_points.size() - m_maxPoints;
        if (excess > 0) {
            for (int i = 0; i < excess; i++) {
                m_points.removeFirst();
            }
        }
    }

    int getPointCount() const {
        return m_points.size();
    }

    void clearPoints() {
        m_points.clear();
    }

public slots:
    void generateVirtualData() {
        static int shapeType = 0;
        if (m_points.size() >= m_maxPoints)
            m_points.clear();
        // 가상 데이터 생성
        float angle = 270;
        for (int i = 0; i < m_measureCnt; ++i) { // 0.3도 간격으로 360도 커버
            angle += 0.3;
            if (angle < 0)
                angle += 360;
            if (angle > 360)
                angle -= 360;

            float radian = angle * M_PI / 180.0;
            float distance = (shapeType) ? 3000 : 2500 + (i % 50); // 사각형 및 원 모양 교차
            distance *= m_scale;
            float x = distance * std::cos(radian);
            float y = distance * std::sin(radian);
            m_points.append(QPointF(x, y));
        }
        ++shapeType %= 2;
        emit newData();
    }

signals:
    void newData();

private:
    QVector<QPointF> m_points;
    QTimer timer;

    float m_resolution = 0.3;
    int m_measureCnt = 360 / m_resolution;
    const int m_maxPoints = m_measureCnt * 2;

    float m_pixelsPerMeter = 100;
    float m_scale = m_pixelsPerMeter / 1000;
};


#endif // CCLOUDPOINTS_H

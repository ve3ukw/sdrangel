///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Marcel, VE3UKW <marcel@ve3ukw.ca>                          //
// Some code by AI                                                               //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
// (at your option) any later version.                                           //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include <cmath>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWheelEvent>

#include "jogwheelknob.h"

static QPointF eventPos(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

JogWheelKnob::JogWheelKnob(QWidget *parent) :
    QWidget(parent),
    m_angle(0.0),
    m_dragAngle(0.0),
    m_residual(0.0),
    m_wheelResidual(0),
    m_dragging(false)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setToolTip(tr("Drag round to tune, or use the mouse wheel.\n"
        "With focus: Up/Down or Left/Right for one step, Page Up/Down for ten."));
}

// Angle of the pointer around the centre, in degrees clockwise from 12 o'clock
double JogWheelKnob::pointerAngle(const QPointF& pos) const
{
    QPointF d = pos - QPointF(width() / 2.0, height() / 2.0);
    return std::atan2(d.x(), -d.y()) * 180.0 / M_PI;
}

void JogWheelKnob::rotate(double degrees)
{
    const double detentAngle = 360.0 / m_detents;

    m_angle = std::fmod(m_angle + degrees, 360.0);
    m_residual += degrees;
    // Truncates towards zero, so the remainder keeps its sign. The tiny margin stops rounding errors in the
    // accumulated angle losing a detent on an exact multiple
    int detents = (int) ((m_residual + std::copysign(1e-6, m_residual)) / detentAngle);
    m_residual -= detents * detentAngle;

    if (detents != 0) {
        emit turned(detents);
    }

    update();
}

void JogWheelKnob::step(int detents)
{
    m_angle = std::fmod(m_angle + detents * 360.0 / m_detents, 360.0);
    emit turned(detents);
    update();
}

void JogWheelKnob::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_dragAngle = pointerAngle(eventPos(event));
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void JogWheelKnob::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        return;
    }

    // Close to the centre a small movement is a large angle, so ignore it there
    QPointF d = eventPos(event) - QPointF(width() / 2.0, height() / 2.0);
    double radius = std::min(width(), height()) / 2.0;

    if (std::hypot(d.x(), d.y()) < radius * 0.15) {
        return;
    }

    double angle = pointerAngle(eventPos(event));
    double delta = angle - m_dragAngle;

    if (delta > 180.0) {
        delta -= 360.0;
    } else if (delta < -180.0) {
        delta += 360.0;
    }

    m_dragAngle = angle;
    rotate(delta);
    event->accept();
}

void JogWheelKnob::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
    }
}

void JogWheelKnob::wheelEvent(QWheelEvent *event)
{
    // 120 is one notch of a standard mouse wheel. High resolution wheels send smaller deltas
    m_wheelResidual += event->angleDelta().y();
    int detents = m_wheelResidual / 120;
    m_wheelResidual -= detents * 120;

    if (detents != 0) {
        step(detents);
    }

    event->accept();
}

void JogWheelKnob::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_Up:
    case Qt::Key_Right:
        step(1);
        break;
    case Qt::Key_Down:
    case Qt::Key_Left:
        step(-1);
        break;
    case Qt::Key_PageUp:
        step(10);
        break;
    case Qt::Key_PageDown:
        step(-10);
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    event->accept();
}

void JogWheelKnob::paintEvent(QPaintEvent *event)
{
    (void) event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const double side = std::min(width(), height());
    const QPointF centre(width() / 2.0, height() / 2.0);
    const double outer = side / 2.0 - 2.0;
    const double knob = outer * 0.84;
    const QColor accent = palette().color(QPalette::Highlight);
    const QColor tickColor = palette().color(QPalette::WindowText);

    painter.translate(centre);

    // Scale of detents round the knob, fixed like the markings on a rig's panel
    for (int i = 0; i < m_detents; i++)
    {
        bool major = (i % 5) == 0;
        QColor c = tickColor;
        c.setAlphaF(major ? 0.7 : 0.35);
        painter.setPen(QPen(c, major ? 2.0 : 1.0, Qt::SolidLine, Qt::RoundCap));
        double a = i * 2.0 * M_PI / m_detents;
        double r0 = major ? outer * 0.89 : outer * 0.92;
        painter.drawLine(QPointF(r0 * std::sin(a), -r0 * std::cos(a)), QPointF(outer * std::sin(a), -outer * std::cos(a)));
    }

    // Body, lit from the top left
    QRadialGradient body(QPointF(-knob * 0.35, -knob * 0.35), knob * 1.6);
    body.setColorAt(0.0, QColor(110, 110, 115));
    body.setColorAt(0.5, QColor(55, 55, 60));
    body.setColorAt(1.0, QColor(25, 25, 28));
    painter.setPen(QPen(hasFocus() ? accent : QColor(15, 15, 15), hasFocus() ? 2.0 : 1.5));
    painter.setBrush(body);
    painter.drawEllipse(QPointF(0, 0), knob, knob);

    // Knurled skirt: fine ridges turning with the knob
    painter.save();
    painter.rotate(m_angle);
    const int ridges = 90;

    for (int i = 0; i < ridges; i++)
    {
        double a = i * 2.0 * M_PI / ridges;
        painter.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
        painter.drawLine(QPointF(knob * 0.88 * std::sin(a), -knob * 0.88 * std::cos(a)),
            QPointF(knob * 0.99 * std::sin(a), -knob * 0.99 * std::cos(a)));
    }

    // Face
    QRadialGradient face(QPointF(-knob * 0.25, -knob * 0.3), knob);
    face.setColorAt(0.0, QColor(85, 85, 90));
    face.setColorAt(1.0, QColor(35, 35, 40));
    painter.setPen(QPen(QColor(20, 20, 20), 1.0));
    painter.setBrush(face);
    painter.drawEllipse(QPointF(0, 0), knob * 0.86, knob * 0.86);

    // Finger dimple, the part you can see turning
    const double dimpleR = knob * 0.17;
    const QPointF dimple(0, -knob * 0.58);
    QRadialGradient hollow(dimple + QPointF(dimpleR * 0.3, dimpleR * 0.3), dimpleR * 1.2);
    hollow.setColorAt(0.0, QColor(70, 70, 75));
    hollow.setColorAt(1.0, QColor(18, 18, 20));
    painter.setPen(QPen(QColor(120, 120, 125, 120), 1.0));
    painter.setBrush(hollow);
    painter.drawEllipse(dimple, dimpleR, dimpleR);

    // Index mark at the rim
    painter.setPen(QPen(accent, 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(0, -knob * 0.97), QPointF(0, -knob * 0.88));
    painter.restore();
}

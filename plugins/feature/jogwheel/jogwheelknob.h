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

#ifndef INCLUDE_FEATURE_JOGWHEELKNOB_H_
#define INCLUDE_FEATURE_JOGWHEELKNOB_H_

#include <QWidget>

// Tuning knob, drawn like the main dial of an HF rig, with a finger dimple.
// Turn it by dragging round its centre, with the mouse wheel, or with the arrow and page keys when it has focus.
// Each detent emits turned(): only whole detents, so the caller deals in steps rather than angles.
class JogWheelKnob : public QWidget
{
    Q_OBJECT
public:
    explicit JogWheelKnob(QWidget *parent = nullptr);

    QSize sizeHint() const override { return QSize(180, 180); }
    QSize minimumSizeHint() const override { return QSize(90, 90); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return width; }

    static const int m_detents = 40; //!< Detents per revolution

signals:
    void turned(int detents);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    double m_angle;         //!< Displayed rotation in degrees, clockwise
    double m_dragAngle;     //!< Pointer angle at the last mouse event while dragging
    double m_residual;      //!< Rotation in degrees not yet emitted as a detent
    int m_wheelResidual;    //!< Mouse wheel angle delta not yet emitted as a detent
    bool m_dragging;

    double pointerAngle(const QPointF& pos) const;
    void rotate(double degrees);
    void step(int detents);
};

#endif // INCLUDE_FEATURE_JOGWHEELKNOB_H_

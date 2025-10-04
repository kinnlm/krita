/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_BRUSH_CHANNEL_MATRIX_WIDGET_H
#define KIS_BRUSH_CHANNEL_MATRIX_WIDGET_H

#include <QWidget>
#include <QScopedPointer>

#include "kritaui_export.h"
#include "kis_brush_channel_matrix.h"

class QCheckBox;
class QPushButton;
class KisDoubleSliderSpinBox;

/**
 * Minimal UI widget that exposes the brush channel matrix.
 *
 * The final design will likely integrate with the brush editor, but for the
 * MVP we provide a compact stand-alone widget that can be embedded in a dock.
 */
class KRITAUI_EXPORT KisBrushChannelMatrixWidget : public QWidget
{
    Q_OBJECT
public:
    explicit KisBrushChannelMatrixWidget(QWidget *parent = nullptr);
    ~KisBrushChannelMatrixWidget() override;

    void setMatrix(const KisBrushChannelMatrix &matrix);
    KisBrushChannelMatrix matrix() const;

signals:
    void matrixChanged(const KisBrushChannelMatrix &matrix);

private:
    struct UiElements;
    const QScopedPointer<UiElements> m_ui;

    void buildUi();
    void connectSignals();
    void updateFromMatrix();

    KisBrushChannelMatrix *m_data;
};

#endif // KIS_BRUSH_CHANNEL_MATRIX_WIDGET_H


/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_PAINTERLY_PBR_ROUTER_H
#define KIS_PAINTERLY_PBR_ROUTER_H

#include "kritaimage_export.h"

#include <QObject>
#include <memory>
#include <QList>

class KisRenderedDab;

class KisPainter;
class KisMaterialGroupLayer;
class KisPaintDevice;
class KisBrushChannelMatrix;

/**
 * @brief Routes brush dabs to multiple material channels.
 *
 * The actual painting logic will map a single stroke to multiple channel
 * layers inside a KisMaterialGroupLayer. This MVP exposes a simplified API so
 * that paintops can opt into the behaviour without needing to understand the
 * details of layer management.
 */
class KRITAIMAGE_EXPORT KisPainterlyPbrRouter : public QObject
{
    Q_OBJECT
public:
    explicit KisPainterlyPbrRouter(QObject *parent = nullptr);
    ~KisPainterlyPbrRouter() override;

    void setTargetGroup(KisMaterialGroupLayer *group);
    KisMaterialGroupLayer *targetGroup() const;

    void setChannelMatrix(const KisBrushChannelMatrix &matrix);
    KisBrushChannelMatrix channelMatrix() const;

    void beginStroke();
    void endStroke();

    /// Invoked by the painter after the base color dab has been blitted.
    void applyDabs(const QRect &applyRect, const QList<KisRenderedDab> &dabs, KisPainter *baseColorPainter);

private:
    struct Private;
    const std::unique_ptr<Private> d;
};

#endif // KIS_PAINTERLY_PBR_ROUTER_H


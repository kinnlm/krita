/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_painterly_pbr_router.h"

#include "kis_brush_channel_matrix.h"
#include "kis_material_group_layer.h"
#include "kis_painter.h"

#include "kis_paint_layer.h"
#include "kis_paint_device.h"
#include "kis_hline_iterator.h"
#include "KisRenderedDab.h"

#include <KoColorSpace.h>
#include <KoColor.h>

#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QtMath>
#include <cmath>

namespace {
inline float clamp01(float value)
{
    return qBound(0.0f, value, 1.0f);
}

inline QVector3D ensureNormalized(const QVector3D &value)
{
    if (value.isNull()) {
        return QVector3D(0.0f, 0.0f, 1.0f);
    }
    QVector3D result = value;
    result.normalize();
    return result;
}

inline QVector3D decodeNormal(const KoColorSpace *colorSpace, const quint8 *data, QVector<float> &buffer)
{
    colorSpace->normalisedChannelsValue(data, buffer);
    QVector3D vector(buffer.value(0) * 2.0f - 1.0f,
                     buffer.value(1) * 2.0f - 1.0f,
                     buffer.value(2) * 2.0f - 1.0f);
    return ensureNormalized(vector);
}

inline void encodeNormal(const KoColorSpace *colorSpace, QVector3D vector, quint8 *data, QVector<float> &buffer)
{
    vector = ensureNormalized(vector);
    if (buffer.size() < colorSpace->channelCount()) {
        buffer.resize(colorSpace->channelCount());
    }
    buffer[0] = clamp01(0.5f * (vector.x() + 1.0f));
    buffer[1] = clamp01(0.5f * (vector.y() + 1.0f));
    buffer[2] = clamp01(0.5f * (vector.z() + 1.0f));
    if (buffer.size() > 3) {
        buffer[3] = 1.0f;
    }
    colorSpace->fromNormalisedChannelsValue(data, buffer);
}

inline float sampleHeightValue(KisPaintDeviceSP device,
                               const KoColorSpace *cs,
                               KoColor &scratchColor,
                               QVector<float> &channelBuffer,
                               int x,
                               int y)
{
    const QRect extent = device->extent();
    if (!extent.contains(x, y)) {
        return 0.0f;
    }

    device->pixel(QPoint(x, y), &scratchColor);
    cs->normalisedChannelsValue(scratchColor.data(), channelBuffer);
    return channelBuffer.value(0);
}

inline QVector3D rnmBlend(const QVector3D &base, const QVector3D &detail)
{
    QVector3D normalizedBase = ensureNormalized(base);
    QVector3D normalizedDetail = ensureNormalized(detail);

    const QVector2D baseXY(normalizedBase.x(), normalizedBase.y());
    const QVector2D detailXY(normalizedDetail.x(), normalizedDetail.y());

    QVector3D blended;
    blended.setX(baseXY.x() * normalizedDetail.z() + detailXY.x() * normalizedBase.z());
    blended.setY(baseXY.y() * normalizedDetail.z() + detailXY.y() * normalizedBase.z());
    blended.setZ(normalizedBase.z() * normalizedDetail.z() - QVector2D::dotProduct(baseXY, detailXY));

    return ensureNormalized(blended);
}
}

struct KisPainterlyPbrRouter::Private
{
    KisMaterialGroupLayer *group = nullptr;
    KisBrushChannelMatrix matrix;
    bool strokeActive = false;
};

KisPainterlyPbrRouter::KisPainterlyPbrRouter(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
}

KisPainterlyPbrRouter::~KisPainterlyPbrRouter() = default;

void KisPainterlyPbrRouter::setTargetGroup(KisMaterialGroupLayer *group)
{
    d->group = group;
}

KisMaterialGroupLayer *KisPainterlyPbrRouter::targetGroup() const
{
    return d->group;
}

void KisPainterlyPbrRouter::setChannelMatrix(const KisBrushChannelMatrix &matrix)
{
    d->matrix = matrix;
}

KisBrushChannelMatrix KisPainterlyPbrRouter::channelMatrix() const
{
    return d->matrix;
}

void KisPainterlyPbrRouter::beginStroke()
{
    d->strokeActive = true;
    if (d->group) {
        d->group->ensureChannelChildren();
    }
}

void KisPainterlyPbrRouter::endStroke()
{
    d->strokeActive = false;
}

void KisPainterlyPbrRouter::applyDabs(const QRect &applyRect, const QList<KisRenderedDab> &dabs, KisPainter *baseColorPainter)
{
    Q_UNUSED(applyRect);
    Q_UNUSED(baseColorPainter);

    if (!d->group || dabs.isEmpty()) {
        return;
    }

    if (!d->strokeActive) {
        d->strokeActive = true;
    }

    d->group->ensureChannelChildren();

    auto toPaintLayer = [](const KisLayerSP &node) -> KisPaintLayer * {
        return qobject_cast<KisPaintLayer *>(node.data());
    };

    KisPaintLayer *heightLayer = toPaintLayer(d->group->channelLayer(KisMaterialGroupLayer::HeightChannel));
    KisPaintLayer *normalLayer = toPaintLayer(d->group->channelLayer(KisMaterialGroupLayer::NormalChannel));
    KisPaintLayer *roughnessLayer = toPaintLayer(d->group->channelLayer(KisMaterialGroupLayer::RoughnessChannel));
    KisPaintLayer *metallicLayer = toPaintLayer(d->group->channelLayer(KisMaterialGroupLayer::MetallicChannel));

    for (const KisRenderedDab &dab : dabs) {
        const KoColorSpace *srcColorSpace = dab.device->colorSpace();
        if (!srcColorSpace) {
            continue;
        }

        const QRect dabBounds = dab.device->bounds();
        const int dabWidth = dabBounds.width();
        const int dabHeight = dabBounds.height();
        if (dabWidth <= 0 || dabHeight <= 0) {
            continue;
        }

        const int srcPixelSize = srcColorSpace->pixelSize();
        const int dabRowStride = srcPixelSize * dabWidth;
        const quint8 *dabData = dab.device->constData();
        const QPoint targetTopLeft = dab.realBounds().topLeft();

        // The dab's opacity already encodes input modulation (e.g. pressure)
        // from the brush engine, so we reuse it as the pressure proxy for
        // channel-specific computations.
        const float dabPressure = clamp01(float(dab.opacity));

        if (d->matrix.affectHeight() && heightLayer) {
            KisPaintDeviceSP heightDevice = heightLayer->paintDevice();
            const KoColorSpace *heightCs = heightDevice->colorSpace();
            QVector<float> heightChannels(heightCs->channelCount());

            const float baseHeight = d->matrix.heightScaleMM() * std::pow(dabPressure, d->matrix.heightCreaminess());

            for (int y = 0; y < dabHeight; ++y) {
                KisHLineIteratorSP it = heightDevice->createHLineIteratorNG(targetTopLeft.x(), targetTopLeft.y() + y, dabWidth);
                const quint8 *srcPixel = dabData + y * dabRowStride;
                for (int x = 0; x < dabWidth; ++x) {
                    const float alphaMask = srcColorSpace->opacityF(srcPixel);
                    const float weight = clamp01(d->matrix.opacityHeight() * alphaMask);
                    if (weight > 0.0f) {
                        heightCs->normalisedChannelsValue(it->rawData(), heightChannels);
                        const float existing = heightChannels.value(0);
                        const float result = existing + (baseHeight - existing) * weight;
                        heightChannels[0] = result;
                        if (heightChannels.size() > 1) {
                            heightChannels[1] = clamp01(qMax(heightChannels[1], weight));
                        }
                        heightCs->fromNormalisedChannelsValue(it->rawData(), heightChannels);
                    }
                    srcPixel += srcPixelSize;
                    ++(*it);
                }
            }
        }

        auto applyScalarChannel = [&](KisPaintLayer *layer, float targetValue) {
            if (!layer) {
                return;
            }
            KisPaintDeviceSP device = layer->paintDevice();
            const KoColorSpace *dstCs = device->colorSpace();
            QVector<float> dstChannels(dstCs->channelCount());

            for (int y = 0; y < dabHeight; ++y) {
                KisHLineIteratorSP it = device->createHLineIteratorNG(targetTopLeft.x(), targetTopLeft.y() + y, dabWidth);
                const quint8 *srcPixel = dabData + y * dabRowStride;
                for (int x = 0; x < dabWidth; ++x) {
                    const float alphaMask = srcColorSpace->opacityF(srcPixel);
                    if (alphaMask > 0.0f) {
                        dstCs->normalisedChannelsValue(it->rawData(), dstChannels);
                        const float existing = dstChannels.value(0);
                        const float result = existing + (targetValue - existing) * alphaMask;
                        dstChannels[0] = result;
                        if (dstChannels.size() > 1) {
                            dstChannels[1] = clamp01(qMax(dstChannels[1], alphaMask));
                        }
                        dstCs->fromNormalisedChannelsValue(it->rawData(), dstChannels);
                    }
                    srcPixel += srcPixelSize;
                    ++(*it);
                }
            }
        };

        if (d->matrix.affectRoughness()) {
            applyScalarChannel(roughnessLayer, clamp01(d->matrix.roughnessValue()));
        }

        if (d->matrix.affectMetallic()) {
            applyScalarChannel(metallicLayer, clamp01(d->matrix.metallicValue()));
        }

        if (d->matrix.affectNormal() && heightLayer && normalLayer) {
            KisPaintDeviceSP heightDevice = heightLayer->paintDevice();
            KisPaintDeviceSP normalDevice = normalLayer->paintDevice();

            const KoColorSpace *heightCs = heightDevice->colorSpace();
            const KoColorSpace *normalCs = normalDevice->colorSpace();

            KoColor heightColor(heightCs);
            KoColor normalColor(normalCs);

            QVector<float> heightChannelBuffer(heightCs->channelCount());
            QVector<float> normalChannelBuffer(normalCs->channelCount());

            for (int y = 0; y < dabHeight; ++y) {
                const quint8 *srcPixel = dabData + y * dabRowStride;
                for (int x = 0; x < dabWidth; ++x) {
                    const float alphaMask = srcColorSpace->opacityF(srcPixel);
                    const float weight = clamp01(d->matrix.normalStrength() * alphaMask);
                    if (weight > 0.0f) {
                        const int pixelX = targetTopLeft.x() + x;
                        const int pixelY = targetTopLeft.y() + y;

                        const float hLeft = sampleHeightValue(heightDevice, heightCs, heightColor, heightChannelBuffer, pixelX - 1, pixelY);
                        const float hRight = sampleHeightValue(heightDevice, heightCs, heightColor, heightChannelBuffer, pixelX + 1, pixelY);
                        const float hUp = sampleHeightValue(heightDevice, heightCs, heightColor, heightChannelBuffer, pixelX, pixelY - 1);
                        const float hDown = sampleHeightValue(heightDevice, heightCs, heightColor, heightChannelBuffer, pixelX, pixelY + 1);

                        const float gradientScale = qMax(0.0f, d->matrix.normalStrength());
                        const float dx = (hRight - hLeft) * 0.5f * gradientScale;
                        const float dy = (hDown - hUp) * 0.5f * gradientScale;

                        QVector3D detail(-dx, -dy, 1.0f);
                        detail = ensureNormalized(detail);

                        normalDevice->pixel(QPoint(pixelX, pixelY), &normalColor);
                        QVector3D baseNormal = decodeNormal(normalCs, normalColor.data(), normalChannelBuffer);

                        QVector3D combined = rnmBlend(baseNormal, detail);
                        QVector3D finalNormal = ensureNormalized(baseNormal * (1.0f - weight) + combined * weight);

                        encodeNormal(normalCs, finalNormal, normalColor.data(), normalChannelBuffer);
                        normalDevice->setPixel(pixelX, pixelY, normalColor);
                    }
                    srcPixel += srcPixelSize;
                }
            }
        }
    }
}


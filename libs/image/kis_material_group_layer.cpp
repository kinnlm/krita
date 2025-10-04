/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_material_group_layer.h"

#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorSpaceStandardIds.h>
#include <klocalizedstring.h>

#include <QSet>

#include "kis_image.h"
#include "kis_layer.h"
#include "kis_paint_layer.h"

namespace {
struct ChannelNames
{
    QString id;
    QString displayName;
};

const ChannelNames &channelNamesForIndex(KisMaterialGroupLayer::ChannelIndex index)
{
    static const ChannelNames names[] = {
        {QStringLiteral("BaseColor"), QStringLiteral("BaseColor")},
        {QStringLiteral("Height"), QStringLiteral("Height")},
        {QStringLiteral("Normal"), QStringLiteral("Normal")},
        {QStringLiteral("Roughness"), QStringLiteral("Roughness")},
        {QStringLiteral("Metallic"), QStringLiteral("Metallic")}
    };
    return names[index];
}

const KoColorSpace *colorSpaceForChannel(KisMaterialGroupLayer::ChannelIndex index)
{
    KoColorSpaceRegistry *registry = KoColorSpaceRegistry::instance();
    switch (index) {
    case KisMaterialGroupLayer::BaseColorChannel:
        return registry->rgb8();
    case KisMaterialGroupLayer::NormalChannel:
        return registry->colorSpace(RGBAColorModelID.id(), Float16BitsColorDepthID.id(), QString());
    case KisMaterialGroupLayer::HeightChannel:
    case KisMaterialGroupLayer::RoughnessChannel:
    case KisMaterialGroupLayer::MetallicChannel:
        return registry->colorSpace(GrayAColorModelID.id(), Float16BitsColorDepthID.id(), QString());
    default:
        break;
    }
    return nullptr;
}
}

KisMaterialGroupLayer::KisMaterialGroupLayer(KisImageWSP image,
                                             const QString &name,
                                             quint8 opacity,
                                             const KoColorSpace *colorSpace)
    : KisGroupLayer(image, name, opacity, colorSpace)
{
    setProperty(materialGroupPropertyKey().toUtf8().constData(), true);
    setNodeProperty(materialGroupPropertyKey(), true);
}

KisMaterialGroupLayer::KisMaterialGroupLayer(const KisMaterialGroupLayer &rhs)
    : KisGroupLayer(rhs)
{
    setProperty(materialGroupPropertyKey().toUtf8().constData(), true);
    setNodeProperty(materialGroupPropertyKey(), true);
}

KisMaterialGroupLayer::~KisMaterialGroupLayer() = default;

QString KisMaterialGroupLayer::channelNodeId(ChannelIndex index)
{
    return channelNamesForIndex(index).id;
}

QString KisMaterialGroupLayer::channelDisplayName(ChannelIndex index)
{
    return channelNamesForIndex(index).displayName;
}

QString KisMaterialGroupLayer::channelPropertyKey()
{
    return QStringLiteral("materialChannel");
}

QString KisMaterialGroupLayer::materialGroupPropertyKey()
{
    return QStringLiteral("materialGroup");
}

bool KisMaterialGroupLayer::channelIndexFromId(const QString &id, ChannelIndex *indexOut)
{
    for (int i = 0; i < ChannelCount; ++i) {
        const ChannelNames &names = channelNamesForIndex(static_cast<ChannelIndex>(i));
        if (names.id.compare(id, Qt::CaseInsensitive) == 0) {
            if (indexOut) {
                *indexOut = static_cast<ChannelIndex>(i);
            }
            return true;
        }
    }
    return false;
}

void KisMaterialGroupLayer::ensureChannelChildren()
{
    normalizeChannelMetadata();

    for (int i = 0; i < ChannelCount; ++i) {
        const ChannelIndex index = static_cast<ChannelIndex>(i);
        KisLayerSP existing = channelLayer(index);
        if (!existing) {
            KisPaintLayer *templ = createChannelLayerTemplate(index);
            if (!templ) {
                continue;
            }

            KisNodeSP above = insertionAboveNode(index);
            if (image()) {
                image()->addNode(KisNodeSP(templ), KisNodeSP(this), above);
            }
        }
    }
    normalizeChannelMetadata();
}

void KisMaterialGroupLayer::normalizeChannelMetadata()
{
    QSet<ChannelIndex> claimed;

    // First pass: honour existing metadata if it is valid
    for (KisNodeSP child = firstChild(); child; child = child->nextSibling()) {
        if (KisLayerSP layer = qobject_cast<KisLayer *>(child.data())) {
            ChannelIndex index;
            if (channelIndexFromId(layer->nodeProperties().stringProperty(channelPropertyKey()), &index)) {
                applyChannelMetadata(layer.data(), index);
                claimed.insert(index);
            }
        }
    }

    // Second pass: try to map by name when the metadata is missing
    for (KisNodeSP child = firstChild(); child; child = child->nextSibling()) {
        if (KisLayerSP layer = qobject_cast<KisLayer *>(child.data())) {
            ChannelIndex index;
            if (channelIndexFromId(layer->nodeProperties().stringProperty(channelPropertyKey()), &index)) {
                continue;
            }

            const QString layerName = layer->name();
            for (int i = 0; i < ChannelCount; ++i) {
                ChannelIndex candidate = static_cast<ChannelIndex>(i);
                if (claimed.contains(candidate)) {
                    continue;
                }

                if (layerName.compare(channelDisplayName(candidate), Qt::CaseInsensitive) == 0) {
                    applyChannelMetadata(layer.data(), candidate);
                    claimed.insert(candidate);
                    break;
                }
            }
        }
    }
}

KisLayerSP KisMaterialGroupLayer::channelLayer(ChannelIndex index) const
{
    KisLayerSP fallback;
    for (KisNodeSP child = firstChild(); child; child = child->nextSibling()) {
        if (KisLayerSP layer = qobject_cast<KisLayer *>(child.data())) {
            const QString storedId = layer->nodeProperties().stringProperty(channelPropertyKey());
            ChannelIndex found;
            if (channelIndexFromId(storedId, &found) && found == index) {
                return layer;
            }
            if (!fallback && storedId.isEmpty() && layer->name().compare(channelDisplayName(index), Qt::CaseInsensitive) == 0) {
                fallback = layer;
            }
        }
    }
    return fallback;
}

KisNodeSP KisMaterialGroupLayer::insertionAboveNode(ChannelIndex index) const
{
    for (KisNodeSP child = firstChild(); child; child = child->nextSibling()) {
        if (KisLayer *layer = qobject_cast<KisLayer *>(child.data())) {
            ChannelIndex found;
            if (channelIndexFromId(layer->nodeProperties().stringProperty(channelPropertyKey()), &found)) {
                if (found > index) {
                    return child;
                }
            }
        }
    }
    return KisNodeSP();
}

bool KisMaterialGroupLayer::isValidMaterialStack() const
{
    return missingChannels().isEmpty() && validationIssues().isEmpty();
}

QVector<KisMaterialGroupLayer::ChannelIndex> KisMaterialGroupLayer::missingChannels() const
{
    QVector<ChannelIndex> result;
    for (int i = 0; i < ChannelCount; ++i) {
        ChannelIndex index = static_cast<ChannelIndex>(i);
        if (!channelLayer(index)) {
            result.append(index);
        }
    }
    return result;
}

QStringList KisMaterialGroupLayer::validationIssues() const
{
    QStringList issues;
    const QVector<ChannelIndex> missing = missingChannels();
    for (ChannelIndex index : missing) {
        issues << i18nc("Validation warning", "%1 channel is missing.", channelDisplayName(index));
    }

    QSet<QString> seenIds;

    for (KisNodeSP child = firstChild(); child; child = child->nextSibling()) {
        KisLayer *layer = qobject_cast<KisLayer *>(child.data());
        if (!layer) {
            continue;
        }

        const QString channelId = layer->nodeProperties().stringProperty(channelPropertyKey());
        ChannelIndex index;
        if (!channelIndexFromId(channelId, &index)) {
            issues << i18nc("Validation warning", "%1 is not assigned to a material channel.", layer->name());
            continue;
        }

        if (seenIds.contains(channelId)) {
            issues << i18nc("Validation warning", "Duplicate channel %1 detected.", channelId);
        }
        seenIds.insert(channelId);

        const KoColorSpace *expected = colorSpaceForChannel(index);
        if (expected && layer->colorSpace() && layer->colorSpace()->id() != expected->id()) {
            issues << i18nc("Validation warning", "%1 channel should use color space %2 but is %3.",
                            channelId,
                            expected->name(),
                            layer->colorSpace()->name());
        }
    }

    return issues;
}

void KisMaterialGroupLayer::tagChannelLayer(KisLayerSP layer, ChannelIndex index) const
{
    applyChannelMetadata(layer.data(), index);
}

KisPaintLayer *KisMaterialGroupLayer::createChannelLayerTemplate(ChannelIndex index) const
{
    const KoColorSpace *expected = colorSpaceForChannel(index);
    const KoColorSpace *space = expected ? expected : (image() ? image()->colorSpace() : nullptr);
    if (!space) {
        return nullptr;
    }

    KisPaintLayer *layer = new KisPaintLayer(image(), channelDisplayName(index), OPACITY_OPAQUE_U8, space);
    applyChannelMetadata(layer, index);
    return layer;
}

void KisMaterialGroupLayer::applyChannelMetadata(KisLayer *layer, ChannelIndex index)
{
    if (!layer) {
        return;
    }

    const QString id = channelNodeId(index);
    layer->setName(channelDisplayName(index));
    layer->setProperty(channelPropertyKey().toUtf8().constData(), id);
    layer->setNodeProperty(channelPropertyKey(), id);
}

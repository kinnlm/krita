/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_MATERIAL_GROUP_LAYER_H
#define KIS_MATERIAL_GROUP_LAYER_H

#include <QVector>
#include <QStringList>

#include "kritaimage_export.h"
#include "kis_group_layer.h"
#include "kis_types.h"

class KisLayer;
class KisPaintLayer;

/**
 * @brief Material group layer hosting a set of PBR channel layers.
 *
 * The painterly PBR light MVP treats a material as a fixed bundle of five
 * children layers representing BaseColor, Height, Normal, Roughness and
 * Metallic channels. This class owns the bookkeeping required to keep those
 * children alive and exposes helpers to query them.
 *
 * The actual compositing and brush routing logic is implemented elsewhere –
 * at this level we only keep track of metadata and persistence.
 */
class KRITAIMAGE_EXPORT KisMaterialGroupLayer : public KisGroupLayer
{
    Q_OBJECT
public:
    enum ChannelIndex {
        BaseColorChannel = 0,
        HeightChannel,
        NormalChannel,
        RoughnessChannel,
        MetallicChannel,
        ChannelCount
    };

    explicit KisMaterialGroupLayer(KisImageWSP image,
                                   const QString &name,
                                   quint8 opacity = OPACITY_OPAQUE_U8,
                                   const KoColorSpace *colorSpace = nullptr);
    KisMaterialGroupLayer(const KisMaterialGroupLayer &rhs);
    ~KisMaterialGroupLayer() override;

    KisNodeSP clone() const override { return KisNodeSP(new KisMaterialGroupLayer(*this)); }

    /// Convenience helper that returns the expected storage key for @p index.
    static QString channelNodeId(ChannelIndex index);

    /// Human friendly channel label.
    static QString channelDisplayName(ChannelIndex index);

    /// Storage key used for the node property that stores the channel name.
    static QString channelPropertyKey();

    /// Storage key that marks the node as a material group.
    static QString materialGroupPropertyKey();

    /// Look up the channel index for a stored identifier.
    static bool channelIndexFromId(const QString &id, ChannelIndex *indexOut);

    /// Ensure the children that represent the channels exist. This is cheap
    /// when the layers already exist.
    void ensureChannelChildren();

    /// Return the channel layer if available.
    KisLayerSP channelLayer(ChannelIndex index) const;

    /// Returns the node that a new channel should be inserted above to keep
    /// the canonical channel ordering. A null pointer means append at the end.
    KisNodeSP insertionAboveNode(ChannelIndex index) const;

    /// Whether the group has all channel metadata in sync.
    bool isValidMaterialStack() const;

    /// Which channels are missing.
    QVector<ChannelIndex> missingChannels() const;

    /// Returns validation messages for the material stack. Empty means valid.
    QStringList validationIssues() const;

    /// Synchronise channel names and metadata on the existing children.
    void normalizeChannelMetadata();

    /// Prepare an existing layer so that it behaves as @p index channel.
    void tagChannelLayer(KisLayerSP layer, ChannelIndex index) const;

    /// Creates a new paint layer configured for @p index channel.
    KisPaintLayer *createChannelLayerTemplate(ChannelIndex index) const;

private:
    static void applyChannelMetadata(KisLayer *layer, ChannelIndex index);
};

typedef KisSharedPtr<KisMaterialGroupLayer> KisMaterialGroupLayerSP;

#endif // KIS_MATERIAL_GROUP_LAYER_H


/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_BRUSH_CHANNEL_MATRIX_H
#define KIS_BRUSH_CHANNEL_MATRIX_H

#include "kritaimage_export.h"

#include <QMetaType>
#include <QJsonObject>

class QDomDocument;
class QDomElement;

/**
 * Lightweight value type describing how a brush should affect painterly PBR
 * material channels.
 */
class KRITAIMAGE_EXPORT KisBrushChannelMatrix
{
public:
    KisBrushChannelMatrix();

    bool affectBaseColor() const;
    void setAffectBaseColor(bool value);

    bool affectHeight() const;
    void setAffectHeight(bool value);

    bool affectNormal() const;
    void setAffectNormal(bool value);

    bool affectRoughness() const;
    void setAffectRoughness(bool value);

    bool affectMetallic() const;
    void setAffectMetallic(bool value);

    float opacityBaseColor() const;
    void setOpacityBaseColor(float value);

    float opacityHeight() const;
    void setOpacityHeight(float value);

    float normalStrength() const;
    void setNormalStrength(float value);

    float roughnessValue() const;
    void setRoughnessValue(float value);

    float metallicValue() const;
    void setMetallicValue(float value);

    float heightScaleMM() const;
    void setHeightScaleMM(float value);

    float heightCreaminess() const;
    void setHeightCreaminess(float value);

    void applyColorOnlyPreset();
    void applyTextureOnlyPreset();

    bool operator==(const KisBrushChannelMatrix &other) const;
    bool operator!=(const KisBrushChannelMatrix &other) const;

    QDomElement toXmlElement(QDomDocument &doc) const;
    static KisBrushChannelMatrix fromXmlElement(const QDomElement &element);

    QJsonObject toJson() const;
    static KisBrushChannelMatrix fromJson(const QJsonObject &object);

private:
    bool m_affectBaseColor = true;
    bool m_affectHeight = true;
    bool m_affectNormal = true;
    bool m_affectRoughness = false;
    bool m_affectMetallic = false;

    float m_opacityBaseColor = 1.0f;
    float m_opacityHeight = 1.0f;
    float m_normalStrength = 0.7f;
    float m_roughnessValue = 0.65f;
    float m_metallicValue = 0.0f;
    float m_heightScaleMM = 0.4f;
    float m_heightCreaminess = 1.6f;
};

Q_DECLARE_METATYPE(KisBrushChannelMatrix)

#endif // KIS_BRUSH_CHANNEL_MATRIX_H


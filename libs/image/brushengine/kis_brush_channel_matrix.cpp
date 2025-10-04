/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_brush_channel_matrix.h"

#include <QDomDocument>
#include <QDomElement>
#include <QJsonObject>
#include <QJsonValue>
#include <QtMath>

namespace {
inline float clamp01(float value)
{
    return qBound(0.0f, value, 1.0f);
}

inline bool fuzzyEqual(float lhs, float rhs)
{
    return qAbs(lhs - rhs) <= 0.0001f;
}
}

KisBrushChannelMatrix::KisBrushChannelMatrix() = default;

bool KisBrushChannelMatrix::affectBaseColor() const
{
    return m_affectBaseColor;
}

void KisBrushChannelMatrix::setAffectBaseColor(bool value)
{
    m_affectBaseColor = value;
}

bool KisBrushChannelMatrix::affectHeight() const
{
    return m_affectHeight;
}

void KisBrushChannelMatrix::setAffectHeight(bool value)
{
    m_affectHeight = value;
}

bool KisBrushChannelMatrix::affectNormal() const
{
    return m_affectNormal;
}

void KisBrushChannelMatrix::setAffectNormal(bool value)
{
    m_affectNormal = value;
}

bool KisBrushChannelMatrix::affectRoughness() const
{
    return m_affectRoughness;
}

void KisBrushChannelMatrix::setAffectRoughness(bool value)
{
    m_affectRoughness = value;
}

bool KisBrushChannelMatrix::affectMetallic() const
{
    return m_affectMetallic;
}

void KisBrushChannelMatrix::setAffectMetallic(bool value)
{
    m_affectMetallic = value;
}

float KisBrushChannelMatrix::opacityBaseColor() const
{
    return m_opacityBaseColor;
}

void KisBrushChannelMatrix::setOpacityBaseColor(float value)
{
    m_opacityBaseColor = clamp01(value);
}

float KisBrushChannelMatrix::opacityHeight() const
{
    return m_opacityHeight;
}

void KisBrushChannelMatrix::setOpacityHeight(float value)
{
    m_opacityHeight = clamp01(value);
}

float KisBrushChannelMatrix::normalStrength() const
{
    return m_normalStrength;
}

void KisBrushChannelMatrix::setNormalStrength(float value)
{
    m_normalStrength = clamp01(value);
}

float KisBrushChannelMatrix::roughnessValue() const
{
    return m_roughnessValue;
}

void KisBrushChannelMatrix::setRoughnessValue(float value)
{
    m_roughnessValue = clamp01(value);
}

float KisBrushChannelMatrix::metallicValue() const
{
    return m_metallicValue;
}

void KisBrushChannelMatrix::setMetallicValue(float value)
{
    m_metallicValue = clamp01(value);
}

float KisBrushChannelMatrix::heightScaleMM() const
{
    return m_heightScaleMM;
}

void KisBrushChannelMatrix::setHeightScaleMM(float value)
{
    m_heightScaleMM = qMax(0.0f, value);
}

float KisBrushChannelMatrix::heightCreaminess() const
{
    return m_heightCreaminess;
}

void KisBrushChannelMatrix::setHeightCreaminess(float value)
{
    m_heightCreaminess = qMax(0.01f, value);
}

void KisBrushChannelMatrix::applyColorOnlyPreset()
{
    m_affectBaseColor = true;
    m_affectHeight = false;
    m_affectNormal = false;
    m_affectRoughness = false;
    m_affectMetallic = false;
}

void KisBrushChannelMatrix::applyTextureOnlyPreset()
{
    m_affectBaseColor = false;
}

bool KisBrushChannelMatrix::operator==(const KisBrushChannelMatrix &other) const
{
    return m_affectBaseColor == other.m_affectBaseColor
        && m_affectHeight == other.m_affectHeight
        && m_affectNormal == other.m_affectNormal
        && m_affectRoughness == other.m_affectRoughness
        && m_affectMetallic == other.m_affectMetallic
        && fuzzyEqual(m_opacityBaseColor, other.m_opacityBaseColor)
        && fuzzyEqual(m_opacityHeight, other.m_opacityHeight)
        && fuzzyEqual(m_normalStrength, other.m_normalStrength)
        && fuzzyEqual(m_roughnessValue, other.m_roughnessValue)
        && fuzzyEqual(m_metallicValue, other.m_metallicValue)
        && fuzzyEqual(m_heightScaleMM, other.m_heightScaleMM)
        && fuzzyEqual(m_heightCreaminess, other.m_heightCreaminess);
}

bool KisBrushChannelMatrix::operator!=(const KisBrushChannelMatrix &other) const
{
    return !(*this == other);
}

QDomElement KisBrushChannelMatrix::toXmlElement(QDomDocument &doc) const
{
    QDomElement matrixElement = doc.createElement(QStringLiteral("materialChannelMatrix"));
    matrixElement.setAttribute(QStringLiteral("version"), 2);
    matrixElement.setAttribute(QStringLiteral("affectBaseColor"), m_affectBaseColor);
    matrixElement.setAttribute(QStringLiteral("affectHeight"), m_affectHeight);
    matrixElement.setAttribute(QStringLiteral("affectNormal"), m_affectNormal);
    matrixElement.setAttribute(QStringLiteral("affectRoughness"), m_affectRoughness);
    matrixElement.setAttribute(QStringLiteral("affectMetallic"), m_affectMetallic);
    matrixElement.setAttribute(QStringLiteral("opacityBaseColor"), m_opacityBaseColor);
    matrixElement.setAttribute(QStringLiteral("opacityHeight"), m_opacityHeight);
    matrixElement.setAttribute(QStringLiteral("normalStrength"), m_normalStrength);
    matrixElement.setAttribute(QStringLiteral("roughnessValue"), m_roughnessValue);
    matrixElement.setAttribute(QStringLiteral("metallicValue"), m_metallicValue);
    matrixElement.setAttribute(QStringLiteral("heightScaleMM"), m_heightScaleMM);
    matrixElement.setAttribute(QStringLiteral("heightCreaminess"), m_heightCreaminess);

    auto appendLegacyChannel = [&doc, &matrixElement](int id, bool enabled, float strength) {
        QDomElement channelElement = doc.createElement(QStringLiteral("channel"));
        channelElement.setAttribute(QStringLiteral("id"), id);
        channelElement.setAttribute(QStringLiteral("enabled"), enabled);
        channelElement.setAttribute(QStringLiteral("strength"), strength);
        matrixElement.appendChild(channelElement);
    };

    appendLegacyChannel(0, m_affectBaseColor, m_opacityBaseColor);
    appendLegacyChannel(1, m_affectHeight, m_opacityHeight);
    appendLegacyChannel(2, m_affectNormal, m_normalStrength);
    appendLegacyChannel(3, m_affectRoughness, m_roughnessValue);
    appendLegacyChannel(4, m_affectMetallic, m_metallicValue);

    return matrixElement;
}

KisBrushChannelMatrix KisBrushChannelMatrix::fromXmlElement(const QDomElement &element)
{
    KisBrushChannelMatrix matrix;
    if (element.isNull()) {
        return matrix;
    }

    const int version = element.attribute(QStringLiteral("version"), QStringLiteral("1")).toInt();
    if (version >= 2) {
        matrix.setAffectBaseColor(element.attribute(QStringLiteral("affectBaseColor"), matrix.affectBaseColor() ? QStringLiteral("true") : QStringLiteral("false")) == QStringLiteral("true"));
        matrix.setAffectHeight(element.attribute(QStringLiteral("affectHeight"), matrix.affectHeight() ? QStringLiteral("true") : QStringLiteral("false")) == QStringLiteral("true"));
        matrix.setAffectNormal(element.attribute(QStringLiteral("affectNormal"), matrix.affectNormal() ? QStringLiteral("true") : QStringLiteral("false")) == QStringLiteral("true"));
        matrix.setAffectRoughness(element.attribute(QStringLiteral("affectRoughness"), matrix.affectRoughness() ? QStringLiteral("true") : QStringLiteral("false")) == QStringLiteral("true"));
        matrix.setAffectMetallic(element.attribute(QStringLiteral("affectMetallic"), matrix.affectMetallic() ? QStringLiteral("true") : QStringLiteral("false")) == QStringLiteral("true"));
        matrix.setOpacityBaseColor(element.attribute(QStringLiteral("opacityBaseColor"), QString::number(matrix.opacityBaseColor())).toFloat());
        matrix.setOpacityHeight(element.attribute(QStringLiteral("opacityHeight"), QString::number(matrix.opacityHeight())).toFloat());
        matrix.setNormalStrength(element.attribute(QStringLiteral("normalStrength"), QString::number(matrix.normalStrength())).toFloat());
        matrix.setRoughnessValue(element.attribute(QStringLiteral("roughnessValue"), QString::number(matrix.roughnessValue())).toFloat());
        matrix.setMetallicValue(element.attribute(QStringLiteral("metallicValue"), QString::number(matrix.metallicValue())).toFloat());
        matrix.setHeightScaleMM(element.attribute(QStringLiteral("heightScaleMM"), QString::number(matrix.heightScaleMM())).toFloat());
        matrix.setHeightCreaminess(element.attribute(QStringLiteral("heightCreaminess"), QString::number(matrix.heightCreaminess())).toFloat());
    }

    for (QDomElement child = element.firstChildElement(QStringLiteral("channel")); !child.isNull(); child = child.nextSiblingElement(QStringLiteral("channel"))) {
        const int channelIndex = child.attribute(QStringLiteral("id")).toInt();
        const bool enabled = child.attribute(QStringLiteral("enabled")) == QStringLiteral("true");
        const float strength = child.attribute(QStringLiteral("strength"), QStringLiteral("1.0")).toFloat();
        switch (channelIndex) {
        case 0:
            matrix.setAffectBaseColor(enabled);
            matrix.setOpacityBaseColor(strength);
            break;
        case 1:
            matrix.setAffectHeight(enabled);
            matrix.setOpacityHeight(strength);
            break;
        case 2:
            matrix.setAffectNormal(enabled);
            matrix.setNormalStrength(strength);
            break;
        case 3:
            matrix.setAffectRoughness(enabled);
            matrix.setRoughnessValue(strength);
            break;
        case 4:
            matrix.setAffectMetallic(enabled);
            matrix.setMetallicValue(strength);
            break;
        default:
            break;
        }
    }

    return matrix;
}

QJsonObject KisBrushChannelMatrix::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("affectBaseColor"), m_affectBaseColor);
    object.insert(QStringLiteral("affectHeight"), m_affectHeight);
    object.insert(QStringLiteral("affectNormal"), m_affectNormal);
    object.insert(QStringLiteral("affectRoughness"), m_affectRoughness);
    object.insert(QStringLiteral("affectMetallic"), m_affectMetallic);
    object.insert(QStringLiteral("opacityBaseColor"), m_opacityBaseColor);
    object.insert(QStringLiteral("opacityHeight"), m_opacityHeight);
    object.insert(QStringLiteral("normalStrength"), m_normalStrength);
    object.insert(QStringLiteral("roughnessValue"), m_roughnessValue);
    object.insert(QStringLiteral("metallicValue"), m_metallicValue);
    object.insert(QStringLiteral("heightScaleMM"), m_heightScaleMM);
    object.insert(QStringLiteral("heightCreaminess"), m_heightCreaminess);
    return object;
}

KisBrushChannelMatrix KisBrushChannelMatrix::fromJson(const QJsonObject &object)
{
    KisBrushChannelMatrix matrix;
    if (object.isEmpty()) {
        return matrix;
    }

    matrix.setAffectBaseColor(object.value(QStringLiteral("affectBaseColor")).toBool(matrix.affectBaseColor()));
    matrix.setAffectHeight(object.value(QStringLiteral("affectHeight")).toBool(matrix.affectHeight()));
    matrix.setAffectNormal(object.value(QStringLiteral("affectNormal")).toBool(matrix.affectNormal()));
    matrix.setAffectRoughness(object.value(QStringLiteral("affectRoughness")).toBool(matrix.affectRoughness()));
    matrix.setAffectMetallic(object.value(QStringLiteral("affectMetallic")).toBool(matrix.affectMetallic()));
    matrix.setOpacityBaseColor(static_cast<float>(object.value(QStringLiteral("opacityBaseColor")).toDouble(matrix.opacityBaseColor())));
    matrix.setOpacityHeight(static_cast<float>(object.value(QStringLiteral("opacityHeight")).toDouble(matrix.opacityHeight())));
    matrix.setNormalStrength(static_cast<float>(object.value(QStringLiteral("normalStrength")).toDouble(matrix.normalStrength())));
    matrix.setRoughnessValue(static_cast<float>(object.value(QStringLiteral("roughnessValue")).toDouble(matrix.roughnessValue())));
    matrix.setMetallicValue(static_cast<float>(object.value(QStringLiteral("metallicValue")).toDouble(matrix.metallicValue())));
    matrix.setHeightScaleMM(static_cast<float>(object.value(QStringLiteral("heightScaleMM")).toDouble(matrix.heightScaleMM())));
    matrix.setHeightCreaminess(static_cast<float>(object.value(QStringLiteral("heightCreaminess")).toDouble(matrix.heightCreaminess())));

    return matrix;
}


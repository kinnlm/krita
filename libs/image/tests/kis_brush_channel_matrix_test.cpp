/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QTest>
#include <QDomDocument>
#include <QJsonDocument>

#include "kis_brush_channel_matrix.h"

class KisBrushChannelMatrixTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void defaults();
    void clamping();
    void serialization();
    void quickPresets();
};

void KisBrushChannelMatrixTest::defaults()
{
    KisBrushChannelMatrix matrix;
    QVERIFY(matrix.affectBaseColor());
    QVERIFY(matrix.affectHeight());
    QVERIFY(matrix.affectNormal());
    QVERIFY(!matrix.affectRoughness());
    QVERIFY(!matrix.affectMetallic());
    QCOMPARE(matrix.opacityBaseColor(), 1.0f);
    QCOMPARE(matrix.opacityHeight(), 1.0f);
    QCOMPARE(matrix.normalStrength(), 0.7f);
    QCOMPARE(matrix.roughnessValue(), 0.65f);
    QCOMPARE(matrix.metallicValue(), 0.0f);
    QCOMPARE(matrix.heightScaleMM(), 0.4f);
    QCOMPARE(matrix.heightCreaminess(), 1.6f);
}

void KisBrushChannelMatrixTest::clamping()
{
    KisBrushChannelMatrix matrix;
    matrix.setOpacityBaseColor(2.0f);
    QCOMPARE(matrix.opacityBaseColor(), 1.0f);
    matrix.setOpacityBaseColor(-1.0f);
    QCOMPARE(matrix.opacityBaseColor(), 0.0f);

    matrix.setMetallicValue(1.5f);
    QCOMPARE(matrix.metallicValue(), 1.0f);
    matrix.setMetallicValue(-0.5f);
    QCOMPARE(matrix.metallicValue(), 0.0f);

    matrix.setHeightScaleMM(-0.5f);
    QCOMPARE(matrix.heightScaleMM(), 0.0f);

    matrix.setHeightCreaminess(0.0f);
    QVERIFY(matrix.heightCreaminess() >= 0.01f);
}

void KisBrushChannelMatrixTest::serialization()
{
    KisBrushChannelMatrix matrix;
    matrix.setAffectBaseColor(false);
    matrix.setAffectHeight(false);
    matrix.setAffectNormal(false);
    matrix.setAffectRoughness(true);
    matrix.setAffectMetallic(true);
    matrix.setOpacityBaseColor(0.45f);
    matrix.setOpacityHeight(0.25f);
    matrix.setNormalStrength(0.85f);
    matrix.setRoughnessValue(0.5f);
    matrix.setMetallicValue(0.3f);
    matrix.setHeightScaleMM(1.2f);
    matrix.setHeightCreaminess(2.1f);

    const QJsonObject json = matrix.toJson();
    KisBrushChannelMatrix fromJson = KisBrushChannelMatrix::fromJson(json);
    QVERIFY(fromJson == matrix);

    QDomDocument doc;
    QDomElement root = matrix.toXmlElement(doc);
    doc.appendChild(root);
    KisBrushChannelMatrix fromXml = KisBrushChannelMatrix::fromXmlElement(root);
    QVERIFY(fromXml == matrix);
}

void KisBrushChannelMatrixTest::quickPresets()
{
    KisBrushChannelMatrix matrix;
    matrix.applyColorOnlyPreset();
    QVERIFY(matrix.affectBaseColor());
    QVERIFY(!matrix.affectHeight());
    QVERIFY(!matrix.affectNormal());
    QVERIFY(!matrix.affectRoughness());
    QVERIFY(!matrix.affectMetallic());

    matrix.applyTextureOnlyPreset();
    QVERIFY(!matrix.affectBaseColor());
}

QTEST_MAIN(KisBrushChannelMatrixTest)

#include "kis_brush_channel_matrix_test.moc"

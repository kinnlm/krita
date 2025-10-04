/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisBrushChannelMatrixWidget.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <kis_slider_spin_box.h>

#include <QMetaType>

namespace {
static QString channelLabel(int channel)
{
    switch (channel) {
    case 0: return QObject::tr("BaseColor");
    case 1: return QObject::tr("Height");
    case 2: return QObject::tr("Normal");
    case 3: return QObject::tr("Roughness");
    case 4: return QObject::tr("Metallic");
    default: return QObject::tr("Channel");
    }
}
}

struct KisBrushChannelMatrixWidget::UiElements
{
    UiElements()
    {
        colorOnlyButton = new QPushButton(QObject::tr("Color-only"));
        textureOnlyButton = new QPushButton(QObject::tr("Texture-only"));
    }

    QCheckBox *affectBaseColor {nullptr};
    QCheckBox *affectHeight {nullptr};
    QCheckBox *affectNormal {nullptr};
    QCheckBox *affectRoughness {nullptr};
    QCheckBox *affectMetallic {nullptr};

    KisDoubleSliderSpinBox *opacityBaseColor {nullptr};
    KisDoubleSliderSpinBox *opacityHeight {nullptr};
    KisDoubleSliderSpinBox *normalStrength {nullptr};
    KisDoubleSliderSpinBox *roughnessValue {nullptr};
    KisDoubleSliderSpinBox *metallicValue {nullptr};
    KisDoubleSliderSpinBox *heightScale {nullptr};
    KisDoubleSliderSpinBox *heightCreaminess {nullptr};

    QPushButton *colorOnlyButton {nullptr};
    QPushButton *textureOnlyButton {nullptr};
};

KisBrushChannelMatrixWidget::KisBrushChannelMatrixWidget(QWidget *parent)
    : QWidget(parent)
    , m_ui(new UiElements)
    , m_data(new KisBrushChannelMatrix)
{
    qRegisterMetaType<KisBrushChannelMatrix>("KisBrushChannelMatrix");
    buildUi();
    connectSignals();
    updateFromMatrix();
}

KisBrushChannelMatrixWidget::~KisBrushChannelMatrixWidget()
{
    delete m_data;
}

void KisBrushChannelMatrixWidget::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(6);

    auto *grid = new QGridLayout;
    grid->setColumnStretch(2, 1);

    auto setupRow = [this, grid](int row, QCheckBox **checkPtr, KisDoubleSliderSpinBox **sliderPtr, const QString &labelText, qreal min, qreal max, int decimals) {
        QLabel *label = new QLabel(labelText, this);
        *checkPtr = new QCheckBox(this);
        *sliderPtr = new KisDoubleSliderSpinBox(this);
        (*sliderPtr)->setRange(min, max, decimals);
        (*sliderPtr)->setSingleStep(0.05);
        grid->addWidget(label, row, 0);
        grid->addWidget(*checkPtr, row, 1);
        grid->addWidget(*sliderPtr, row, 2);
    };

    setupRow(0, &m_ui->affectBaseColor, &m_ui->opacityBaseColor, channelLabel(0), 0.0, 1.0, 2);
    setupRow(1, &m_ui->affectHeight, &m_ui->opacityHeight, channelLabel(1), 0.0, 1.0, 2);
    setupRow(2, &m_ui->affectNormal, &m_ui->normalStrength, channelLabel(2), 0.0, 1.0, 2);
    setupRow(3, &m_ui->affectRoughness, &m_ui->roughnessValue, channelLabel(3), 0.0, 1.0, 2);
    setupRow(4, &m_ui->affectMetallic, &m_ui->metallicValue, channelLabel(4), 0.0, 1.0, 2);

    QLabel *heightScaleLabel = new QLabel(tr("Height Scale (mm)"), this);
    m_ui->heightScale = new KisDoubleSliderSpinBox(this);
    m_ui->heightScale->setRange(0.0, 5.0, 2);
    m_ui->heightScale->setSingleStep(0.05);

    QLabel *heightCreaminessLabel = new QLabel(tr("Height Creaminess"), this);
    m_ui->heightCreaminess = new KisDoubleSliderSpinBox(this);
    m_ui->heightCreaminess->setRange(0.1, 4.0, 2);
    m_ui->heightCreaminess->setSingleStep(0.05);

    grid->addWidget(heightScaleLabel, 5, 0);
    grid->addWidget(m_ui->heightScale, 5, 2);
    grid->addWidget(heightCreaminessLabel, 6, 0);
    grid->addWidget(m_ui->heightCreaminess, 6, 2);

    rootLayout->addLayout(grid);

    auto *buttonsLayout = new QHBoxLayout;
    buttonsLayout->addWidget(m_ui->colorOnlyButton);
    buttonsLayout->addWidget(m_ui->textureOnlyButton);
    buttonsLayout->addStretch(1);
    rootLayout->addLayout(buttonsLayout);
}

void KisBrushChannelMatrixWidget::connectSignals()
{
    connect(m_ui->affectBaseColor, &QCheckBox::toggled, this, [this](bool value) {
        m_data->setAffectBaseColor(value);
        m_ui->opacityBaseColor->setEnabled(value);
        emit matrixChanged(*m_data);
    });
    connect(m_ui->opacityBaseColor, QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged), this, [this](qreal value) {
        m_data->setOpacityBaseColor(value);
        emit matrixChanged(*m_data);
    });

    connect(m_ui->affectHeight, &QCheckBox::toggled, this, [this](bool value) {
        m_data->setAffectHeight(value);
        m_ui->opacityHeight->setEnabled(value);
        emit matrixChanged(*m_data);
    });
    connect(m_ui->opacityHeight, QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged), this, [this](qreal value) {
        m_data->setOpacityHeight(value);
        emit matrixChanged(*m_data);
    });

    connect(m_ui->affectNormal, &QCheckBox::toggled, this, [this](bool value) {
        m_data->setAffectNormal(value);
        m_ui->normalStrength->setEnabled(value);
        emit matrixChanged(*m_data);
    });
    connect(m_ui->normalStrength, QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged), this, [this](qreal value) {
        m_data->setNormalStrength(value);
        emit matrixChanged(*m_data);
    });

    connect(m_ui->affectRoughness, &QCheckBox::toggled, this, [this](bool value) {
        m_data->setAffectRoughness(value);
        m_ui->roughnessValue->setEnabled(value);
        emit matrixChanged(*m_data);
    });
    connect(m_ui->roughnessValue, QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged), this, [this](qreal value) {
        m_data->setRoughnessValue(value);
        emit matrixChanged(*m_data);
    });

    connect(m_ui->affectMetallic, &QCheckBox::toggled, this, [this](bool value) {
        m_data->setAffectMetallic(value);
        m_ui->metallicValue->setEnabled(value);
        emit matrixChanged(*m_data);
    });
    connect(m_ui->metallicValue, QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged), this, [this](qreal value) {
        m_data->setMetallicValue(value);
        emit matrixChanged(*m_data);
    });

    connect(m_ui->heightScale, QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged), this, [this](qreal value) {
        m_data->setHeightScaleMM(value);
        emit matrixChanged(*m_data);
    });
    connect(m_ui->heightCreaminess, QOverload<qreal>::of(&KisDoubleSliderSpinBox::valueChanged), this, [this](qreal value) {
        m_data->setHeightCreaminess(value);
        emit matrixChanged(*m_data);
    });

    connect(m_ui->colorOnlyButton, &QPushButton::clicked, this, [this]() {
        m_data->applyColorOnlyPreset();
        updateFromMatrix();
        emit matrixChanged(*m_data);
    });

    connect(m_ui->textureOnlyButton, &QPushButton::clicked, this, [this]() {
        m_data->applyTextureOnlyPreset();
        updateFromMatrix();
        emit matrixChanged(*m_data);
    });
}

void KisBrushChannelMatrixWidget::updateFromMatrix()
{
    const QSignalBlocker baseColorBlocker(m_ui->affectBaseColor);
    const QSignalBlocker baseOpacityBlocker(m_ui->opacityBaseColor);
    const QSignalBlocker heightBlocker(m_ui->affectHeight);
    const QSignalBlocker heightOpacityBlocker(m_ui->opacityHeight);
    const QSignalBlocker normalBlocker(m_ui->affectNormal);
    const QSignalBlocker normalStrengthBlocker(m_ui->normalStrength);
    const QSignalBlocker roughnessBlocker(m_ui->affectRoughness);
    const QSignalBlocker roughnessValueBlocker(m_ui->roughnessValue);
    const QSignalBlocker metallicBlocker(m_ui->affectMetallic);
    const QSignalBlocker metallicValueBlocker(m_ui->metallicValue);
    const QSignalBlocker heightScaleBlocker(m_ui->heightScale);
    const QSignalBlocker heightCreaminessBlocker(m_ui->heightCreaminess);

    m_ui->affectBaseColor->setChecked(m_data->affectBaseColor());
    m_ui->opacityBaseColor->setValue(m_data->opacityBaseColor());
    m_ui->opacityBaseColor->setEnabled(m_data->affectBaseColor());
    m_ui->affectHeight->setChecked(m_data->affectHeight());
    m_ui->opacityHeight->setValue(m_data->opacityHeight());
    m_ui->opacityHeight->setEnabled(m_data->affectHeight());
    m_ui->affectNormal->setChecked(m_data->affectNormal());
    m_ui->normalStrength->setValue(m_data->normalStrength());
    m_ui->normalStrength->setEnabled(m_data->affectNormal());
    m_ui->affectRoughness->setChecked(m_data->affectRoughness());
    m_ui->roughnessValue->setValue(m_data->roughnessValue());
    m_ui->roughnessValue->setEnabled(m_data->affectRoughness());
    m_ui->affectMetallic->setChecked(m_data->affectMetallic());
    m_ui->metallicValue->setValue(m_data->metallicValue());
    m_ui->metallicValue->setEnabled(m_data->affectMetallic());
    m_ui->heightScale->setValue(m_data->heightScaleMM());
    m_ui->heightCreaminess->setValue(m_data->heightCreaminess());
}

void KisBrushChannelMatrixWidget::setMatrix(const KisBrushChannelMatrix &matrix)
{
    *m_data = matrix;
    updateFromMatrix();
}

KisBrushChannelMatrix KisBrushChannelMatrixWidget::matrix() const
{
    return *m_data;
}


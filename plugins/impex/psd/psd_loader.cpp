/*
 *  SPDX-FileCopyrightText: 2009 Boudewijn Rempt <boud@valdyas.org>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "psd_loader.h"

#include <QApplication>

#include <QStack>
#include <QMessageBox>

#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorModelStandardIds.h>
#include <KoColorProfile.h>
#include <KoCompositeOp.h>
#include <KoUnit.h>
#include <KoSvgTextShape.h>
#include <KoSvgTextShapeMarkupConverter.h>
#include <kis_shape_layer.h>
#include <KoPathShape.h>
#include <KoShapeStroke.h>
#include <kis_shape_selection.h>
#include <SvgShape.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeRegistry.h>
#include <KoDocumentResourceManager.h>
#include <KoProperties.h>

#include <kis_annotation.h>
#include <kis_types.h>
#include <kis_paint_layer.h>
#include <KisDocument.h>
#include <kis_image.h>
#include <kis_group_layer.h>
#include <kis_paint_device.h>
#include <kis_transaction.h>
#include <kis_transparency_mask.h>
#include <kis_generator_layer.h>
#include <kis_generator_registry.h>
#include <kis_guides_config.h>

#include <kis_asl_layer_style_serializer.h>
#include <asl/kis_asl_xml_parser.h>
#include <cos/psd_text_data_converter.h>
#include "KisResourceServerProvider.h"

#include "psd.h"
#include "psd_header.h"
#include "psd_colormode_block.h"
#include "psd_utils.h"
#include "psd_resource_section.h"
#include "psd_layer_section.h"
#include "psd_resource_block.h"
#include "psd_image_data.h"
#include "KisEmbeddedResourceStorageProxy.h"
#include "KisImageBarrierLock.h"
#include "KisImportUserFeedbackInterface.h"


PSDLoader::PSDLoader(KisDocument *doc, KisImportUserFeedbackInterface *feedbackInterface)
    : m_image(0)
    , m_doc(doc)
    , m_stop(false)
    , m_feedbackInterface(feedbackInterface)
{
}

PSDLoader::~PSDLoader()
{
}

KisImportExportErrorCode PSDLoader::decode(QIODevice &io)
{
    // open the file

    dbgFile << "pos:" << io.pos();

    PSDHeader header;
    if (!header.read(io)) {
        dbgFile << "failed reading header: " << header.error;
        return ImportExportCodes::FileFormatIncorrect;
    }

    dbgFile << header;
    dbgFile << "Read header. pos:" << io.pos();

    PSDColorModeBlock colorModeBlock(header.colormode);
    if (!colorModeBlock.read(io)) {
        dbgFile << "failed reading colormode block: " << colorModeBlock.error;
        return ImportExportCodes::FileFormatIncorrect;
    }

    dbgFile << "Read color mode block. pos:" << io.pos();

    PSDImageResourceSection resourceSection;
    if (!resourceSection.read(io)) {
        dbgFile << "failed image reading resource section: " << resourceSection.error;
        return ImportExportCodes::FileFormatIncorrect;
    }
    dbgFile << "Read image resource section. pos:" << io.pos();

    PSDLayerMaskSection layerSection(header);
    if (!layerSection.read(io)) {
        dbgFile << "failed reading layer/mask section: " << layerSection.error;
        return ImportExportCodes::FileFormatIncorrect;
    }
    dbgFile << "Read layer/mask section. " << layerSection.nLayers << "layers. pos:" << io.pos();

    // Done reading, except possibly for the image data block, which is only relevant if there
    // are no layers.

    // Get the right colorspace
    QPair<QString, QString> colorSpaceId = psd_colormode_to_colormodelid(header.colormode,
                                                                         header.channelDepth);
    if (colorSpaceId.first.isNull()) {
        dbgFile << "Unsupported colorspace" << header.colormode << header.channelDepth;
        return ImportExportCodes::FormatColorSpaceUnsupported;
    }

    // Get the icc profile from the image resource section
    const KoColorProfile* profile = 0;
    if (resourceSection.resources.contains(PSDImageResourceSection::ICC_PROFILE)) {
        ICC_PROFILE_1039 *iccProfileData = dynamic_cast<ICC_PROFILE_1039*>(resourceSection.resources[PSDImageResourceSection::ICC_PROFILE]->resource);
        if (iccProfileData ) {
            profile = KoColorSpaceRegistry::instance()->createColorProfile(colorSpaceId.first,
                                                                           colorSpaceId.second,
                                                                           iccProfileData->icc);
            dbgFile  << "Loaded ICC profile" << profile->name();
            delete resourceSection.resources.take(PSDImageResourceSection::ICC_PROFILE);
        }
    }

    // Create the colorspace
    const KoColorSpace* cs = KoColorSpaceRegistry::instance()->colorSpace(colorSpaceId.first, colorSpaceId.second, profile);
    if (!cs) {
        return ImportExportCodes::FormatColorSpaceUnsupported;
    }

    // Creating the KisImage
    QFile *file = dynamic_cast<QFile *>(&io);
    QString name = file ? file->fileName() : "Imported";
    m_image = new KisImage(m_doc->createUndoStore(),  header.width, header.height, cs, name);
    Q_CHECK_PTR(m_image);

    KisImageBarrierLock lock(m_image);

    // set the correct resolution
    if (resourceSection.resources.contains(PSDImageResourceSection::RESN_INFO)) {
        RESN_INFO_1005 *resInfo = dynamic_cast<RESN_INFO_1005*>(resourceSection.resources[PSDImageResourceSection::RESN_INFO]->resource);
        if (resInfo) {
            // check resolution size is not zero
            if (resInfo->hRes * resInfo->vRes > 0)
                m_image->setResolution(POINT_TO_INCH(resInfo->hRes), POINT_TO_INCH(resInfo->vRes));
            // let's skip the unit for now; we can only set that on the KisDocument, and krita doesn't use it.
            delete resourceSection.resources.take(PSDImageResourceSection::RESN_INFO);
        }
    }

    if (resourceSection.resources.contains(PSDImageResourceSection::GRID_GUIDE)) {
        GRID_GUIDE_1032 *gridGuidesInfo = dynamic_cast<GRID_GUIDE_1032*>(resourceSection.resources[PSDImageResourceSection::GRID_GUIDE]->resource);
        if (gridGuidesInfo) {
            KisGuidesConfig config = m_doc->guidesConfig();
            Q_FOREACH(quint32 guide, gridGuidesInfo->verticalGuides) {
                config.addGuideLine(Qt::Vertical, guide / m_image->xRes());
            }
            Q_FOREACH(quint32 guide, gridGuidesInfo->horizontalGuides) {
                config.addGuideLine(Qt::Horizontal, guide / m_image->yRes());
            }
            config.setShowGuides(true);
            m_doc->setGuidesConfig(config);
        }
    }

    // Preserve all the annotations
    Q_FOREACH (PSDResourceBlock *resourceBlock, resourceSection.resources.values()) {
        m_image->addAnnotation(resourceBlock);
    }

    // Preserve the duotone colormode block for saving back to psd
    if (header.colormode == DuoTone) {
        KisAnnotationSP annotation = new KisAnnotation("DuotoneColormodeBlock",
                                                       i18n("Duotone Colormode Block"),
                                                       colorModeBlock.data);
        m_image->addAnnotation(annotation);
    }

    // Load embedded patterns early for fill layers.

    const QVector<QDomDocument> &embeddedPatterns =
        layerSection.globalInfoSection.embeddedPatterns;

    const QString storageLocation = m_doc->embeddedResourcesStorageId();

    KisEmbeddedResourceStorageProxy resourceProxy(storageLocation);

    KisAslLayerStyleSerializer serializer;
    if (!embeddedPatterns.isEmpty()) {
        Q_FOREACH (const QDomDocument &doc, embeddedPatterns) {
            serializer.registerPSDPattern(doc);
        }
        Q_FOREACH (KoPatternSP pattern, serializer.patterns()) {
            if (pattern && pattern->valid()) {
                resourceProxy.addResource(pattern);
                dbgFile << "Loaded embedded pattern: " << pattern->name();
            }
            else {
                qWarning() << "Invalid or empty pattern" << pattern;
            }
        }
    }

    // Read the projection into our single layer. Since we only read the projection when
    // we have just one layer, we don't need to later on apply the alpha channel of the
    // first layer to the projection if the number of layers is negative/
    // See https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/#50577409_16000.
    if (layerSection.nLayers == 0) {
        dbgFile << "Position" << io.pos() << "Going to read the projection into the first layer, which Photoshop calls 'Background'";

        KisPaintLayerSP layer = new KisPaintLayer(m_image, i18nc("Name for the bottom-most layer in the layerstack", "Background"), OPACITY_OPAQUE_U8);

        PSDImageData imageData(&header);
        imageData.read(io, layer->paintDevice());

        m_image->addNode(layer, m_image->rootLayer());

        // Only one layer, the background layer, so we're done.
        return ImportExportCodes::OK;
    }

    // More than one layer, so now construct the Krita image from the info we read.

    QStack<KisGroupLayerSP> groupStack;
    groupStack.push(m_image->rootLayer());

    /**
     * PSD has a weird "optimization": if a group layer has only one
     * child layer, it omits it's 'psd_bounding_divider' section. So
     * fi you ever see an unbalanced layers group in PSD, most
     * probably, it is just a single layered group.
     */
    KisNodeSP lastAddedLayer;

    typedef QPair<QDomDocument, KisLayerSP> LayerStyleMapping;
    QVector<LayerStyleMapping> allStylesXml;
    using namespace std::placeholders;

    bool convertTextToShape = true;
    for (int i = 0; i < layerSection.nLayers; i++) {
        if (!layerSection.layers.at(i)->infoBlocks.textData.isNull()) {
            KisImportUserFeedbackInterface::Result result =
                    m_feedbackInterface->askUser([&] (QWidget *parent) {
                    QMessageBox::StandardButton btn = QMessageBox::question(parent,
                                                i18nc("@title:window PSD import question about text.", "Found Text Layers"),
                                                i18nc("PSD import question about text",
                                                      "Found text objects, do you wish to load them as editable text shapes? "
                                                      "If not, they will be loaded as pixel data, which will be visually"
                                                      " more accurate to the original file."));
                    return (btn == QMessageBox::Yes);
            });
            convertTextToShape = (result == KisImportUserFeedbackInterface::Success
                                  || result == KisImportUserFeedbackInterface::SuppressedByBatchMode);
            break;
        }
    }

    // read the channels for the various layers
    for(int i = 0; i < layerSection.nLayers; ++i) {

        PSDLayerRecord* layerRecord = layerSection.layers.at(i);
        dbgFile << "Going to read channels for layer" << i << layerRecord->layerName;
        KisLayerSP newLayer;
        if (layerRecord->infoBlocks.keys.contains("lsct") &&
            layerRecord->infoBlocks.sectionDividerType != psd_other) {

            if (layerRecord->infoBlocks.sectionDividerType == psd_bounding_divider && !groupStack.isEmpty()) {
                KisGroupLayerSP groupLayer = new KisGroupLayer(m_image, "temp", OPACITY_OPAQUE_U8);
                m_image->addNode(groupLayer, groupStack.top());
                groupStack.push(groupLayer);
                newLayer = groupLayer;
            }
            else if ((layerRecord->infoBlocks.sectionDividerType == psd_open_folder ||
                      layerRecord->infoBlocks.sectionDividerType == psd_closed_folder) &&
                     (groupStack.size() > 1 || (lastAddedLayer && !groupStack.isEmpty()))) {
                KisGroupLayerSP groupLayer;

                if (groupStack.size() <= 1) {
                    groupLayer = new KisGroupLayer(m_image, "temp", OPACITY_OPAQUE_U8);
                    m_image->addNode(groupLayer, groupStack.top());
                    m_image->moveNode(lastAddedLayer, groupLayer, KisNodeSP());
                } else {
                    groupLayer = groupStack.pop();
                }

                const QDomDocument &styleXml = layerRecord->infoBlocks.layerStyleXml;

                if (!styleXml.isNull()) {
                    allStylesXml << LayerStyleMapping(styleXml, groupLayer);
                }

                groupLayer->setName(layerRecord->layerName);
                groupLayer->setVisible(layerRecord->visible);

                QString compositeOp = psd_blendmode_to_composite_op(layerRecord->infoBlocks.sectionDividerBlendMode);

                // Krita doesn't support pass-through blend
                // mode. Instead it is just a property of a group
                // layer, so flip it
                if (compositeOp == COMPOSITE_PASS_THROUGH) {
                    compositeOp = COMPOSITE_OVER;
                    groupLayer->setPassThroughMode(true);
                }

                groupLayer->setCompositeOpId(compositeOp);

                newLayer = groupLayer;
            } else {
                /**
                 * In some files saved by PS CS6 the group layer sections seem
                 * to be unbalanced.  I don't know why it happens because the
                 * reporter didn't provide us an example file. So here we just
                 * check if the new layer was created, and if not, skip the
                 * initialization of masks.
                 *
                 * See bug: 357559
                 */

                warnKrita << "WARNING: Provided PSD has unbalanced group "
                          << "layer markers. Some masks and/or layers can "
                          << "be lost while loading this file. Please "
                          << "report a bug to Krita developers and attach "
                          << "this file to the bugreport\n"
                          << "    " << ppVar(layerRecord->layerName) << "\n"
                          << "    " << ppVar(layerRecord->infoBlocks.sectionDividerType) << "\n"
                          << "    " << ppVar(groupStack.size());
                continue;
            }
        } else {
            KisLayerSP layer;
            if (!layerRecord->infoBlocks.fillConfig.isNull()) {
                KisFilterConfigurationSP cfg;
                QDomDocument fillConfig;
                KisAslCallbackObjectCatcher catcher;

                KoShape *vectorMask = nullptr;
                if (layerRecord->infoBlocks.keys.contains("vmsk") || layerRecord->infoBlocks.keys.contains("vsms")) {
                    psd_vector_origination_data data;
                    if (!layerRecord->infoBlocks.vectorOriginationData.isNull()) {
                        KisAslCallbackObjectCatcher catcher;
                        psd_vector_origination_data::setupCatcher("/null", catcher, &data);
                        KisAslXmlParser parser;
                        parser.parseXML(layerRecord->infoBlocks.vectorOriginationData, catcher);
                    }
                    QString shapeName = data.shapeName();
                    const KoShapeFactoryBase *f = KoShapeRegistry::instance()->value(shapeName);
                    if (!(data.canMakeParametricShape() && f)) {
                        double width = m_image->width() / m_image->xRes();
                        double height = m_image->height() / m_image->yRes();
                        vectorMask = layerRecord->constructPathShape(layerRecord->infoBlocks.vectorMask.path, width, height);
                        vectorMask->setUserData(new KisShapeSelectionMarker);
                    } else {
                        QSizeF size;
                        double angle;
                        data.OriginalSizeAndAngle(size, angle);
                        double resMultiplier = data.originResolution/72.0;
                        QTransform scaleToPt = QTransform::fromScale(resMultiplier, resMultiplier).inverted();
                        size = QSizeF(size.width()/resMultiplier, size.height()/resMultiplier);

                        KoDocumentResourceManager manager;
                        KoProperties props;
                        if (shapeName == "RectangleShape") {
                            props.setProperty("x", 0);
                            props.setProperty("y", 0);
                            props.setProperty("width", size.width());
                            props.setProperty("height", size.height());
                        } else if (shapeName == "StarShape") {
                            props.setProperty("corners", data.originPolySides);
                            props.setProperty("convex", !data.isStar);

                            double angle = 360.0/(data.originPolySides*2);
                            double a = cos(kisDegreesToRadians(angle)) * 100.0;
                            double totalHeight = a + 100.0;
                            double l = size.height() / totalHeight * 100.0;

                            if (data.isStar) {
                                // 100% is a normal polygon.
                                a = cos(kisDegreesToRadians(angle)) * ((data.originPolyStarRatio*0.01) * l);
                                props.setProperty("baseRadius", a);
                            }
                            props.setProperty("tipRadius", l);
                            props.setProperty("baseRoundness", 0.0);
                            props.setProperty("tipRoundness", 0.0);
                        }
                        KoShape *shape = f->createShape(&props, &manager);
                        if (!shape)
                            continue;
                        shape->setSize(size);
                        QTransform t;
                        t.rotate(360.0-angle);

                        shape->setTransformation(t * scaleToPt.inverted() * data.transform * scaleToPt);
                        shape->setAbsolutePosition(scaleToPt.map(data.originShapeBBox.center()));

                        vectorMask = shape;
                    }
                }
                if (layerRecord->infoBlocks.fillType == psd_fill_gradient) {
                    cfg = KisGeneratorRegistry::instance()->value("gradient")->defaultConfiguration(resourceProxy.resourcesInterface());

                    psd_layer_gradient_fill fill;
                    fill.imageWidth = m_image->width();
                    fill.imageHeight = m_image->height();
                    psd_layer_gradient_fill::setupCatcher("/null", catcher, &fill);
                    KisAslXmlParser parser;
                    parser.parseXML(layerRecord->infoBlocks.fillConfig, catcher);
                    fillConfig = fill.getFillLayerConfig();
                    if (vectorMask) {
                        vectorMask->setBackground(fill.getBackground());
                    }

                } else if (layerRecord->infoBlocks.fillType == psd_fill_pattern) {
                    cfg = KisGeneratorRegistry::instance()->value("pattern")->defaultConfiguration(resourceProxy.resourcesInterface());

                    psd_layer_pattern_fill fill;
                    psd_layer_pattern_fill::setupCatcher("/null", catcher, &fill);

                    KisAslXmlParser parser;
                    parser.parseXML(layerRecord->infoBlocks.fillConfig, catcher);
                    fillConfig = fill.getFillLayerConfig();
                    if (vectorMask) {
                        vectorMask->setBackground(fill.getBackground(resourceProxy));
                    }

                } else {
                    cfg = KisGeneratorRegistry::instance()->value("color")->defaultConfiguration(resourceProxy.resourcesInterface());

                    psd_layer_solid_color fill;
                    fill.cs = m_image->colorSpace();
                    psd_layer_solid_color::setupCatcher("/null", catcher, &fill);
                    KisAslXmlParser parser;
                    parser.parseXML(layerRecord->infoBlocks.fillConfig, catcher);

                    fillConfig = fill.getFillLayerConfig();
                    if (vectorMask) {
                        vectorMask->setBackground(fill.getBackground());
                    }
                }
                if (vectorMask) {
                    KisShapeLayerSP shapeLayer = new KisShapeLayer(m_doc->shapeController(), m_image, layerRecord->layerName, layerRecord->opacity);


                    if (!layerRecord->infoBlocks.vectorStroke.isNull()) {
                        KoShapeStrokeSP stroke(new KoShapeStroke());
                        psd_vector_stroke_data data;
                        psd_layer_solid_color fill;
                        psd_layer_gradient_fill grad;
                        fill.cs = m_image->colorSpace();
                        KisAslCallbackObjectCatcher strokeCatcher;
                        psd_vector_stroke_data::setupCatcher("", strokeCatcher, &data);
                        psd_layer_solid_color::setupCatcher("/strokeStyle/strokeStyleContent", strokeCatcher, &fill);
                        psd_layer_gradient_fill::setupCatcher("/strokeStyle/strokeStyleContent", strokeCatcher, &grad);
                        KisAslXmlParser parser;
                        parser.parseXML(layerRecord->infoBlocks.vectorStroke, strokeCatcher);

                        if (!data.fillEnabled) {
                            vectorMask->setBackground(QSharedPointer<KoShapeBackground>(0));
                        }
                        if (data.strokeEnabled) {
                            QColor c = fill.getBrush().color();
                            c.setAlphaF(data.opacity);
                            stroke->setColor(c);
                            if (!grad.gradient.isNull()) {
                                stroke->setLineBrush(grad.getBrush());
                            }
                        } else {
                            stroke->setColor(Qt::transparent);
                        }
                        data.setupShapeStroke(stroke);

                        vectorMask->setStroke(stroke);
                    }

                    shapeLayer->addShape(vectorMask);
                    layer = shapeLayer;
                } else {
                    cfg->fromXML(fillConfig.firstChildElement());
                    cfg->createLocalResourcesSnapshot();
                    KisGeneratorLayerSP genlayer = new KisGeneratorLayer(m_image, layerRecord->layerName, cfg, m_image->globalSelection());
                    genlayer->setFilter(cfg);
                    layer = genlayer;
                }


            } else if (!layerRecord->infoBlocks.textData.isNull() && convertTextToShape) {
                KisShapeLayerSP textLayer = new KisShapeLayer(m_doc->shapeController(), m_image, layerRecord->layerName, layerRecord->opacity);
                KisAslCallbackObjectCatcher catcher;
                psd_layer_type_shape text;
                psd_layer_type_shape::setupCatcher(QString(), catcher, &text);
                KisAslXmlParser parser;
                parser.parseXML(layerRecord->infoBlocks.textData, catcher);
                KoSvgTextShape *shape = new KoSvgTextShape();
                PsdTextDataConverter converter;
                KoSvgTextShapeMarkupConverter svgConverter(shape);

                QString svg;
                QString styles;
                // This is to align inlinesize appropriately.
                bool offsetByAscent = false;
                QPointF offset1;
                // PSD text layers have all their coordinates in pixels, and because fonts can be very precise-unit sensitive,
                // we want to ensure all values are scaled appropriately.

                QTransform scaleToPt = QTransform::fromScale(m_image->xRes(), m_image->yRes()).inverted();
                bool res = converter.convertPSDTextEngineDataToSVG(text.engineData,
                                                                   layerSection.globalInfoSection.txt2Data,
                                                                   m_image->colorSpace(),
                                                                   text.textIndex,
                                                                   &svg, &styles,
                                                                   offset1, offsetByAscent,
                                                                   text.isHorizontal, scaleToPt);
                if (!res || !converter.errors().isEmpty()) {
                    qWarning() << converter.errors();
                }
                dbgFile << converter.warnings();
                svgConverter.convertFromSvg(svg, styles, m_image->bounds(), m_image->xRes()*72.0);
                if (offsetByAscent) {
                    QPointF offset2 = QPointF() - shape->outlineRect().topLeft();
                    if (text.isHorizontal) {
                        offset2.setX(offset1.x());
                    } else {
                        offset2.setY(offset1.y());
                    }
                    shape->setTransformation(QTransform::fromTranslate(offset2.x(), offset2.y()) * scaleToPt.inverted()
                                             * layerRecord->infoBlocks.textTransform * scaleToPt);
                } else {
                    shape->setTransformation(scaleToPt.inverted() * layerRecord->infoBlocks.textTransform * scaleToPt);
                }
                textLayer->addShape(shape);
                layer = textLayer;
            } else {
                layer = new KisPaintLayer(m_image, layerRecord->layerName, layerRecord->opacity);
                if (!layerRecord->readPixelData(io, layer->paintDevice())) {
                    dbgFile << "failed reading channels for layer: " << layerRecord->layerName << layerRecord->error;
                    return ImportExportCodes::FileFormatIncorrect;
                }
            }
            layer->setCompositeOpId(psd_blendmode_to_composite_op(layerRecord->blendModeKey));

            layer->setColorLabelIndex(layerRecord->labelColor);

            const QDomDocument &styleXml = layerRecord->infoBlocks.layerStyleXml;

            if (!styleXml.isNull()) {
                allStylesXml << LayerStyleMapping(styleXml, layer);
            }

            if (!groupStack.isEmpty()) {
                m_image->addNode(layer, groupStack.top());
            }
            else {
                m_image->addNode(layer, m_image->root());
            }
            layer->setVisible(layerRecord->visible);
            newLayer = layer;

        }

        Q_FOREACH (ChannelInfo *channelInfo, layerRecord->channelInfoRecords) {
            if (channelInfo->channelId < -1) {
                const KisGeneratorLayer *fillLayer = qobject_cast<KisGeneratorLayer *>(newLayer.data());
                const KisShapeLayer *shapeLayer = qobject_cast<KisShapeLayer *>(newLayer.data());
                KoPathShape *vectorMask = new KoPathShape();
                if (layerRecord->infoBlocks.keys.contains("vmsk") || layerRecord->infoBlocks.keys.contains("vsms")) {
                    double width = m_image->width() / m_image->xRes();
                    double height = m_image->height() / m_image->yRes();
                    vectorMask = layerRecord->constructPathShape(layerRecord->infoBlocks.vectorMask.path, width, height);
                    vectorMask->setUserData(new KisShapeSelectionMarker);
                }
                bool hasVectorMask = vectorMask->pointCount() > 0 && layerRecord->infoBlocks.vectorMask.path.subPaths.size() > 0;
                if (fillLayer) {
                    if (!layerRecord->readMask(io, fillLayer->paintDevice(), channelInfo)) {
                        dbgFile << "failed reading masks for generator layer: " << layerRecord->layerName << layerRecord->error;
                    }
                    if (hasVectorMask) {
                        KisShapeSelection* shapeSelection = new KisShapeSelection(m_doc->shapeController(), fillLayer->internalSelection());
                        fillLayer->internalSelection()->convertToVectorSelectionNoUndo(shapeSelection);
                        shapeSelection->addShape(vectorMask);
                        fillLayer->internalSelection()->updateProjection();
                    }
                } else {
                    if (!(shapeLayer && hasVectorMask)) {
                        KisTransparencyMaskSP mask = new KisTransparencyMask(m_image, i18n("Transparency Mask"));
                        mask->initSelection(newLayer);
                        if (!layerRecord->readMask(io, mask->paintDevice(), channelInfo)) {
                            dbgFile << "failed reading masks for layer: " << layerRecord->layerName << layerRecord->error;
                        }
                        if (hasVectorMask) {
                            KisShapeSelection* shapeSelection = new KisShapeSelection(m_doc->shapeController(), mask->selection());
                            mask->selection()->convertToVectorSelectionNoUndo(shapeSelection);
                            shapeSelection->addShape(vectorMask);
                            mask->selection()->updateProjection();
                        }
                        m_image->addNode(mask, newLayer);
                    }
                }
            }
        }

        lastAddedLayer = newLayer;
    }

    if (!allStylesXml.isEmpty()) {
        Q_FOREACH (const LayerStyleMapping &mapping, allStylesXml) {

            serializer.readFromPSDXML(mapping.first);

            if (serializer.styles().size() == 1) {
                KisPSDLayerStyleSP layerStyle = serializer.styles().first();
                KisLayerSP layer = mapping.second;

                Q_FOREACH (KoAbstractGradientSP gradient, serializer.gradients()) {
                    if (gradient && gradient->valid()) {
                        resourceProxy.addResource(gradient);
                    }
                    else {
                        qWarning() << "Invalid or empty gradient" << gradient;
                    }
                }

                Q_FOREACH (const KoPatternSP &pattern, serializer.patterns()) {
                    if (pattern && pattern->valid()) {
                        resourceProxy.addResource(pattern);
                    } else {
                        qWarning() << "Invalid or empty pattern" << pattern;
                    }
                }

                layerStyle->setName(layer->name());
                layerStyle->setResourcesInterface(resourceProxy.detachedResourcesInterface());
                if (!layerStyle->uuid().isNull()) {
                    layerStyle->setUuid(QUuid::createUuid());
                }
                layerStyle->setValid(true);

                resourceProxy.addResource(layerStyle);

                layer->setLayerStyle(layerStyle->cloneWithResourcesSnapshot(layerStyle->resourcesInterface(), 0));
            } else {
                warnKrita << "WARNING: Couldn't read layer style!" << ppVar(serializer.styles());
            }

        }
    }

    return KritaUtils::workaroundUnsuitableImageColorSpace(m_image, m_feedbackInterface, lock);
}

KisImportExportErrorCode PSDLoader::buildImage(QIODevice &io)
{
    return decode(io);
}


KisImageSP PSDLoader::image()
{
    return m_image;
}

void PSDLoader::cancel()
{
    m_stop = true;
}



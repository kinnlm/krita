/*
 *  SPDX-FileCopyrightText: 2015 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_asl_callback_object_catcher.h"

#include <QHash>

#include <QColor>
#include <QPointF>
#include <QString>

#include <KoColor.h>

#include "kis_debug.h"

typedef QHash<QString, ASLCallbackDouble> MapHashDouble;
typedef QHash<QString, ASLCallbackInteger> MapHashInt;

struct EnumMapping {
    EnumMapping(const QString &_typeId, ASLCallbackString _map)
        : typeId(_typeId)
        , map(_map)
    {
    }

    QString typeId;
    ASLCallbackString map;
};

typedef QHash<QString, EnumMapping> MapHashEnum;

struct UnitFloatMapping {
    UnitFloatMapping() {

    }
    UnitFloatMapping(const QString &_unit, ASLCallbackDouble _map)
    {
        unitMap.insert(_unit, _map);
    }

    QMap<QString, ASLCallbackDouble> unitMap;
};

struct UnitRectMapping {
    UnitRectMapping(const QString &_unit, ASLCallbackRect _map)
        : unit(_unit)
        , map(_map)
    {
    }

    QString unit;
    ASLCallbackRect map;
};

typedef QHash<QString, UnitFloatMapping> MapHashUnitFloat;
typedef QHash<QString, UnitRectMapping> MapHashUnitRect;

typedef QHash<QString, ASLCallbackString> MapHashText;
typedef QHash<QString, ASLCallbackBoolean> MapHashBoolean;
typedef QHash<QString, ASLCallbackColor> MapHashColor;
typedef QHash<QString, ASLCallbackPoint> MapHashPoint;
typedef QHash<QString, ASLCallbackCurve> MapHashCurve;
typedef QHash<QString, ASLCallbackPattern> MapHashPattern;
typedef QHash<QString, ASLCallbackPatternRef> MapHashPatternRef;
typedef QHash<QString, ASLCallbackGradient> MapHashGradient;
typedef QHash<QString, ASLCallbackRawData> MapHashRawData;
typedef QHash<QString, ASLCallbackTransform> MapHashTransform;
typedef QHash<QString, ASLCallbackRect> MapHashRect;

struct KisAslCallbackObjectCatcher::Private {
    MapHashDouble mapDouble;
    MapHashInt mapInteger;
    MapHashEnum mapEnum;
    MapHashUnitFloat mapUnitFloat;
    MapHashText mapText;
    MapHashBoolean mapBoolean;
    MapHashColor mapColor;
    MapHashPoint mapPoint;
    MapHashCurve mapCurve;
    MapHashPattern mapPattern;
    MapHashPatternRef mapPatternRef;
    MapHashGradient mapGradient;
    MapHashRawData mapRawData;
    MapHashTransform mapTransform;
    MapHashRect mapRect;
    MapHashUnitRect mapUnitRect;

    ASLCallbackNewStyle newStyleCallback;
};

KisAslCallbackObjectCatcher::KisAslCallbackObjectCatcher()
    : m_d(new Private)
{
}

KisAslCallbackObjectCatcher::~KisAslCallbackObjectCatcher()
{
}

template<class HashType, typename T>
inline void passToCallback(const QString &path, const HashType &hash, const T &value)
{
    typename HashType::const_iterator it = hash.constFind(path);
    if (it != hash.constEnd()) {
        (*it)(value);
    } else {
        warnKrita << "Unhandled:" << path << typeid(hash).name() << value;
    }
}

template<class HashType, typename T1, typename T2>
inline void passToCallback(const QString &path, const HashType &hash, const T1 &value1, const T2 &value2)
{
    typename HashType::const_iterator it = hash.constFind(path);
    if (it != hash.constEnd()) {
        (*it)(value1, value2);
    } else {
        warnKrita << "Unhandled:" << path << typeid(hash).name() << value1 << value2;
    }
}

void KisAslCallbackObjectCatcher::addDouble(const QString &path, double value)
{
    passToCallback(path, m_d->mapDouble, value);
}

void KisAslCallbackObjectCatcher::addInteger(const QString &path, int value)
{
    passToCallback(path, m_d->mapInteger, value);
}

void KisAslCallbackObjectCatcher::addEnum(const QString &path, const QString &typeId, const QString &value)
{
    MapHashEnum::const_iterator it = m_d->mapEnum.constFind(path);
    if (it != m_d->mapEnum.constEnd()) {
        if (it->typeId == typeId) {
            it->map(value);
        } else {
            warnKrita << "KisAslCallbackObjectCatcher::addEnum: inconsistent typeId" << ppVar(typeId) << ppVar(it->typeId);
        }
    }
}

void KisAslCallbackObjectCatcher::addUnitFloat(const QString &path, const QString &unit, double value)
{
    MapHashUnitFloat::const_iterator it = m_d->mapUnitFloat.constFind(path);
    if (it != m_d->mapUnitFloat.constEnd()) {
        if (it->unitMap.contains(unit)) {
            ASLCallbackDouble map = it->unitMap.value(unit);
            map(value);
        } else {
            warnKrita << "KisAslCallbackObjectCatcher::addUnitFloat: inconsistent unit" << ppVar(unit) << ppVar(it->unitMap.keys());
        }
    }
}

void KisAslCallbackObjectCatcher::addText(const QString &path, const QString &value)
{
    passToCallback(path, m_d->mapText, value);
}

void KisAslCallbackObjectCatcher::addBoolean(const QString &path, bool value)
{
    passToCallback(path, m_d->mapBoolean, value);
}

void KisAslCallbackObjectCatcher::addColor(const QString &path, const KoColor &value)
{
    passToCallback(path, m_d->mapColor, value);
}

void KisAslCallbackObjectCatcher::addPoint(const QString &path, const QPointF &value)
{
    passToCallback(path, m_d->mapPoint, value);
}

void KisAslCallbackObjectCatcher::addCurve(const QString &path, const QString &name, const QVector<QPointF> &points)
{
    MapHashCurve::const_iterator it = m_d->mapCurve.constFind(path);
    if (it != m_d->mapCurve.constEnd()) {
        (*it)(name, points);
    }
}

void KisAslCallbackObjectCatcher::addPattern(const QString &path, const KoPatternSP value, const QString &patternUuid)
{
    passToCallback(path, m_d->mapPattern, value, patternUuid);
}

void KisAslCallbackObjectCatcher::addPatternRef(const QString &path, const QString &patternUuid, const QString &patternName)
{
    MapHashPatternRef::const_iterator it = m_d->mapPatternRef.constFind(path);
    if (it != m_d->mapPatternRef.constEnd()) {
        (*it)(patternUuid, patternName);
    }
}

void KisAslCallbackObjectCatcher::addGradient(const QString &path, KoAbstractGradientSP value)
{
    passToCallback(path, m_d->mapGradient, value);
}

void KisAslCallbackObjectCatcher::newStyleStarted()
{
    if (m_d->newStyleCallback) {
        m_d->newStyleCallback();
    }
}

void KisAslCallbackObjectCatcher::addRawData(const QString &path, QByteArray ba)
{
    passToCallback(path, m_d->mapRawData, ba);
}

void KisAslCallbackObjectCatcher::addTransform(const QString &path, const QTransform &transform)
{
    passToCallback(path, m_d->mapTransform, transform);
}

void KisAslCallbackObjectCatcher::addRect(const QString &path, const QRectF &rect)
{
    passToCallback(path, m_d->mapRect, rect);
}

void KisAslCallbackObjectCatcher::addUnitRect(const QString &path, const QString &unit, const QRectF &rect)
{
    MapHashUnitRect::const_iterator it = m_d->mapUnitRect.constFind(path);
    if (it != m_d->mapUnitRect.constEnd()) {
        if (it->unit == unit) {
            it->map(rect);
        } else {
            warnKrita << "KisAslCallbackObjectCatcher::addUnitRect: inconsistent unit" << ppVar(unit) << ppVar(it->unit);
        }
    }
}

/*****************************************************************/
/*      Subscription methods                                      */
/*****************************************************************/

void KisAslCallbackObjectCatcher::subscribeDouble(const QString &path, ASLCallbackDouble callback)
{
    m_d->mapDouble.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribeInteger(const QString &path, ASLCallbackInteger callback)
{
    m_d->mapInteger.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribeEnum(const QString &path, const QString &typeId, ASLCallbackString callback)
{
    m_d->mapEnum.insert(path, EnumMapping(typeId, callback));
}

void KisAslCallbackObjectCatcher::subscribeUnitFloat(const QString &path, const QString &unit, ASLCallbackDouble callback)
{
    if (m_d->mapUnitFloat.contains(path)) {
        UnitFloatMapping mapping = m_d->mapUnitFloat.value(path);
        mapping.unitMap.insert(unit, callback);
        m_d->mapUnitFloat.insert(path, mapping);
    } else {
        m_d->mapUnitFloat.insert(path, UnitFloatMapping(unit, callback));
    }
}

void KisAslCallbackObjectCatcher::subscribeText(const QString &path, ASLCallbackString callback)
{
    m_d->mapText.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribeBoolean(const QString &path, ASLCallbackBoolean callback)
{
    m_d->mapBoolean.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribeColor(const QString &path, ASLCallbackColor callback)
{
    m_d->mapColor.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribePoint(const QString &path, ASLCallbackPoint callback)
{
    m_d->mapPoint.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribeCurve(const QString &path, ASLCallbackCurve callback)
{
    m_d->mapCurve.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribePattern(const QString &path, ASLCallbackPattern callback)
{
    m_d->mapPattern.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribePatternRef(const QString &path, ASLCallbackPatternRef callback)
{
    m_d->mapPatternRef.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribeGradient(const QString &path, ASLCallbackGradient callback)
{
    m_d->mapGradient.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribeNewStyleStarted(ASLCallbackNewStyle callback)
{
    m_d->newStyleCallback = callback;
}

void KisAslCallbackObjectCatcher::subscribeRawData(const QString &path, ASLCallbackRawData callback)
{
    m_d->mapRawData.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribeTransform(const QString &path, ASLCallbackTransform callback)
{
    m_d->mapTransform.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribeRect(const QString &path, ASLCallbackRect callback)
{
    m_d->mapRect.insert(path, callback);
}

void KisAslCallbackObjectCatcher::subscribeUnitRect(const QString &path, const QString &unit, ASLCallbackRect callback)
{
    m_d->mapUnitRect.insert(path, UnitRectMapping(unit, callback));
}

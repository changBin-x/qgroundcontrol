#pragma once

#include "MapProvider.h"

/**
 * @file GaodeMapProvider.h
 * @brief Amap (Gaode) XYZ tile providers for QGC flight maps.
 *
 * Tile endpoints match the public web CDN used by common Chinese TMS
 * integrations (no commercial API key). Coordinates are GCJ-02; expect a
 * small offset vs WGS-84 vehicle positions.
 */

static constexpr const quint32 AVERAGE_GAODE_STREET_MAP = 18000;
static constexpr const quint32 AVERAGE_GAODE_SAT_MAP    = 35000;

/**
 * Shared Gaode / AutoNavi XYZ tile provider.
 *
 * URL template uses server shard 1-4 from `_getServerNum`.
 */
class GaodeMapProvider : public MapProvider
{
protected:
    /**
     * @param mapName Display name ("Gaode Hybrid", etc.).
     * @param styleId AutoNavi style id (street / sat / labels).
     * @param imageFormat Default tile format hint ("png" or "jpg").
     * @param averageSize Typical compressed tile size for cache estimates.
     * @param mapStyle Qt map style enum value.
     * @param host Host for the tile CDN (webrd vs webst).
     */
    GaodeMapProvider(const QString &mapName, const QString &styleId, const QString &imageFormat,
                     quint32 averageSize, MapProvider::MapStyle mapStyle, const QString &host)
        : MapProvider(mapName, QStringLiteral("https://www.amap.com/"), imageFormat, averageSize, mapStyle)
        , _styleId(styleId)
        , _host(host)
    {
    }

    QString _getURL(int x, int y, int zoom) const override;

    const QString _styleId;
    const QString _host;
};

/** Gaode street / road map tiles. */
class GaodeRoadMapProvider : public GaodeMapProvider
{
public:
    GaodeRoadMapProvider()
        : GaodeMapProvider(
              QStringLiteral("Gaode Road"),
              QStringLiteral("8"),
              QStringLiteral("png"),
              AVERAGE_GAODE_STREET_MAP,
              MapProvider::StreetMap,
              QStringLiteral("webrd0%1.is.autonavi.com"))
    {
    }
};

/** Gaode pure satellite imagery (style=6). */
class GaodeSatelliteMapProvider : public GaodeMapProvider
{
public:
    GaodeSatelliteMapProvider()
        : GaodeMapProvider(
              QStringLiteral("Gaode Satellite"),
              QStringLiteral("6"),
              QStringLiteral("jpg"),
              AVERAGE_GAODE_SAT_MAP,
              MapProvider::SatelliteMapDay,
              QStringLiteral("webst0%1.is.autonavi.com"))
    {
    }
};

/**
 * Gaode hybrid: satellite base (style=6) plus road/label overlay (style=8).
 *
 * Overlay is fetched and composited in QGeoTiledMapReplyQGC.
 */
class GaodeHybridMapProvider : public GaodeMapProvider
{
public:
    GaodeHybridMapProvider()
        : GaodeMapProvider(
              QStringLiteral("Gaode Hybrid"),
              QStringLiteral("6"),
              QStringLiteral("png"),
              AVERAGE_GAODE_SAT_MAP,
              MapProvider::HybridMap,
              QStringLiteral("webst0%1.is.autonavi.com"))
    {
    }

    QUrl getOverlayTileURL(int x, int y, int zoom) const final;
};
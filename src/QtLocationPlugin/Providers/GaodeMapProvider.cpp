#include "GaodeMapProvider.h"

QString GaodeMapProvider::_getURL(int x, int y, int zoom) const
{
    // Public Amap web CDN: style selects street / sat / label layers.
    // lang/size/scale match common open TMS configs used without an API key.
    return QStringLiteral("https://%1/appmaptile?lang=zh_cn&size=1&scale=1&style=%2&x=%3&y=%4&z=%5")
        .arg(_host.arg(_getServerNum(x, y, 4)))
        .arg(_styleId)
        .arg(x)
        .arg(y)
        .arg(zoom);
}

QUrl GaodeHybridMapProvider::getOverlayTileURL(int x, int y, int zoom) const
{
    // style=8 is the transparent road + POI label layer over satellite.
    const QString url =
        QStringLiteral("https://%1/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x=%2&y=%3&z=%4")
            .arg(QStringLiteral("webst0%1.is.autonavi.com").arg(_getServerNum(x, y, 4)))
            .arg(x)
            .arg(y)
            .arg(zoom);
    return QUrl(url);
}
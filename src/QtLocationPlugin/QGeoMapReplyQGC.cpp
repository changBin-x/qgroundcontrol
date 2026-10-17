#include "QGeoMapReplyQGC.h"

#include <QtCore/QBuffer>
#include <QtCore/QFile>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtLocation/private/qgeotilespec_p.h>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QSslError>

#include "ElevationMapProvider.h"
#include "MapProvider.h"
#include "QGCCacheTile.h"
#include "QGCLoggingCategory.h"
#include "QGCMapEngine.h"
#include "QGCMapTasks.h"
#include "QGCMapUrlEngine.h"
#include "QGCNetworkHelper.h"
#include "QGeoFileTileCacheQGC.h"
#include "QGeoTileFetcherQGC.h"

QGC_LOGGING_CATEGORY(QGeoTiledMapReplyQGCLog, "QtLocationPlugin.QGeoTiledMapReplyQGC")

QByteArray QGeoTiledMapReplyQGC::_bingNoTileImage;
QByteArray QGeoTiledMapReplyQGC::_badTile;

QGeoTiledMapReplyQGC::QGeoTiledMapReplyQGC(QNetworkAccessManager *networkManager, const QNetworkRequest &request, const QGeoTileSpec &spec, QObject *parent)
    : QGeoTiledMapReply(spec, parent)
    , _networkManager(networkManager)
    , _request(request)
{
    qCDebug(QGeoTiledMapReplyQGCLog) << this;
}

QGeoTiledMapReplyQGC::~QGeoTiledMapReplyQGC()
{
    if (QNetworkReply* const reply = _networkReply.data()) {
        _networkReply.clear();
        (void) disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    qCDebug(QGeoTiledMapReplyQGCLog) << this;
}

bool QGeoTiledMapReplyQGC::init()
{
    if (m_initialized) {
        return true;
    }

    m_initialized = true;

    _initDataFromResources();

    (void) connect(this, &QGeoTiledMapReplyQGC::errorOccurred, this, [this](QGeoTiledMapReply::Error error, const QString &errorString) {
        qCWarning(QGeoTiledMapReplyQGCLog) << error << errorString;
        setMapImageData(_badTile);
        setMapImageFormat(QStringLiteral("png"));
        setCached(false);
    }, Qt::AutoConnection);

    QGCFetchTileTask *task = QGeoFileTileCacheQGC::createFetchTileTask(UrlFactory::getProviderTypeFromQtMapId(tileSpec().mapId()), tileSpec().x(), tileSpec().y(), tileSpec().zoom());
    if (!task) {
        qCWarning(QGeoTiledMapReplyQGCLog) << "Failed to create fetch tile task";
        m_initialized = false;
        return false;
    }
    (void) connect(task, &QGCFetchTileTask::tileFetched, this, &QGeoTiledMapReplyQGC::_cacheReply);
    (void) connect(task, &QGCMapTask::error, this, &QGeoTiledMapReplyQGC::_cacheError);
    if (!getQGCMapEngine()->addTask(task)) {
        task->deleteLater();
        m_initialized = false;
        return false;
    }

    return true;
}

void QGeoTiledMapReplyQGC::_initDataFromResources()
{
    if (_bingNoTileImage.isEmpty()) {
        QFile file(":/res/BingNoTileBytes.dat");
        if (file.open(QFile::ReadOnly)) {
            _bingNoTileImage = file.readAll();
            file.close();
        }
    }

    if (_badTile.isEmpty()) {
        QFile file(":/res/images/notile.png");
        if (file.open(QFile::ReadOnly)) {
            _badTile = file.readAll();
            file.close();
        }
    }
}

QByteArray QGeoTiledMapReplyQGC::_compositeOverlay(const QByteArray &baseImage, const QByteArray &overlayImage)
{
    QImage base;
    QImage overlay;
    if (!base.loadFromData(baseImage) || !overlay.loadFromData(overlayImage)) {
        return baseImage;
    }

    base = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    overlay = overlay.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (overlay.size() != base.size()) {
        overlay = overlay.scaled(base.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QPainter painter(&base);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(0, 0, overlay);
    painter.end();

    QByteArray out;
    QBuffer buffer(&out);
    if (!buffer.open(QIODevice::WriteOnly) || !base.save(&buffer, "PNG")) {
        return baseImage;
    }
    return out;
}

void QGeoTiledMapReplyQGC::_finishWithImage(const QByteArray &image, const QString &providerName)
{
    setMapImageData(image);

    const SharedMapProvider mapProvider = UrlFactory::getMapProviderFromProviderType(providerName);
    const QString format = mapProvider ? mapProvider->getImageFormat(image) : QStringLiteral("png");
    if (format.isEmpty()) {
        setError(QGeoTiledMapReply::ParseError, tr("Unknown Format"));
        return;
    }
    setMapImageFormat(format);

    QGeoFileTileCacheQGC::cacheTile(providerName, tileSpec().x(), tileSpec().y(), tileSpec().zoom(), image, format);
    setFinished(true);
}

void QGeoTiledMapReplyQGC::_startOverlayFetch(const QUrl &overlayUrl)
{
    if (!_networkManager || !overlayUrl.isValid()) {
        _finishWithImage(_pendingBaseImage, UrlFactory::getProviderTypeFromQtMapId(tileSpec().mapId()));
        _pendingBaseImage.clear();
        return;
    }

    QNetworkRequest overlayRequest = QGeoTileFetcherQGC::getNetworkRequest(
        tileSpec().mapId(), tileSpec().x(), tileSpec().y(), tileSpec().zoom());
    // getNetworkRequest builds the base URL; replace with overlay URL.
    overlayRequest.setUrl(overlayUrl);
    overlayRequest.setOriginatingObject(this);

    _fetchingOverlay = true;
    _networkReply = _networkManager->get(overlayRequest);
    if (!_networkReply) {
        _fetchingOverlay = false;
        _finishWithImage(_pendingBaseImage, UrlFactory::getProviderTypeFromQtMapId(tileSpec().mapId()));
        _pendingBaseImage.clear();
        return;
    }

    QNetworkReply* const reply = _networkReply.data();
    QGCNetworkHelper::ignoreSslErrorsIfNeeded(reply);
    (void) connect(reply, &QNetworkReply::finished, this, &QGeoTiledMapReplyQGC::_networkReplyFinished);
    (void) connect(reply, &QNetworkReply::errorOccurred, this, &QGeoTiledMapReplyQGC::_networkReplyError);
    (void) connect(reply, &QNetworkReply::sslErrors, this, &QGeoTiledMapReplyQGC::_networkReplySslErrors);
    (void) connect(this, &QGeoTiledMapReplyQGC::aborted, reply, &QNetworkReply::abort);
}

void QGeoTiledMapReplyQGC::_networkReplyFinished()
{
    QNetworkReply* const reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        setError(QGeoTiledMapReply::UnknownError, tr("Unexpected Error"));
        return;
    }
    if (reply == _networkReply) {
        _networkReply.clear();
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (_fetchingOverlay && !_pendingBaseImage.isEmpty()) {
            // Overlay failed: still show satellite base rather than failing the tile.
            _fetchingOverlay = false;
            _finishWithImage(_pendingBaseImage, UrlFactory::getProviderTypeFromQtMapId(tileSpec().mapId()));
            _pendingBaseImage.clear();
        }
        return;
    }

    if (!reply->isOpen()) {
        setError(QGeoTiledMapReply::ParseError, tr("Empty Reply"));
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!QGCNetworkHelper::isHttpSuccess(statusCode)) {
        if (_fetchingOverlay && !_pendingBaseImage.isEmpty()) {
            _fetchingOverlay = false;
            _finishWithImage(_pendingBaseImage, UrlFactory::getProviderTypeFromQtMapId(tileSpec().mapId()));
            _pendingBaseImage.clear();
            return;
        }
        setError(QGeoTiledMapReply::CommunicationError, reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
        return;
    }

    QByteArray image = reply->readAll();
    if (image.isEmpty()) {
        setError(QGeoTiledMapReply::ParseError, tr("Image is Empty"));
        return;
    }

    const SharedMapProvider mapProvider = UrlFactory::getMapProviderFromQtMapId(tileSpec().mapId());
    if (!mapProvider) {
        setError(QGeoTiledMapReply::UnknownError, tr("Invalid Map Provider"));
        return;
    }

    if (_fetchingOverlay) {
        _fetchingOverlay = false;
        const QByteArray composited = _compositeOverlay(_pendingBaseImage, image);
        _pendingBaseImage.clear();
        _finishWithImage(composited, mapProvider->getMapName());
        return;
    }

    if (mapProvider->isBingProvider() && (image == _bingNoTileImage)) {
        setError(QGeoTiledMapReply::CommunicationError, tr("Bing Tile Above Zoom Level"));
        return;
    }

    if (mapProvider->isElevationProvider()) {
        const SharedElevationProvider elevationProvider = std::dynamic_pointer_cast<const ElevationProvider>(mapProvider);
        image = elevationProvider->serialize(image);
        if (image.isEmpty()) {
            setError(QGeoTiledMapReply::ParseError, tr("Failed to Serialize Terrain Tile"));
            return;
        }
    }

    const QUrl overlayUrl = mapProvider->getOverlayTileURL(tileSpec().x(), tileSpec().y(), tileSpec().zoom());
    if (overlayUrl.isValid()) {
        _pendingBaseImage = image;
        _startOverlayFetch(overlayUrl);
        return;
    }

    _finishWithImage(image, mapProvider->getMapName());
}

void QGeoTiledMapReplyQGC::_networkReplyError(QNetworkReply::NetworkError error)
{
    if (error != QNetworkReply::OperationCanceledError) {
        if (_fetchingOverlay && !_pendingBaseImage.isEmpty()) {
            _fetchingOverlay = false;
            _finishWithImage(_pendingBaseImage, UrlFactory::getProviderTypeFromQtMapId(tileSpec().mapId()));
            _pendingBaseImage.clear();
            return;
        }
        const QNetworkReply* const reply = qobject_cast<const QNetworkReply*>(sender());
        if (!reply) {
            setError(QGeoTiledMapReply::CommunicationError, tr("Invalid Reply"));
        } else {
            setError(QGeoTiledMapReply::CommunicationError, reply->errorString());
        }
    } else {
        setFinished(true);
    }
}

void QGeoTiledMapReplyQGC::_networkReplySslErrors(const QList<QSslError> &errors)
{
    QString errorString;
    for (const QSslError &error : errors) {
        if (!errorString.isEmpty()) {
            (void) errorString.append('\n');
        }
        (void) errorString.append(error.errorString());
    }

    if (!errorString.isEmpty()) {
        setError(QGeoTiledMapReply::CommunicationError, errorString);
    }
}

void QGeoTiledMapReplyQGC::_cacheReply(QGCCacheTile *tile)
{
    if (tile) {
        setMapImageData(tile->img);
        setMapImageFormat(tile->format);
        setCached(true);
        setFinished(true);
        delete tile;
    } else {
        setError(QGeoTiledMapReply::UnknownError, tr("Invalid Cache Tile"));
    }
}

void QGeoTiledMapReplyQGC::_cacheError(QGCMapTask::TaskType type, QStringView errorString)
{
    Q_UNUSED(errorString);

    if (type != QGCMapTask::TaskType::taskFetchTile) {
        setError(QGeoTiledMapReply::UnknownError, tr("Unexpected Cache Error"));
        return;
    }

    if (!_networkManager) {
        setError(QGeoTiledMapReply::CommunicationError, tr("Network Manager Not Available"));
        return;
    }

    if (!QGCNetworkHelper::isInternetAvailable()) {
        setError(QGeoTiledMapReply::CommunicationError, tr("Network Not Available"));
        return;
    }

    _request.setOriginatingObject(this);

    _networkReply = _networkManager->get(_request);
    if (!_networkReply) {
        setError(QGeoTiledMapReply::CommunicationError, tr("Failed to Create Network Reply"));
        return;
    }

    QNetworkReply* const reply = _networkReply.data();
    QGCNetworkHelper::ignoreSslErrorsIfNeeded(reply);

    (void) connect(reply, &QNetworkReply::finished, this, &QGeoTiledMapReplyQGC::_networkReplyFinished);
    (void) connect(reply, &QNetworkReply::errorOccurred, this, &QGeoTiledMapReplyQGC::_networkReplyError);
    (void) connect(reply, &QNetworkReply::sslErrors, this, &QGeoTiledMapReplyQGC::_networkReplySslErrors);
    (void) connect(this, &QGeoTiledMapReplyQGC::aborted, reply, &QNetworkReply::abort);
}

void QGeoTiledMapReplyQGC::abort()
{
    QGeoTiledMapReply::abort();
}

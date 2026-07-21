/***************************************************************************
    ngsplashscreenrenderer.h
    ---------------------
    begin                : July 2026
    copyright            : (C) 2026 by NextGIS
    email                : info at nextgis dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef NGSPLASHSCREENRENDERER_H
#define NGSPLASHSCREENRENDERER_H

#include "qgis_app.h"

#include <QElapsedTimer>
#include <QRect>
#include <QSvgRenderer>

class QColor;
class QPainter;
class QPixmap;
class QString;

/**
 * Handles rendering of the QGIS splash screen dynamic content.
 */
class APP_EXPORT NgSplashScreenRenderer
{
  public:
    explicit NgSplashScreenRenderer( const QString &splashPath );

    static QPixmap createSplashPixmap( const QString &splashPath, qreal devicePixelRatio );
    static QColor statusTextColor();

    void renderDynamicContent( QPainter *painter, const QRect &rect, const QString &statusText );

  private:
    static QString splashFilePath( const QString &basePath, const QString &fileName );
    static QString formatSvgNumber( qreal value );
    static void replaceSvgElementAttribute( QString &svgContent, const QString &elementName, const QString &elementId, const QString &attributeName, const QString &attributeValue );
    static void updateStabilityBadge( QString &svgContent, const QString &stabilityText );

    void renderLoading( QPainter *painter );
    void renderStatusText( QPainter *painter, const QRect &rect, const QString &statusText );

    QSvgRenderer mLoadingRenderer;
    QElapsedTimer mLoadingAnimationTime;
};

#endif // NGSPLASHSCREENRENDERER_H

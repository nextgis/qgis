/***************************************************************************
    ngsplashscreenrenderer.cpp
    -----------------------
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

#include "ngsplashscreenrenderer.h"

#include "qgis.h"

#include <QColor>
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>

#include <cmath>

NgSplashScreenRenderer::NgSplashScreenRenderer( const QString &splashPath )
{
  const bool nightlyBuild = Qgis::ngqIsNightlyBuild();
  const QString loadingFileName = nightlyBuild ? QStringLiteral( "loading_nightly.svg" ) : QStringLiteral( "loading.svg" );

  mLoadingRenderer.load( splashFilePath( splashPath, loadingFileName ) );

  if ( !mLoadingRenderer.isValid() )
    mLoadingRenderer.load( QStringLiteral( ":/images/splash/%1" ).arg( loadingFileName ) );

  if ( !mLoadingRenderer.isValid() && nightlyBuild )
    mLoadingRenderer.load( QStringLiteral( ":/images/splash/loading.svg" ) );

  mLoadingAnimationTime.start();
}

QPixmap NgSplashScreenRenderer::createSplashPixmap( const QString &splashPath, const qreal devicePixelRatio )
{
  const bool nightlyBuild = Qgis::ngqIsNightlyBuild();
  const QString splashFileName = nightlyBuild ? QStringLiteral( "stable_nightly_splash.svg" ) : QStringLiteral( "stable_splash.svg" );

  QFile svgFile( splashFilePath( splashPath, splashFileName ) );
  bool svgOpened = svgFile.open( QIODevice::ReadOnly );
  if ( !svgOpened && nightlyBuild )
  {
    svgFile.setFileName( splashFilePath( splashPath, QStringLiteral( "stable_splash.svg" ) ) );
    svgOpened = svgFile.open( QIODevice::ReadOnly );
  }

  if ( svgOpened )
  {
    QString svgContent = QString::fromUtf8( svgFile.readAll() );
    const QString stabilityText = QObject::tr( "Stable version" );
    svgContent.replace( QStringLiteral( "{{stability}}" ), stabilityText.toHtmlEscaped() );
    updateStabilityBadge( svgContent, stabilityText );

    const QString versionInfo = QObject::tr( "v%1 • based on QGIS %2" )
                                .arg( Qgis::ngqFullVersion(), Qgis::version().section( '-', 0, 0 ) );
    svgContent.replace( QStringLiteral( "{{version_info}}" ), versionInfo.toHtmlEscaped() );

    QSvgRenderer svgRenderer( svgContent.toUtf8() );
    if ( svgRenderer.isValid() )
    {
      QSize splashSize = svgRenderer.defaultSize();
      if ( splashSize.isEmpty() )
        splashSize = QSize( 750, 350 );

      const QSize renderSize( std::ceil( splashSize.width() * devicePixelRatio ), std::ceil( splashSize.height() * devicePixelRatio ) );
      QPixmap pixmap( renderSize );
      pixmap.fill( Qt::transparent );
      pixmap.setDevicePixelRatio( devicePixelRatio );

      QPainter painter( &pixmap );
      painter.setRenderHint( QPainter::Antialiasing, true );
      painter.setRenderHint( QPainter::TextAntialiasing, true );
      painter.setRenderHint( QPainter::SmoothPixmapTransform, true );
      svgRenderer.render( &painter, QRectF( QPointF( 0, 0 ), QSizeF( splashSize ) ) );
      return pixmap;
    }
  }

  return QPixmap( splashFilePath( splashPath, QStringLiteral( "splash.png" ) ) );
}

QColor NgSplashScreenRenderer::statusTextColor()
{
  return Qgis::ngqIsNightlyBuild() ? QColor( 255, 255, 255, 204 ) : QColor( QStringLiteral( "#3f3f3f" ) );
}

void NgSplashScreenRenderer::renderDynamicContent( QPainter *painter, const QRect &rect, const QString &statusText )
{
  if ( !painter )
    return;

  painter->setRenderHint( QPainter::Antialiasing, true );
  painter->setRenderHint( QPainter::TextAntialiasing, true );
  painter->setRenderHint( QPainter::SmoothPixmapTransform, true );

  renderLoading( painter );
  renderStatusText( painter, rect, statusText );
}

QString NgSplashScreenRenderer::splashFilePath( const QString &basePath, const QString &fileName )
{
  if ( basePath.endsWith( QLatin1Char( '/' ) ) )
    return basePath + fileName;

  return basePath + QLatin1Char( '/' ) + fileName;
}

QString NgSplashScreenRenderer::formatSvgNumber( const qreal value )
{
  QString number = QString::number( value, 'f', 2 );

  while ( number.contains( QLatin1Char( '.' ) ) && number.endsWith( QLatin1Char( '0' ) ) )
    number.chop( 1 );

  if ( number.endsWith( QLatin1Char( '.' ) ) )
    number.chop( 1 );

  return number;
}

void NgSplashScreenRenderer::replaceSvgElementAttribute( QString &svgContent, const QString &elementName, const QString &elementId, const QString &attributeName, const QString &attributeValue )
{
  const QRegularExpression attributeExpression(
    QStringLiteral( R"((<%1\b(?=[^>]*\bid="%2")[^>]*\b%3=")([^"]*)("))" )
    .arg( QRegularExpression::escape( elementName ),
          QRegularExpression::escape( elementId ),
          QRegularExpression::escape( attributeName ) ) );
  svgContent.replace( attributeExpression, QStringLiteral( "\\1%1\\3" ).arg( attributeValue ) );
}

void NgSplashScreenRenderer::updateStabilityBadge( QString &svgContent, const QString &stabilityText )
{
  constexpr qreal badgeX = 74.0;
  constexpr qreal badgeY = 203.0;
  constexpr qreal badgeHeight = 38.0;
  constexpr qreal textX = 126.0;
  constexpr qreal rightPadding = 20.0;

  QFont badgeFont( QStringLiteral( "Inter" ) );
  badgeFont.setPixelSize( 17 );
  badgeFont.setWeight( QFont::Normal );

  const QFontMetricsF badgeFontMetrics( badgeFont );
  const qreal textWidth = badgeFontMetrics.horizontalAdvance( stabilityText );
  const qreal textBaselineY = badgeY + ( badgeHeight - badgeFontMetrics.height() ) / 2.0 + badgeFontMetrics.ascent();
  const qreal badgeWidth = textX - badgeX + textWidth + rightPadding;

  const QString svgTextX = formatSvgNumber( textX );
  const QString svgTextBaselineY = formatSvgNumber( textBaselineY );

  replaceSvgElementAttribute( svgContent, QStringLiteral( "rect" ), QStringLiteral( "rect24" ), QStringLiteral( "width" ), formatSvgNumber( badgeWidth ) );
  replaceSvgElementAttribute( svgContent, QStringLiteral( "text" ), QStringLiteral( "text24" ), QStringLiteral( "x" ), svgTextX );
  replaceSvgElementAttribute( svgContent, QStringLiteral( "text" ), QStringLiteral( "text24" ), QStringLiteral( "y" ), svgTextBaselineY );
  replaceSvgElementAttribute( svgContent, QStringLiteral( "tspan" ), QStringLiteral( "tspan24" ), QStringLiteral( "x" ), svgTextX );
  replaceSvgElementAttribute( svgContent, QStringLiteral( "tspan" ), QStringLiteral( "tspan24" ), QStringLiteral( "y" ), svgTextBaselineY );
}

void NgSplashScreenRenderer::renderLoading( QPainter *painter )
{
  if ( !mLoadingRenderer.isValid() )
    return;

  constexpr qreal loadingStartX = -750.0;
  constexpr qreal loadingY = 347.0;
  constexpr qreal loadingWidth = 750.0;
  constexpr qreal loadingHeight = 3.0;
  constexpr qint64 animationDurationMs = 1800;

  const qint64 elapsedMs = mLoadingAnimationTime.isValid() ? mLoadingAnimationTime.elapsed() : 0;
  const qreal progress = static_cast<qreal>( elapsedMs % animationDurationMs ) / static_cast<qreal>( animationDurationMs );
  const qreal loadingX = loadingStartX + loadingWidth * progress;

  for ( qreal x = loadingX; x < loadingWidth; x += loadingWidth )
  {
    mLoadingRenderer.render( painter, QRectF( x, loadingY, loadingWidth, loadingHeight ) );
  }
}

void NgSplashScreenRenderer::renderStatusText( QPainter *painter, const QRect &rect, const QString &statusText )
{
  if ( statusText.isEmpty() )
    return;

  QFont statusFont = painter->font();
  statusFont.setFamily( QStringLiteral( "Inter" ) );

  constexpr qreal statusBottomOffset = 10.0;

  painter->save();
  painter->setFont( statusFont );
  painter->setPen( statusTextColor() );
  painter->drawText( QRectF( rect ).adjusted( 0, 0, 0, -statusBottomOffset ), Qt::AlignHCenter | Qt::AlignBottom, statusText );
  painter->restore();
}

/***************************************************************************
    ngsplashscreen.cpp
    -------------------
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

#include "ngsplashscreen.h"

#include "ngsplashscreenrenderer.h"

#include <QPainter>

NgSplashScreen::NgSplashScreen( const QString &splashPath, const qreal devicePixelRatio )
  : QSplashScreen( NgSplashScreenRenderer::createSplashPixmap( splashPath, devicePixelRatio ) )
  , mRenderer( std::make_unique<NgSplashScreenRenderer>( splashPath ) )
{
  mAnimationTimer.setInterval( 16 );
  connect( &mAnimationTimer, &QTimer::timeout, this, [this]
  {
    repaint();
  } );
  mAnimationTimer.start();
}

NgSplashScreen::~NgSplashScreen() = default;

void NgSplashScreen::drawContents( QPainter *painter )
{
  mRenderer->renderDynamicContent( painter, rect(), message() );
}

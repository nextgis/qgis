/***************************************************************************
    ngsplashscreen.h
    -----------------
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

#ifndef NGSPLASHSCREEN_H
#define NGSPLASHSCREEN_H

#include "qgis_app.h"

#include <QSplashScreen>
#include <QTimer>

#include <memory>

class QPainter;
class QString;
class NgSplashScreenRenderer;

/**
 * Splash screen widget with custom dynamic content rendering.
 */
class APP_EXPORT NgSplashScreen : public QSplashScreen
{
  public:
    explicit NgSplashScreen( const QString &splashPath, qreal devicePixelRatio );
    ~NgSplashScreen() override;

  protected:
    void drawContents( QPainter *painter ) override;

  private:
    std::unique_ptr<NgSplashScreenRenderer> mRenderer;
    QTimer mAnimationTimer;
};

#endif // NGSPLASHSCREEN_H

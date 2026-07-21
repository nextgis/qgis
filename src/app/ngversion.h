/***************************************************************************
    ngversion.h
    -----------
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

#ifndef NGVERSION_H
#define NGVERSION_H

#include "qgis_app.h"

class QString;

class APP_EXPORT NgVersion
{
  public:
    static bool stableVersionRequested();
    static QString nextgisQgisVersion();
};

#endif // NGVERSION_H

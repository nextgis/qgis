/***************************************************************************
    ngversion.cpp
    -------------
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

#include "ngversion.h"

#include "qgsconfig.h"

#include <QByteArray>
#include <QString>

bool NgVersion::stableVersionRequested()
{
  const QByteArray stableValue = qgetenv( "NGQ_STABLE_VERSION" ).trimmed().toLower();
  return !stableValue.isEmpty()
         && stableValue != "0"
         && stableValue != "false"
         && stableValue != "no"
         && stableValue != "off";
}

QString NgVersion::nextgisQgisVersion()
{
  QString version = QLatin1String( NGQ_VERSION );

  if ( !stableVersionRequested() )
  {
    version += QStringLiteral( "-nightly.%1" ).arg( QLatin1String( NGQ_BUILD_NUMBER ) );
  }

  return version;
}

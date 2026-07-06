/***************************************************************************
    qgsngutils.h
    ------------
    begin                : July 2026
    copyright            : (C) 2026 by NextGIS
***************************************************************************/

#ifndef QGSNGUTILS_H
#define QGSNGUTILS_H

#include "qgis_core.h"

#include <QString>

namespace QgsNgUtils
{
  CORE_EXPORT QString locale();
  CORE_EXPORT QString nextgisDomain( const QString &subdomain = QString() );
  CORE_EXPORT QString utmTags( const QString &utmMedium,
                               const QString &utmCampaign = QString() );
}

#endif // QGSNGUTILS_H
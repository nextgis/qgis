/***************************************************************************
    qgsngutils.cpp
    --------------
    begin                : July 2026
    copyright            : (C) 2026 by NextGIS
***************************************************************************/

#include "qgsngutils.h"

#include "qgsapplication.h"

#include <QLocale>
#include <QSet>
#include <QUrl>

namespace
{
  bool isRussianSpeaking()
  {
    static const QSet<QString> sRussianSpeakingLocales
    {
      QStringLiteral( "be" ),
      QStringLiteral( "kk" ),
      QStringLiteral( "ky" ),
      QStringLiteral( "ru" ),
      QStringLiteral( "uk" ),
    };

    return sRussianSpeakingLocales.contains( QgsNgUtils::locale() );
  }
}

namespace QgsNgUtils
{
  QString locale()
  {
    const bool overrideLocale = QgsApplication::settingsLocaleOverrideFlag->value();
    const QString localeFullName = overrideLocale
                                   ? QgsApplication::settingsLocaleUserLocale->value()
                                   : QLocale::system().name();
    const QString localeShort = localeFullName.left( 2 ).toLower();

    return localeShort.isEmpty() || localeShort == QLatin1String( "c" )
           ? QStringLiteral( "en" )
           : localeShort;
  }

  QString nextgisDomain( const QString &subdomain )
  {
    QString subdomainPrefix = subdomain.trimmed();
    if ( !subdomainPrefix.isEmpty() && !subdomainPrefix.endsWith( QLatin1Char( '.' ) ) )
      subdomainPrefix += QLatin1Char( '.' );

    return QStringLiteral( "https://%1nextgis.%2" )
      .arg( subdomainPrefix,
            isRussianSpeaking() ? QStringLiteral( "ru" )
                                : QStringLiteral( "com" ) );
  }

  QString utmTags( const QString &utmMedium, const QString &utmCampaign )
  {
    const QString campaign = utmCampaign.isEmpty()
                             ? QStringLiteral( "constant" )
                             : utmCampaign;

    return QStringLiteral( "utm_source=qgis&utm_medium=%1&utm_campaign=%2&utm_content=%3" )
      .arg( QString::fromLatin1( QUrl::toPercentEncoding( utmMedium ) ),
            QString::fromLatin1( QUrl::toPercentEncoding( campaign ) ),
            QString::fromLatin1( QUrl::toPercentEncoding( locale() ) ) );
  }
}

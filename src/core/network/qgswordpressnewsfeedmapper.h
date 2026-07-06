/***************************************************************************
    qgswordpressnewsfeedmapper.h
    ---------------------------
    begin                : July 2026
    copyright            : (C) 2026 by NextGIS
 ***************************************************************************/

#ifndef QGSWORDPRESSNEWSFEEDMAPPER_H
#define QGSWORDPRESSNEWSFEEDMAPPER_H

#include "qgis_core.h"

#include <memory>
#include <QUrl>
#include <QVariantMap>

class CORE_EXPORT QgsWordPressNewsFeedMapper
{
  public:

    virtual ~QgsWordPressNewsFeedMapper() = default;

    static bool canMap( const QVariantMap &post );
    static std::unique_ptr<QgsWordPressNewsFeedMapper> create( const QVariantMap &post );

    QVariantMap toQgisFeedEntry( const QVariantMap &post ) const;

  protected:

    virtual QString mapUrl( const QVariantMap &post ) const;

  private:

    struct ImageCandidate
    {
      QString url;
      int width = 0;
    };

    static QString decodeAndTrim( const QString &html );
    static QString sanitizeHtml( const QString &html );
    static QString mapImageUrl( const QVariantMap &post );
    static qlonglong mapPublishFrom( const QVariantMap &post );
};

class CORE_EXPORT QgsNextGISNewsFeedMapper final : public QgsWordPressNewsFeedMapper
{
  protected:

    QString mapUrl( const QVariantMap &post ) const override;
};

#endif // QGSWORDPRESSNEWSFEEDMAPPER_H

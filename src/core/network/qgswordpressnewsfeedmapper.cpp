/***************************************************************************
    qgswordpressnewsfeedmapper.cpp
    -----------------------------
    begin                : July 2026
    copyright            : (C) 2026 by NextGIS
 ***************************************************************************/

#include "qgswordpressnewsfeedmapper.h"

#include "qgsngutils.h"

#include <QDateTime>
#include <QFont>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QUrlQuery>

#include <limits>

namespace
{
  QString wrapInlineText( const QString &text, const QTextCharFormat &format )
  {
    QString result = text.toHtmlEscaped();
    result.replace( QChar::LineSeparator, QStringLiteral( "<br>" ) );
    result.replace( QLatin1Char( '\n' ), QStringLiteral( "<br>" ) );

    if ( format.fontItalic() )
      result = QStringLiteral( "<em>%1</em>" ).arg( result );

    if ( format.fontWeight() > QFont::Normal )
      result = QStringLiteral( "<strong>%1</strong>" ).arg( result );

    if ( format.isAnchor() && !format.anchorHref().isEmpty() )
    {
      result = QStringLiteral( "<a href=\"%1\">%2</a>" )
                 .arg( format.anchorHref().toHtmlEscaped(), result );
    }

    return result;
  }

  QString sanitizeBlock( const QTextBlock &block )
  {
    QString blockHtml;

    for ( QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it )
    {
      const QTextFragment fragment = it.fragment();
      if ( !fragment.isValid() )
        continue;

      QString text = fragment.text();
      text.remove( QChar::ParagraphSeparator );
      if ( text.isEmpty() )
        continue;

      blockHtml += wrapInlineText( text, fragment.charFormat() );
    }

    if ( blockHtml.trimmed().isEmpty() )
      return QString();

    return QStringLiteral( "<p>%1</p>" ).arg( blockHtml );
  }
}

bool QgsWordPressNewsFeedMapper::canMap( const QVariantMap &post )
{
  return post.contains( QStringLiteral( "id" ) )
         && post.value( QStringLiteral( "title" ) ).toMap().contains(
              QStringLiteral( "rendered" ) );
}

std::unique_ptr<QgsWordPressNewsFeedMapper> QgsWordPressNewsFeedMapper::create( const QVariantMap &post )
{
  const QUrl link( post.value( QStringLiteral( "link" ) ).toString() );
  const QString host = link.host().toLower();
  if ( host == QLatin1String( "nextgis.com" )
       || host == QLatin1String( "nextgis.ru" )
       || host.endsWith( QLatin1String( ".nextgis.com" ) )
       || host.endsWith( QLatin1String( ".nextgis.ru" ) ) )
  {
    return std::make_unique<QgsNextGISNewsFeedMapper>();
  }

  return std::make_unique<QgsWordPressNewsFeedMapper>();
}

QVariantMap QgsWordPressNewsFeedMapper::toQgisFeedEntry( const QVariantMap &post ) const
{
  QVariantMap entry;
  entry.insert( QStringLiteral( "pk" ), post.value( QStringLiteral( "id" ) ) );
  entry.insert( QStringLiteral( "title" ),
                decodeAndTrim( post.value( QStringLiteral( "title" ) )
                                 .toMap()
                                 .value( QStringLiteral( "rendered" ) )
                                 .toString() ) );
  entry.insert( QStringLiteral( "image" ), mapImageUrl( post ) );
  entry.insert( QStringLiteral( "content" ),
                sanitizeHtml( post.value( QStringLiteral( "excerpt" ) )
                                .toMap()
                                .value( QStringLiteral( "rendered" ) )
                                .toString() ) );
  entry.insert( QStringLiteral( "url" ), mapUrl( post ) );
  entry.insert( QStringLiteral( "sticky" ),
                post.value( QStringLiteral( "sticky" ) ).toBool() );

  const qlonglong publishFrom = mapPublishFrom( post );
  if ( publishFrom > 0 )
    entry.insert( QStringLiteral( "publish_from" ), publishFrom );

  return entry;
}

QString QgsWordPressNewsFeedMapper::mapUrl( const QVariantMap &post ) const
{
  return post.value( QStringLiteral( "link" ) ).toString();
}

QString QgsWordPressNewsFeedMapper::decodeAndTrim( const QString &html )
{
  return QTextDocumentFragment::fromHtml( html ).toPlainText().trimmed();
}

QString QgsWordPressNewsFeedMapper::sanitizeHtml( const QString &html )
{
  QTextDocument document;
  document.setHtml( html );

  QStringList sanitizedBlocks;
  for ( QTextBlock block = document.begin(); block.isValid(); block = block.next() )
  {
    const QString sanitizedBlock = sanitizeBlock( block );
    if ( !sanitizedBlock.isEmpty() )
      sanitizedBlocks.append( sanitizedBlock );
  }

  return sanitizedBlocks.join( QString() );
}

QString QgsWordPressNewsFeedMapper::mapImageUrl( const QVariantMap &post )
{
  const QVariantMap embedded = post.value( QStringLiteral( "_embedded" ) ).toMap();
  const QVariantList mediaList = embedded.value( QStringLiteral( "wp:featuredmedia" ) )
                                   .toList();
  if ( mediaList.isEmpty() )
    return QString();

  const QVariantMap media = mediaList.at( 0 ).toMap();
  if ( media.value( QStringLiteral( "media_type" ) ).toString()
       != QLatin1String( "image" ) )
    return QString();

  const QString sourceUrl = media.value( QStringLiteral( "source_url" ) ).toString();
  const QVariantMap sizes = media.value( QStringLiteral( "media_details" ) )
                              .toMap()
                              .value( QStringLiteral( "sizes" ) )
                              .toMap();
  if ( sizes.isEmpty() )
    return sourceUrl;

  const QVariantMap mediumLarge = sizes.value( QStringLiteral( "medium_large" ) ).toMap();
  const QString mediumLargeUrl = mediumLarge.value( QStringLiteral( "source_url" ) )
                                   .toString();
  const int mediumLargeWidth = mediumLarge.value( QStringLiteral( "width" ) ).toInt();
  if ( !mediumLargeUrl.isEmpty() && mediumLargeWidth >= 480 )
    return mediumLargeUrl;

  std::vector< ImageCandidate > candidates;
  candidates.reserve( static_cast< size_t >( sizes.size() ) );
  for ( auto it = sizes.constBegin(); it != sizes.constEnd(); ++it )
  {
    const QVariantMap sizeMap = it.value().toMap();
    const QString url = sizeMap.value( QStringLiteral( "source_url" ) ).toString();
    const int width = sizeMap.value( QStringLiteral( "width" ) ).toInt();
    if ( url.isEmpty() || width <= 0 )
      continue;

    candidates.push_back( ImageCandidate {url, width} );
  }

  if ( candidates.empty() )
    return sourceUrl;

  QString bestWideUrl;
  int bestWideWidth = std::numeric_limits< int >::max();
  QString bestNarrowUrl;
  int bestNarrowWidth = -1;

  for ( const ImageCandidate &candidate : candidates )
  {
    if ( candidate.width >= 768 && candidate.width < bestWideWidth )
    {
      bestWideWidth = candidate.width;
      bestWideUrl = candidate.url;
    }

    if ( candidate.width < 768 && candidate.width > bestNarrowWidth )
    {
      bestNarrowWidth = candidate.width;
      bestNarrowUrl = candidate.url;
    }
  }

  if ( !bestWideUrl.isEmpty() )
    return bestWideUrl;

  if ( !bestNarrowUrl.isEmpty() )
    return bestNarrowUrl;

  return sourceUrl;
}

qlonglong QgsWordPressNewsFeedMapper::mapPublishFrom( const QVariantMap &post )
{
  const QString dateString = post.value( QStringLiteral( "date_gmt" ) ).toString();
  if ( dateString.isEmpty() )
    return 0;

  QDateTime publishFrom = QDateTime::fromString( dateString, Qt::ISODate );
  if ( !publishFrom.isValid() )
    return 0;

  if ( publishFrom.timeSpec() == Qt::LocalTime )
    publishFrom.setTimeSpec( Qt::UTC );

  return publishFrom.toSecsSinceEpoch();
}

QString QgsNextGISNewsFeedMapper::mapUrl( const QVariantMap &post ) const
{
  QUrl url( post.value( QStringLiteral( "link" ) ).toString() );
  if ( !url.isValid() )
    return post.value( QStringLiteral( "link" ) ).toString();

  QUrlQuery query( url );
  QUrlQuery utmQuery;
  utmQuery.setQuery( QgsNgUtils::utmTags( QStringLiteral( "news_feed" ) ) );
  const auto utmItems = utmQuery.queryItems();
  for ( const auto &item : utmItems )
  {
    query.removeAllQueryItems( item.first );
    query.addQueryItem( item.first, item.second );
  }
  url.setQuery( query );
  return url.toString();
}

/***************************************************************************
    testqgsnewsfeedparser.cpp
     --------------------------------------
    Date                 : July 2019
    Copyright            : (C) 2019 by Nyall Dawson
    Email                : nyall dot dawson at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


#include "qgstest.h"
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImage>
#include "qgsngutils.h"
#include "qgsnewsfeedparser.h"
#include "qgsnewsfeedmodel.h"


class TestQgsNewsFeedParser : public QObject
{
    Q_OBJECT
  public:
    TestQgsNewsFeedParser() = default;

  private slots:
    void initTestCase();    // will be called before the first testfunction is executed.
    void cleanupTestCase(); // will be called after the last testfunction was executed.
    void init();            // will be called before each testfunction is executed.
    void cleanup();         // will be called after every testfunction.

    void testFetch();
    void testAutoExpiry();
    void testLang();
    void testGeoFencing();
    void testModel();
    void testProxyModel();
    void testUpdatedEntries();
    void testWordPressMapping();
    void testNgUtils();
    void testPersistedPreviewImage();
    void testPersistedPreviewImageSurvivesRefetch();
};


void TestQgsNewsFeedParser::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();
  QCoreApplication::setOrganizationName( QStringLiteral( "QGIS" ) );
  QCoreApplication::setOrganizationDomain( QStringLiteral( "qgis.org" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "QGIS-TEST" ) );
}

void TestQgsNewsFeedParser::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsNewsFeedParser::init()
{
}


void TestQgsNewsFeedParser::cleanup()
{
}

void TestQgsNewsFeedParser::testFetch()
{
  QList<QgsNewsFeedParser::Entry> entries;

  const QUrl url( QUrl::fromLocalFile( QStringLiteral( TEST_DATA_DIR ) + "/newsfeed/feed" ) );
  const QString feedKey = QgsNewsFeedParser::keyForFeed( url.toString() );
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );

  const qint64 beforeTime = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();

  QgsNewsFeedParser parser( url );
  const QSignalSpy spy( &parser, &QgsNewsFeedParser::entryAdded );
  QVERIFY( parser.entries().isEmpty() );
  QEventLoop loop;
  connect( &parser, &QgsNewsFeedParser::fetched, this, [=, &loop, &entries]( const QList<QgsNewsFeedParser::Entry> &e ) {
    entries = e;
    loop.quit();
  } );
  parser.fetch();
  loop.exec();

  // check result
  QCOMPARE( spy.count(), 5 );
  QCOMPARE( entries.count(), 5 );
  QCOMPARE( entries.at( 0 ).title, QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QCOMPARE( entries.at( 1 ).title, QStringLiteral( "QGIS core will be rewritten in Rust" ) );
  QCOMPARE( entries.at( 2 ).title, QStringLiteral( "QGIS Italian Meeting" ) );
  QCOMPARE( entries.at( 3 ).title, QStringLiteral( "QGIS acquired by ESRI" ) );
  QCOMPARE( entries.at( 4 ).title, QStringLiteral( "Null Island QGIS Meeting" ) );

  QCOMPARE( parser.entries().count(), 5 );
  QCOMPARE( parser.entries().at( 0 ).title, QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QVERIFY( parser.entries().at( 0 ).expiry.isValid() );
  QCOMPARE( parser.entries().at( 0 ).expiry.toUTC(), QDateTime( QDate( 2027, 1, 1 ), QTime( 0, 0, 0 ), Qt::UTC ) );
  QCOMPARE( parser.entries().at( 1 ).title, QStringLiteral( "QGIS core will be rewritten in Rust" ) );
  QVERIFY( !parser.entries().at( 1 ).expiry.isValid() );
  QCOMPARE( parser.entries().at( 2 ).title, QStringLiteral( "QGIS Italian Meeting" ) );
  QCOMPARE( parser.entries().at( 3 ).title, QStringLiteral( "QGIS acquired by ESRI" ) );
  QCOMPARE( parser.entries().at( 4 ).title, QStringLiteral( "Null Island QGIS Meeting" ) );

  entries.clear();

  // after a fetch, the current timestamp should be saved
  const uint after = QgsNewsFeedParser::settingsFeedLastFetchTime->value( feedKey );
  QVERIFY( after >= beforeTime );

  // reset to a standard known last time
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );
  QgsNewsFeedParser::settingsFeedLastFetchTime->setValue( 1457360008, feedKey );

  // refetch, only new items should be fetched
  QgsNewsFeedParser parser2( url );
  QVERIFY( parser2.entries().isEmpty() );
  QEventLoop loop2;
  connect( &parser2, &QgsNewsFeedParser::fetched, this, [=, &loop2, &entries]( const QList<QgsNewsFeedParser::Entry> &e ) {
    entries = e;
    loop2.quit();
  } );

  parser2.fetch();
  loop2.exec();

  // check only new entries are present
  QCOMPARE( entries.count(), 4 );
  QCOMPARE( entries.at( 0 ).title, QStringLiteral( "QGIS acquired by ESRI" ) );
  QVERIFY( parser.entries().at( 0 ).expiry.isValid() );
  QCOMPARE( parser.entries().at( 0 ).expiry.toUTC(), QDateTime( QDate( 2027, 1, 1 ), QTime( 0, 0, 0 ), Qt::UTC ) );
  QCOMPARE( entries.at( 1 ).title, QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QVERIFY( !parser.entries().at( 1 ).expiry.isValid() );
  QCOMPARE( entries.at( 2 ).title, QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( entries.at( 3 ).title, QStringLiteral( "QGIS Italian Meeting" ) );

  QCOMPARE( parser2.entries().count(), 4 );
  QCOMPARE( parser2.entries().at( 0 ).title, QStringLiteral( "QGIS acquired by ESRI" ) );
  QCOMPARE( parser2.entries().at( 1 ).title, QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QCOMPARE( parser2.entries().at( 2 ).title, QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( parser2.entries().at( 3 ).title, QStringLiteral( "QGIS Italian Meeting" ) );

  entries.clear();

  // make a new parser with existing stored entries
  QgsNewsFeedParser parser3( url );
  // previous entries should be automatically read
  QCOMPARE( parser3.entries().count(), 4 );
  QCOMPARE( parser3.entries().at( 0 ).title, QStringLiteral( "QGIS acquired by ESRI" ) );
  QVERIFY( parser.entries().at( 0 ).expiry.isValid() );
  QCOMPARE( parser.entries().at( 0 ).expiry.toUTC(), QDateTime( QDate( 2027, 1, 1 ), QTime( 0, 0, 0 ), Qt::UTC ) );
  QCOMPARE( parser3.entries().at( 1 ).title, QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QVERIFY( !parser.entries().at( 1 ).expiry.isValid() );
  QCOMPARE( parser3.entries().at( 2 ).title, QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( parser3.entries().at( 3 ).title, QStringLiteral( "QGIS Italian Meeting" ) );

  // dismiss imaginary entry
  const QSignalSpy dismissSpy( &parser3, &QgsNewsFeedParser::entryDismissed );
  parser3.dismissEntry( -1 );
  QCOMPARE( dismissSpy.count(), 0 );
  QCOMPARE( parser3.entries().count(), 4 );

  // dismiss valid entry
  parser3.dismissEntry( 4 );
  QCOMPARE( dismissSpy.count(), 1 );
  QCOMPARE( parser3.entries().count(), 3 );
  QCOMPARE( parser3.entries().at( 0 ).title, QStringLiteral( "QGIS acquired by ESRI" ) );
  QCOMPARE( parser3.entries().at( 1 ).title, QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( parser3.entries().at( 2 ).title, QStringLiteral( "QGIS Italian Meeting" ) );

  // craft a new parser, should not have dismissed entry
  QgsNewsFeedParser parser4( url );
  QCOMPARE( parser4.entries().count(), 3 );
  QCOMPARE( parser4.entries().at( 0 ).title, QStringLiteral( "QGIS acquired by ESRI" ) );
  QCOMPARE( parser4.entries().at( 1 ).title, QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( parser4.entries().at( 2 ).title, QStringLiteral( "QGIS Italian Meeting" ) );

  // even if we re-fetch the full feed, the dismissed entry should not come back
  QgsNewsFeedParser::settingsFeedLastFetchTime->setValue( 0, feedKey );
  QEventLoop loop4;
  connect( &parser4, &QgsNewsFeedParser::fetched, this, [=, &loop4, &entries]( const QList<QgsNewsFeedParser::Entry> &e ) {
    entries = e;
    loop4.quit();
  } );
  parser4.fetch();
  loop4.exec();
  QCOMPARE( entries.count(), 5 );
  QCOMPARE( parser4.entries().count(), 4 );

  QStringList titles;
  for ( const QgsNewsFeedParser::Entry &entry : parser4.entries() )
    titles << entry.title;

  QVERIFY( !titles.contains( QStringLiteral( "Next Microsoft Windows code name revealed" ) ) );
  QVERIFY( titles.contains( QStringLiteral( "QGIS core will be rewritten in Rust" ) ) );
  QVERIFY( titles.contains( QStringLiteral( "QGIS acquired by ESRI" ) ) );
  QVERIFY( titles.contains( QStringLiteral( "Null Island QGIS Meeting" ) ) );
  QVERIFY( titles.contains( QStringLiteral( "QGIS Italian Meeting" ) ) );

  // dismiss all
  parser4.dismissAll();
  QCOMPARE( parser4.entries().count(), 0 );

  const QgsNewsFeedParser parser5( url );
  QCOMPARE( parser5.entries().count(), 0 );
}

void TestQgsNewsFeedParser::testAutoExpiry()
{
  const QUrl url( QStringLiteral( "xxx" ) );
  const QString feedKey = QgsNewsFeedParser::keyForFeed( url.toString() );
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );

  // ensure entries "auto expire" when past their use-by date
  QgsNewsFeedParser::Entry testEntry;
  testEntry.key = 1;
  testEntry.title = QStringLiteral( "test entry" );
  QgsNewsFeedParser::Entry testEntry2;
  testEntry2.key = 2;
  testEntry2.title = QStringLiteral( "test entry2" );
  testEntry2.expiry = QDateTime( QDate( 1997, 1, 1 ), QTime( 0, 0, 0 ), Qt::UTC );

  QgsNewsFeedParser parser( url );
  parser.storeEntryInSettings( testEntry );
  parser.storeEntryInSettings( testEntry2 );

  // on relaunch, expired entries should be auto-pruned
  const QgsNewsFeedParser parser2( url );
  QCOMPARE( parser2.entries().count(), 1 );
  QCOMPARE( parser2.entries().at( 0 ).title, QStringLiteral( "test entry" ) );
  QVERIFY( !parser2.entries().at( 0 ).expiry.isValid() );
}

void TestQgsNewsFeedParser::testLang()
{
  QList<QgsNewsFeedParser::Entry> entries;

  const QUrl url( QUrl::fromLocalFile( QStringLiteral( TEST_DATA_DIR ) + "/newsfeed/feed" ) );
  const QString feedKey = QgsNewsFeedParser::keyForFeed( url.toString() );
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );
  // force to Spanish language
  QgsNewsFeedParser::settingsFeedLanguage->setValue( QStringLiteral( "es" ), feedKey );

  QgsNewsFeedParser parser( url );
  const QSignalSpy spy( &parser, &QgsNewsFeedParser::entryAdded );
  QVERIFY( parser.entries().isEmpty() );
  QEventLoop loop;
  connect( &parser, &QgsNewsFeedParser::fetched, this, [=, &loop, &entries]( const QList<QgsNewsFeedParser::Entry> &e ) {
    entries = e;
    loop.quit();
  } );

  parser.fetch();
  loop.exec();

  QCOMPARE( entries.count(), 1 );
  QCOMPARE( entries.at( 0 ).title, QStringLiteral( "Primer seminario SIG libre" ) );
}

void TestQgsNewsFeedParser::testGeoFencing()
{
  QList<QgsNewsFeedParser::Entry> entries;

  const QUrl url( QUrl::fromLocalFile( QStringLiteral( TEST_DATA_DIR ) + "/newsfeed/feed" ) );
  const QString feedKey = QgsNewsFeedParser::keyForFeed( url.toString() );
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );
  QgsNewsFeedParser::settingsFeedLatitude->setValue( 37.2343, feedKey );
  QgsNewsFeedParser::settingsFeedLongitude->setValue( -115.8067, feedKey );

  QgsNewsFeedParser parser( url );
  const QSignalSpy spy( &parser, &QgsNewsFeedParser::entryAdded );
  QVERIFY( parser.entries().isEmpty() );
  QEventLoop loop;
  connect( &parser, &QgsNewsFeedParser::fetched, this, [=, &loop, &entries]( const QList<QgsNewsFeedParser::Entry> &e ) {
    entries = e;
    loop.quit();
  } );

  parser.fetch();
  loop.exec();

  // check only geofenced entries are present (i.e. that request has included lat/lon params)
  QCOMPARE( entries.count(), 1 );
  QCOMPARE( entries.at( 0 ).title, QStringLiteral( "Secret docs leaked" ) );
}

void TestQgsNewsFeedParser::testModel()
{
  // test news feed model
  const QUrl url( QUrl::fromLocalFile( QStringLiteral( TEST_DATA_DIR ) + "/newsfeed/feed" ) );
  const QString feedKey = QgsNewsFeedParser::keyForFeed( url.toString() );
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );

  QgsNewsFeedParser parser( url );
  const QgsNewsFeedModel model( &parser );
  QCOMPARE( model.rowCount(), 0 );

  QEventLoop loop;
  connect( &parser, &QgsNewsFeedParser::fetched, this, [=, &loop]( const QList<QgsNewsFeedParser::Entry> & ) {
    loop.quit();
  } );
  parser.fetch();
  loop.exec();

  QCOMPARE( model.rowCount(), 5 );
  QVERIFY( model.data( model.index( 0, 0, QModelIndex() ), Qt::DisplayRole ).toString().startsWith( QLatin1String( "<p>Rumors from a whistleblower revealed the next Windows release code nam" ) ) );
  QVERIFY( model.data( model.index( 1, 0, QModelIndex() ), Qt::DisplayRole ).toString().startsWith( QLatin1String( "<p>Tired with C++ intricacies, the core developers h" ) ) );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), Qt::DisplayRole ).toString(), QStringLiteral( "<p>Ciao from Italy!</p>" ) );
  QVERIFY( model.data( model.index( 3, 0, QModelIndex() ), Qt::DisplayRole ).toString().startsWith( QLatin1String( "<p>QGIS is finally part of the ESRI ecosystem, i" ) ) );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), Qt::DisplayRole ).toString(), QStringLiteral( "<p>Let's dive in the ocean together!</p>" ) );
  QVERIFY( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Content ) ).toString().startsWith( QLatin1String( "<p>Rumors from a whistleblower revealed the next Windows release code nam" ) ) );
  QVERIFY( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Content ) ).toString().startsWith( QLatin1String( "<p>Tired with C++ intricacies, the core developers h" ) ) );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Content ) ).toString(), QStringLiteral( "<p>Ciao from Italy!</p>" ) );
  QVERIFY( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Content ) ).toString().startsWith( QLatin1String( "<p>QGIS is finally part of the ESRI ecosystem, i" ) ) );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Content ) ).toString(), QStringLiteral( "<p>Let's dive in the ocean together!</p>" ) );
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), Qt::ToolTipRole ).toString(), QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), Qt::ToolTipRole ).toString(), QStringLiteral( "QGIS core will be rewritten in Rust" ) );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), Qt::ToolTipRole ).toString(), QStringLiteral( "QGIS Italian Meeting" ) );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), Qt::ToolTipRole ).toString(), QStringLiteral( "QGIS acquired by ESRI" ) );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), Qt::ToolTipRole ).toString(), QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS core will be rewritten in Rust" ) );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS Italian Meeting" ) );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS acquired by ESRI" ) );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Key ) ).toInt(), 4 );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Key ) ).toInt(), 6 );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Key ) ).toInt(), 11 );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Key ) ).toInt(), 3 );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Key ) ).toInt(), 5 );
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::ImageUrl ) ).toString(), QString() );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::ImageUrl ) ).toString(), QStringLiteral( "http://0.0.0.0:8000/media/feedimages/rust.png" ) );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::ImageUrl ) ).toString(), QString() );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::ImageUrl ) ).toString(), QString() );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::ImageUrl ) ).toString(), QString() );
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Link ) ).toString(), QStringLiteral( "https://www.winux.microsoft.com" ) );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Link ) ).toString(), QString() );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Link ) ).toString(), QString() );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Link ) ).toString(), QStringLiteral( "https://www.qgis.com" ) );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Link ) ).toString(), QString() );
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Sticky ) ).toBool(), true );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Sticky ) ).toBool(), true );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Sticky ) ).toBool(), false );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Sticky ) ).toBool(), false );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Sticky ) ).toBool(), false );

  // remove an entry
  parser.dismissEntry( 11 );
  QCOMPARE( model.rowCount(), 4 );
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS core will be rewritten in Rust" ) );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS acquired by ESRI" ) );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Null Island QGIS Meeting" ) );

  // construct a new model/parser -- should initially have stored entries
  QgsNewsFeedParser parser2( url );
  const QgsNewsFeedModel model2( &parser2 );
  QCOMPARE( model2.rowCount(), 4 );
  QCOMPARE( model2.data( model2.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS acquired by ESRI" ) );
  QCOMPARE( model2.data( model2.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QCOMPARE( model2.data( model2.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( model2.data( model2.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS core will be rewritten in Rust" ) );
}

void TestQgsNewsFeedParser::testProxyModel()
{
  // test news feed proxy model
  const QUrl url( QUrl::fromLocalFile( QStringLiteral( TEST_DATA_DIR ) + "/newsfeed/feed" ) );
  const QString feedKey = QgsNewsFeedParser::keyForFeed( url.toString() );
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );

  QgsNewsFeedParser parser( url );
  const QgsNewsFeedProxyModel model( &parser );
  QCOMPARE( model.rowCount(), 0 );

  QEventLoop loop;
  connect( &parser, &QgsNewsFeedParser::fetched, this, [=, &loop]( const QList<QgsNewsFeedParser::Entry> & ) {
    loop.quit();
  } );
  parser.fetch();
  loop.exec();

  QCOMPARE( model.rowCount(), 5 );
  // stickies first, then sort by key descending (i.e. more recently published entries first)
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Sticky ) ).toBool(), true );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Sticky ) ).toBool(), true );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Sticky ) ).toBool(), false );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Sticky ) ).toBool(), false );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Sticky ) ).toBool(), false );
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Key ) ).toInt(), 6 );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Key ) ).toInt(), 4 );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Key ) ).toInt(), 11 );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Key ) ).toInt(), 5 );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Key ) ).toInt(), 3 );
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS core will be rewritten in Rust" ) );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS Italian Meeting" ) );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( model.data( model.index( 4, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS acquired by ESRI" ) );

  // remove an entry
  parser.dismissEntry( 11 );
  QCOMPARE( model.rowCount(), 4 );
  QCOMPARE( model.data( model.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS core will be rewritten in Rust" ) );
  QCOMPARE( model.data( model.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QCOMPARE( model.data( model.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( model.data( model.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS acquired by ESRI" ) );

  // construct a new model/parser -- should initially have stored entries
  QgsNewsFeedParser parser2( url );
  const QgsNewsFeedProxyModel model2( &parser2 );
  QCOMPARE( model2.rowCount(), 4 );
  QCOMPARE( model2.data( model2.index( 0, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS core will be rewritten in Rust" ) );
  QCOMPARE( model2.data( model2.index( 1, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QCOMPARE( model2.data( model2.index( 2, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( model2.data( model2.index( 3, 0, QModelIndex() ), static_cast<int>( QgsNewsFeedModel::CustomRole::Title ) ).toString(), QStringLiteral( "QGIS acquired by ESRI" ) );
}

void TestQgsNewsFeedParser::testUpdatedEntries()
{
  QList<QgsNewsFeedParser::Entry> entries;

  const QUrl url( QUrl::fromLocalFile( QStringLiteral( TEST_DATA_DIR ) + "/newsfeed/feed" ) );
  const QString feedKey = QgsNewsFeedParser::keyForFeed( url.toString() );

  // reset to a standard known last time
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );
  QgsNewsFeedParser::settingsFeedLastFetchTime->setValue( 1457360008, feedKey );

  // refetch, only new items should be fetched
  QgsNewsFeedParser parser( url );
  QVERIFY( parser.entries().isEmpty() );
  QEventLoop loop;
  connect( &parser, &QgsNewsFeedParser::fetched, this, [=, &loop, &entries]( const QList<QgsNewsFeedParser::Entry> &e ) {
    entries = e;
    loop.quit();
  } );

  parser.fetch();
  loop.exec();

  // check only new entries are present
  QCOMPARE( entries.count(), 4 );

  entries.clear();

  // Now request a new date and check that:
  // - entry 3 has a new expiry date in the past so it is removed
  // - entry 11 has been modified

  QgsNewsFeedParser::settingsFeedLastFetchTime->setValue( 1557079653, feedKey );

  QgsNewsFeedParser parser2( url );
  QVERIFY( !parser2.entries().isEmpty() );
  QEventLoop loop2;
  connect( &parser2, &QgsNewsFeedParser::fetched, this, [=, &loop2, &entries]( const QList<QgsNewsFeedParser::Entry> &e ) {
    entries = e;
    loop2.quit();
  } );

  const QSignalSpy spyUpdated( &parser2, &QgsNewsFeedParser::entryUpdated );
  const QSignalSpy spyDismissed( &parser2, &QgsNewsFeedParser::entryDismissed );

  parser2.fetch();
  loop2.exec();

  QCOMPARE( spyDismissed.count(), 1 );
  QCOMPARE( spyUpdated.count(), 1 );

  QCOMPARE( entries.count(), 2 );
  QCOMPARE( parser2.entries().count(), 3 );
  QCOMPARE( parser2.entries().at( 0 ).title, QStringLiteral( "Next Microsoft Windows code name revealed" ) );
  QCOMPARE( parser2.entries().at( 1 ).title, QStringLiteral( "Null Island QGIS Meeting" ) );
  QCOMPARE( parser2.entries().at( 2 ).title, QStringLiteral( "QGIS Italian Meeting Revisited" ) );
  QCOMPARE( parser2.entries().at( 2 ).expiry.toSecsSinceEpoch(), 7868426853 );
}

void TestQgsNewsFeedParser::testWordPressMapping()
{
  QList<QgsNewsFeedParser::Entry> entries;
  const QString originalLocale = QgsApplication::settingsLocaleUserLocale->value();
  const bool originalOverride = QgsApplication::settingsLocaleOverrideFlag->value();
  QgsApplication::settingsLocaleOverrideFlag->setValue( true );
  QgsApplication::settingsLocaleUserLocale->setValue( QStringLiteral( "en" ) );

  const QUrl url( QUrl::fromLocalFile( QStringLiteral( TEST_DATA_DIR ) + "/newsfeed/wordpress_feed" ) );
  const QString feedKey = QgsNewsFeedParser::keyForFeed( url.toString() );
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );

  QgsNewsFeedParser parser( url );
  QEventLoop loop;
  connect( &parser, &QgsNewsFeedParser::fetched, this, [=, &loop, &entries]( const QList<QgsNewsFeedParser::Entry> &e ) {
    entries = e;
    loop.quit();
  } );

  parser.fetch();
  loop.exec();

  QCOMPARE( entries.count(), 4 );
  QCOMPARE( parser.entries().count(), 4 );

  QCOMPARE( parser.entries().at( 0 ).key, 101 );
  QCOMPARE( parser.entries().at( 0 ).title, QStringLiteral( "First & News" ) );
  QCOMPARE( parser.entries().at( 0 ).link, QUrl( QStringLiteral( "https://nextgis.com/news/first-post/?utm_source=nextgisqgis&utm_medium=news_feed&utm_campaign=constant&utm_content=en" ) ) );
  QCOMPARE( parser.entries().at( 0 ).imageUrl, QStringLiteral( "file:///tmp/medium-large.jpg" ) );
  QCOMPARE( parser.entries().at( 0 ).sticky, true );
  QVERIFY( parser.entries().at( 0 ).content.contains( QStringLiteral( "<strong>world</strong>" ) ) );
  QVERIFY( parser.entries().at( 0 ).content.contains( QStringLiteral( "<a href=\"https://nextgis.com/news/first-post/\">more</a>" ) ) );
  QVERIFY( !parser.entries().at( 0 ).content.contains( QStringLiteral( "<script" ) ) );

  QCOMPARE( parser.entries().at( 1 ).link, QUrl( QStringLiteral( "https://nextgis.com/news/second-post/?utm_source=nextgisqgis&utm_medium=news_feed&utm_campaign=constant&utm_content=en" ) ) );
  QCOMPARE( parser.entries().at( 1 ).imageUrl, QStringLiteral( "file:///tmp/large-900.jpeg" ) );
  QVERIFY( parser.entries().at( 1 ).content.contains( QStringLiteral( "<br>" ) ) );
  QVERIFY( parser.entries().at( 1 ).content.contains( QStringLiteral( "<em>italic</em>" ) ) );
  QVERIFY( !parser.entries().at( 1 ).content.contains( QStringLiteral( "<span" ) ) );

  QCOMPARE( parser.entries().at( 2 ).imageUrl, QStringLiteral( "file:///tmp/fallback-image.webp" ) );
  QCOMPARE( parser.entries().at( 3 ).imageUrl, QString() );

  QgsApplication::settingsLocaleUserLocale->setValue( originalLocale );
  QgsApplication::settingsLocaleOverrideFlag->setValue( originalOverride );
}

void TestQgsNewsFeedParser::testNgUtils()
{
  const QString originalLocale = QgsApplication::settingsLocaleUserLocale->value();
  const bool originalOverride = QgsApplication::settingsLocaleOverrideFlag->value();

  QgsApplication::settingsLocaleOverrideFlag->setValue( true );
  QgsApplication::settingsLocaleUserLocale->setValue( QStringLiteral( "ru_RU" ) );
  QCOMPARE( QgsNgUtils::locale(), QStringLiteral( "ru" ) );
  QCOMPARE( QgsNgUtils::nextgisDomain(), QStringLiteral( "https://nextgis.ru" ) );
  QCOMPARE( QgsNgUtils::nextgisDomain( QStringLiteral( "docs" ) ), QStringLiteral( "https://docs.nextgis.ru" ) );
  QCOMPARE( QgsNgUtils::utmTags( QStringLiteral( "news_feed" ) ), QStringLiteral( "utm_source=nextgisqgis&utm_medium=news_feed&utm_campaign=constant&utm_content=ru" ) );

  QgsApplication::settingsLocaleUserLocale->setValue( QStringLiteral( "C" ) );
  QCOMPARE( QgsNgUtils::locale(), QStringLiteral( "en" ) );
  QCOMPARE( QgsNgUtils::nextgisDomain(), QStringLiteral( "https://nextgis.com" ) );

  QgsApplication::settingsLocaleUserLocale->setValue( originalLocale );
  QgsApplication::settingsLocaleOverrideFlag->setValue( originalOverride );
}

void TestQgsNewsFeedParser::testPersistedPreviewImage()
{
  const QUrl url( QStringLiteral( "persisted-image-test" ) );
  const QString feedKey = QgsNewsFeedParser::keyForFeed( url.toString() );
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );

  QgsNewsFeedParser parser( url );
  QgsNewsFeedParser::Entry entry;
  entry.key = 12345;
  entry.title = QStringLiteral( "entry with cached image" );
  entry.imageUrl = QStringLiteral( "https://nextgis.com/image.png" );
  parser.storeEntryInSettings( entry );

  const QString cachedPath = parser.cachedImagePath( entry.key );
  QDir().mkpath( QFileInfo( cachedPath ).path() );

  QImage previewImage( 24, 12, QImage::Format_ARGB32 );
  previewImage.fill( QColor( 23, 111, 193 ) );
  QVERIFY( previewImage.save( cachedPath ) );

  const QgsNewsFeedParser parser2( url );
  QCOMPARE( parser2.entries().count(), 1 );
  QVERIFY( !parser2.entries().at( 0 ).image.isNull() );

  const QgsNewsFeedModel model( const_cast< QgsNewsFeedParser * >( &parser2 ) );
  QVERIFY( !qvariant_cast< QPixmap >( model.data( model.index( 0, 0, QModelIndex() ), Qt::DecorationRole ) ).isNull() );

  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );
  QFile::remove( cachedPath );
}

void TestQgsNewsFeedParser::testPersistedPreviewImageSurvivesRefetch()
{
  const QUrl url( QUrl::fromLocalFile( QStringLiteral( TEST_DATA_DIR ) + "/newsfeed/feed" ) );
  const QString feedKey = QgsNewsFeedParser::keyForFeed( url.toString() );
  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );

  QgsNewsFeedParser parser( url );
  QgsNewsFeedParser::Entry entry;
  entry.key = 6;
  entry.title = QStringLiteral( "QGIS core will be rewritten in Rust" );
  entry.imageUrl = QStringLiteral( "http://0.0.0.0:8000/media/feedimages/rust.png" );
  parser.storeEntryInSettings( entry );

  const QString cachedPath = parser.cachedImagePath( entry.key );
  QDir().mkpath( QFileInfo( cachedPath ).path() );

  QImage previewImage( 24, 12, QImage::Format_ARGB32 );
  previewImage.fill( QColor( 23, 111, 193 ) );
  QVERIFY( previewImage.save( cachedPath ) );

  const QgsNewsFeedParser parser2( url );
  QVERIFY( parser2.entries().count() == 1 );
  QVERIFY( !parser2.entries().at( 0 ).image.isNull() );

  QgsNewsFeedParser parser3( url );
  QEventLoop loop2;
  connect( &parser3, &QgsNewsFeedParser::fetched, this, [=, &loop2]( const QList<QgsNewsFeedParser::Entry> & ) {
    loop2.quit();
  } );
  QVERIFY( parser3.entries().count() == 1 );
  QVERIFY( !parser3.entries().at( 0 ).image.isNull() );

  parser3.fetch();
  loop2.exec();

  const auto entryIter = std::find_if( parser3.entries().begin(), parser3.entries().end(), []( const QgsNewsFeedParser::Entry &candidate )
  {
    return candidate.key == 6;
  } );
  QVERIFY( entryIter != parser3.entries().end() );
  QVERIFY( !entryIter->image.isNull() );

  QgsNewsFeedParser::sTreeNewsFeed->deleteItem( feedKey );
  QFile::remove( cachedPath );
}


QGSTEST_MAIN( TestQgsNewsFeedParser )
#include "testqgsnewsfeedparser.moc"

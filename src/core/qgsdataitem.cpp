/***************************************************************************
               qgsdataitem.cpp  - Data items
                             -------------------
    begin                : 2011-04-01
    copyright            : (C) 2011 Radim Blazek
    email                : radim dot blazek at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QApplication>
#include <QtConcurrentMap>
#include <QtConcurrentRun>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVector>
#include <QStyle>
#include <QSettings>

#include "qgis.h"
#include "qgsdataitem.h"

#include "qgsdataprovider.h"
#include "qgslogger.h"
#include "qgsproviderregistry.h"
#include "qgsconfig.h"

// use GDAL VSI mechanism
#include "cpl_vsi.h"
#include "cpl_string.h"

// shared icons
const QIcon &QgsLayerItem::iconPoint()
{
  static QIcon icon;

  if ( icon.isNull() )
    icon = QgsApplication::getThemeIcon( "/mIconPointLayer.svg" );

  return icon;
}

const QIcon &QgsLayerItem::iconLine()
{
  static QIcon icon;

  if ( icon.isNull() )
    icon = QgsApplication::getThemeIcon( "/mIconLineLayer.svg" );

  return icon;
}

const QIcon &QgsLayerItem::iconPolygon()
{
  static QIcon icon;

  if ( icon.isNull() )
    icon = QgsApplication::getThemeIcon( "/mIconPolygonLayer.svg" );

  return icon;
}

const QIcon &QgsLayerItem::iconTable()
{
  static QIcon icon;

  if ( icon.isNull() )
    icon = QgsApplication::getThemeIcon( "/mIconTableLayer.png" );

  return icon;
}

const QIcon &QgsLayerItem::iconRaster()
{
  static QIcon icon;

  if ( icon.isNull() )
    icon = QgsApplication::getThemeIcon( "/mIconRaster.svg" );

  return icon;
}

const QIcon &QgsLayerItem::iconDefault()
{
  static QIcon icon;

  if ( icon.isNull() )
    icon = QgsApplication::getThemeIcon( "/mIconLayer.png" );

  return icon;
}

const QIcon &QgsDataCollectionItem::iconDataCollection()
{
  static QIcon icon;

  if ( icon.isNull() )
    icon = QgsApplication::getThemeIcon( "/mIconDbSchema.png" );

  return icon;
}

const QIcon &QgsDataCollectionItem::iconDir()
{
  static QIcon icon;

  if ( icon.isNull() )
  {
    // initialize shared icons
    QStyle *style = QApplication::style();
    icon = QIcon( style->standardPixmap( QStyle::SP_DirClosedIcon ) );
    icon.addPixmap( style->standardPixmap( QStyle::SP_DirOpenIcon ),
                    QIcon::Normal, QIcon::On );
  }

  return icon;
}

const QIcon &QgsFavouritesItem::iconFavourites()
{
  static QIcon icon;

  if ( icon.isNull() )
    icon = QgsApplication::getThemeIcon( "/mIconFavourites.png" );

  return icon;
}

const QIcon &QgsZipItem::iconZip()
{
  static QIcon icon;

  if ( icon.isNull() )
    icon = QgsApplication::getThemeIcon( "/mIconZip.png" );
// icon from http://www.softicons.com/free-icons/application-icons/mega-pack-icons-1-by-nikolay-verin/winzip-folder-icon

  return icon;
}

QMap<QString, QIcon> QgsDataItem::mIconMap = QMap<QString, QIcon>();

int QgsDataItem::mLoadingCount = 0;
QMovie * QgsDataItem::mLoadingMovie = 0;
QIcon QgsDataItem::mLoadingIcon = QIcon();

QgsDataItem::QgsDataItem( QgsDataItem::Type type, QgsDataItem* parent, QString name, QString path )
// Do not pass parent to QObject, Qt would delete this when parent is deleted
    : QObject()
    , mType( type )
    , mCapabilities( NoCapabilities )
    , mParent( parent )
    , mState( NotPopulated )
    , mPopulated( false )
    , mName( name )
    , mPath( path )
    , mWatcher( 0 )
{
}

QgsDataItem::~QgsDataItem()
{
  QgsDebugMsgLevel( "mName = " + mName + " mPath = " + mPath, 2 );
}

QIcon QgsDataItem::icon()
{
  if ( state() == Populating )
    return mLoadingIcon;

  if ( !mIcon.isNull() )
    return mIcon;

  if ( !mIconMap.contains( mIconName ) )
    mIconMap.insert( mIconName, QgsApplication::getThemeIcon( mIconName ) );

  return mIconMap.value( mIconName );
}

void QgsDataItem::emitBeginInsertItems( QgsDataItem* parent, int first, int last )
{
  emit beginInsertItems( parent, first, last );
}
void QgsDataItem::emitEndInsertItems()
{
  emit endInsertItems();
}
void QgsDataItem::emitBeginRemoveItems( QgsDataItem* parent, int first, int last )
{
  emit beginRemoveItems( parent, first, last );
}
void QgsDataItem::emitEndRemoveItems()
{
  emit endRemoveItems();
}

void QgsDataItem::emitDataChanged( QgsDataItem* item )
{
  emit dataChanged( item );
}

void QgsDataItem::emitDataChanged()
{
  emit dataChanged( this );
}

QVector<QgsDataItem*> QgsDataItem::createChildren()
{
  return QVector<QgsDataItem*>();
}

void QgsDataItem::populate()
{
  if ( state() == Populated || state() == Populating )
    return;

  QgsDebugMsg( "mPath = " + mPath );

  if ( capabilities2() & QgsDataItem::Fast )
  {
    populate( createChildren() );
  }
  else
  {
    setState( Populating );
    // The watcher must not be created with item (in constructor) because the item may be created in thread and the watcher created in thread does not work correctly.
    if ( !mWatcher )
    {
      mWatcher = new QFutureWatcher< QVector <QgsDataItem*> >( this );
    }
    connect( mWatcher, SIGNAL( finished() ), SLOT( childrenCreated() ) );
    mWatcher->setFuture( QtConcurrent::run( runCreateChildren, this ) );
  }
}

// This is expected to be run in a separate thread
QVector<QgsDataItem*> QgsDataItem::runCreateChildren( QgsDataItem* item )
{
  QgsDebugMsg( "path = " + item->path() );
  //QTime time;
  //time.start();
  QVector <QgsDataItem*> children = item->createChildren();
  //QgsDebugMsg( QString( "%1 children created in %2 ms" ).arg( children.size() ).arg( time.elapsed() ) );
  // Children objects must be pushed to main thread.
  foreach ( QgsDataItem* child, children )
  {
    if ( !child ) // should not happen
      continue;
    // The object cannot be moved if it has a parent.
    QgsDebugMsg( "moveToThread child " + child->path() );
    child->setParent( 0 );
    child->moveToThread( QApplication::instance()->thread() ); // moves also children
    child->setParent( item );
  }
  QgsDebugMsg( "finished path = " + item->path() );
  return children;
}

void QgsDataItem::childrenCreated()
{
  QgsDebugMsg( QString( "path = %1 children.size() = %2" ).arg( path() ).arg( mWatcher->result().size() ) );
  if ( mChildren.size() == 0 ) // usually populating but may also be refresh if originaly there were no children
  {
    populate( mWatcher->result() );
  }
  else // refreshing
  {
    refresh( mWatcher->result() );
  }
  disconnect( mWatcher, SIGNAL( finished() ), this, SLOT( childrenCreated() ) );
  emit dataChanged( this ); // to replace loading icon by normal icon
}

void QgsDataItem::populate( QVector<QgsDataItem*> children )
{
  QgsDebugMsg( "mPath = " + mPath );

  foreach ( QgsDataItem *child, children )
  {
    if ( !child ) // should not happen
      continue;
    // update after thread finished -> refresh
    addChildItem( child, true );
  }
  setState( Populated );
}

void QgsDataItem::depopulate()
{
  QgsDebugMsg( "mPath = " + mPath );

  foreach ( QgsDataItem *child, mChildren )
  {
    QgsDebugMsg( "remove " + child->path() );
    child->depopulate(); // recursive
    deleteChildItem( child );
  }
  setState( NotPopulated );
}

void QgsDataItem::refresh()
{
  if ( state() == Populating )
    return;

  QgsDebugMsg( "mPath = " + mPath );

  if ( capabilities2() & QgsDataItem::Fast )
  {
    refresh( createChildren() );
  }
  else
  {
    setState( Populating );
    if ( !mWatcher )
    {
      mWatcher = new QFutureWatcher< QVector <QgsDataItem*> >( this );
    }
    connect( mWatcher, SIGNAL( finished() ), SLOT( childrenCreated() ) );
    mWatcher->setFuture( QtConcurrent::run( runCreateChildren, this ) );
  }
}

void QgsDataItem::refresh( QVector<QgsDataItem*> children )
{
  QgsDebugMsgLevel( "mPath = " + mPath, 2 );

  // Remove no more present children
  QVector<QgsDataItem*> remove;
  foreach ( QgsDataItem *child, mChildren )
  {
    if ( !child ) // should not happen
      continue;
    if ( findItem( children, child ) >= 0 )
      continue;
    remove.append( child );
  }
  foreach ( QgsDataItem *child, remove )
  {
    QgsDebugMsg( "remove " + child->path() );
    deleteChildItem( child );
  }

  // Add new children
  foreach ( QgsDataItem *child, children )
  {
    if ( !child ) // should not happen
      continue;

    int index = findItem( mChildren, child );
    if ( index >= 0 )
    {
      // Refresh recursively (some providers may create more generations of descendants)
      if ( !( child->capabilities2() & QgsDataItem::Fertile ) )
      {
        // The child cannot createChildren() itself
        mChildren.value( index )->refresh( child->children() );
      }

      delete child;
      continue;
    }
    addChildItem( child, true );
  }
  setState( Populated );
}

int QgsDataItem::rowCount()
{
  return mChildren.size();
}
bool QgsDataItem::hasChildren()
{
  return ( state() == Populated ? mChildren.count() > 0 : true );
}

void QgsDataItem::addChildItem( QgsDataItem * child, bool refresh )
{
  QgsDebugMsg( QString( "path = %1 add child #%2 - %3 - %4" ).arg( mPath ).arg( mChildren.size() ).arg( child->mName ).arg( child->mType ) );

  int i;
  if ( type() == Directory )
  {
    for ( i = 0; i < mChildren.size(); i++ )
    {
      // sort items by type, so directories are before data items
      if ( mChildren[i]->mType == child->mType &&
           mChildren[i]->mName.localeAwareCompare( child->mName ) > 0 )
        break;
    }
  }
  else
  {
    for ( i = 0; i < mChildren.size(); i++ )
    {
      if ( mChildren[i]->mName.localeAwareCompare( child->mName ) >= 0 )
        break;
    }
  }

  if ( refresh )
    emit beginInsertItems( this, i, i );

  mChildren.insert( i, child );
  child->setParent( this );

  connect( child, SIGNAL( beginInsertItems( QgsDataItem*, int, int ) ),
           this, SLOT( emitBeginInsertItems( QgsDataItem*, int, int ) ) );
  connect( child, SIGNAL( endInsertItems() ),
           this, SLOT( emitEndInsertItems() ) );
  connect( child, SIGNAL( beginRemoveItems( QgsDataItem*, int, int ) ),
           this, SLOT( emitBeginRemoveItems( QgsDataItem*, int, int ) ) );
  connect( child, SIGNAL( endRemoveItems() ),
           this, SLOT( emitEndRemoveItems() ) );
  connect( child, SIGNAL( dataChanged( QgsDataItem* ) ),
           this, SLOT( emitDataChanged( QgsDataItem* ) ) );

  if ( refresh )
    emit endInsertItems();
}
void QgsDataItem::deleteChildItem( QgsDataItem * child )
{
  QgsDebugMsgLevel( "mName = " + child->mName, 2 );
  int i = mChildren.indexOf( child );
  Q_ASSERT( i >= 0 );
  emit beginRemoveItems( this, i, i );
  mChildren.remove( i );
  delete child; // deleting QObject child removes it from QObject parent
  emit endRemoveItems();
}

QgsDataItem * QgsDataItem::removeChildItem( QgsDataItem * child )
{
  QgsDebugMsgLevel( "mName = " + child->mName, 2 );
  int i = mChildren.indexOf( child );
  Q_ASSERT( i >= 0 );
  emit beginRemoveItems( this, i, i );
  mChildren.remove( i );
  emit endRemoveItems();
  disconnect( child, SIGNAL( beginInsertItems( QgsDataItem*, int, int ) ),
              this, SLOT( emitBeginInsertItems( QgsDataItem*, int, int ) ) );
  disconnect( child, SIGNAL( endInsertItems() ),
              this, SLOT( emitEndInsertItems() ) );
  disconnect( child, SIGNAL( beginRemoveItems( QgsDataItem*, int, int ) ),
              this, SLOT( emitBeginRemoveItems( QgsDataItem*, int, int ) ) );
  disconnect( child, SIGNAL( endRemoveItems() ),
              this, SLOT( emitEndRemoveItems() ) );
  disconnect( child, SIGNAL( dataChanged( QgsDataItem* ) ),
              this, SLOT( emitDataChanged( QgsDataItem* ) ) );
  child->setParent( 0 );
  return child;
}

int QgsDataItem::findItem( QVector<QgsDataItem*> items, QgsDataItem * item )
{
  for ( int i = 0; i < items.size(); i++ )
  {
    QgsDebugMsgLevel( QString::number( i ) + " : " + items[i]->mPath + " x " + item->mPath, 2 );
    if ( items[i]->equal( item ) )
      return i;
  }
  return -1;
}

bool QgsDataItem::equal( const QgsDataItem *other )
{
  if ( metaObject()->className() == other->metaObject()->className() &&
       mPath == other->path() )
  {
    return true;
  }
  return false;
}

void QgsDataItem::setLoadingIcon()
{
  mLoadingIcon = QIcon( mLoadingMovie->currentPixmap() );
}

QgsDataItem::State QgsDataItem::state() const
{
  // for backward compatibility (if subclass set mPopulated directly)
  // TODO: remove in 3.0
  if ( mPopulated )
    return Populated;
  return mState;
}

void QgsDataItem::setState( State state )
{
  if ( state == mState )
    return;

  if ( state == Populating ) // start loading
  {
    if ( !mLoadingMovie )
    {
      // QApplication as parent to ensure that it is deleted before QApplication
      mLoadingMovie = new QMovie( QApplication::instance() );
      mLoadingMovie->setFileName( QgsApplication::iconPath( "/mIconLoading.gif" ) );
      mLoadingMovie->setCacheMode( QMovie::CacheAll );
      connect( mLoadingMovie, SIGNAL( frameChanged( int ) ), SLOT( setLoadingIcon() ) );
    }
    connect( mLoadingMovie, SIGNAL( frameChanged( int ) ), SLOT( emitDataChanged() ) );
    mLoadingCount++;
    mLoadingMovie->setPaused( false );
  }
  else if ( mState == Populating && mLoadingMovie ) // stop loading
  {
    disconnect( mLoadingMovie, SIGNAL( frameChanged( int ) ), this, SLOT( emitDataChanged() ) );
    mLoadingCount--;
    if ( mLoadingCount == 0 )
    {
      mLoadingMovie->setPaused( true );
    }
  }

  mState = state;
  // for backward compatibility (if subclass access mPopulated directly)
  // TODO: remove in 3.0
  mPopulated = state == Populated;
}

// ---------------------------------------------------------------------

QgsLayerItem::QgsLayerItem( QgsDataItem* parent, QString name, QString path, QString uri, LayerType layerType, QString providerKey )
    : QgsDataItem( Layer, parent, name, path )
    , mProviderKey( providerKey )
    , mUri( uri )
    , mLayerType( layerType )
{
  switch ( layerType )
  {
    case Point:      mIconName = "/mIconPointLayer.svg"; break;
    case Line:       mIconName = "/mIconLineLayer.svg"; break;
    case Polygon:    mIconName = "/mIconPolygonLayer.svg"; break;
      // TODO add a new icon for generic Vector layers
    case Vector :    mIconName = "/mIconPolygonLayer.svg"; break;
    case TableLayer: mIconName = "/mIconTableLayer.png"; break;
    case Raster:     mIconName = "/mIconRaster.svg"; break;
    default:         mIconName = "/mIconLayer.png"; break;
  }
}

QgsMapLayer::LayerType QgsLayerItem::mapLayerType()
{
  if ( mLayerType == QgsLayerItem::Raster )
    return QgsMapLayer::RasterLayer;
  return QgsMapLayer::VectorLayer;
}

bool QgsLayerItem::equal( const QgsDataItem *other )
{
  //QgsDebugMsg ( mPath + " x " + other->mPath );
  if ( type() != other->type() )
  {
    return false;
  }
  //const QgsLayerItem *o = qobject_cast<const QgsLayerItem *> ( other );
  const QgsLayerItem *o = dynamic_cast<const QgsLayerItem *>( other );
  return ( mPath == o->mPath && mName == o->mName && mUri == o->mUri && mProviderKey == o->mProviderKey );
}

// ---------------------------------------------------------------------
QgsDataCollectionItem::QgsDataCollectionItem( QgsDataItem* parent, QString name, QString path )
    : QgsDataItem( Collection, parent, name, path )
{
  mCapabilities = Fertile;
  mIconName = "/mIconDbSchema.png";
}

QgsDataCollectionItem::~QgsDataCollectionItem()
{
  QgsDebugMsgLevel( "mName = " + mName + " mPath = " + mPath, 2 );

// Do not delete children, children are deleted by QObject parent
#if 0
  foreach ( QgsDataItem* i, mChildren )
  {
    QgsDebugMsgLevel( QString( "delete child = 0x%0" ).arg(( qlonglong )i, 8, 16, QLatin1Char( '0' ) ), 2 );
    delete i;
  }
#endif
}

//-----------------------------------------------------------------------
// QVector<QgsDataProvider*> QgsDirectoryItem::mProviders = QVector<QgsDataProvider*>();
QVector<QLibrary*> QgsDirectoryItem::mLibraries = QVector<QLibrary*>();

QgsDirectoryItem::QgsDirectoryItem( QgsDataItem* parent, QString name, QString path )
    : QgsDataCollectionItem( parent, name, path )
    , mDirPath( path )
{
  mType = Directory;
  init();
}

QgsDirectoryItem::QgsDirectoryItem( QgsDataItem* parent, QString name, QString dirPath, QString path )
    : QgsDataCollectionItem( parent, name, path )
    , mDirPath( dirPath )
{
  mType = Directory;
  init();
}

void QgsDirectoryItem::init()
{
  if ( mLibraries.size() > 0 )
    return;

  QStringList keys = QgsProviderRegistry::instance()->providerList();
  QStringList::const_iterator i;
  for ( i = keys.begin(); i != keys.end(); ++i )
  {
    QString k( *i );
    // some providers hangs with empty uri (Postgis) etc...
    // -> using libraries directly
    QLibrary *library = QgsProviderRegistry::instance()->providerLibrary( k );
    if ( library )
    {
      dataCapabilities_t * dataCapabilities = ( dataCapabilities_t * ) cast_to_fptr( library->resolve( "dataCapabilities" ) );
      if ( !dataCapabilities )
      {
        QgsDebugMsg( library->fileName() + " does not have dataCapabilities" );
        continue;
      }
      if ( dataCapabilities() == QgsDataProvider::NoDataCapabilities )
      {
        QgsDebugMsg( library->fileName() + " has NoDataCapabilities" );
        continue;
      }

      QgsDebugMsg( QString( "%1 dataCapabilities : %2" ).arg( library->fileName() ).arg( dataCapabilities() ) );
      mLibraries.append( library );
    }
    else
    {
      //QgsDebugMsg ( "Cannot get provider " + k );
    }
  }
}

QgsDirectoryItem::~QgsDirectoryItem()
{
}

QIcon QgsDirectoryItem::icon()
{
  return iconDir();
}

QVector<QgsDataItem*> QgsDirectoryItem::createChildren()
{
  QVector<QgsDataItem*> children;
  QDir dir( mDirPath );
  QSettings settings;

  QStringList entries = dir.entryList( QDir::AllDirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase );
  foreach ( QString subdir, entries )
  {
    QString subdirPath = dir.absoluteFilePath( subdir );
    QgsDebugMsgLevel( QString( "creating subdir: %1" ).arg( subdirPath ), 2 );

    QString path = mPath + "/" + subdir; // may differ from subdirPath
    QgsDirectoryItem *item = new QgsDirectoryItem( this, subdir, subdirPath, path );
    // propagate signals up to top

    children.append( item );
  }

  QStringList fileEntries = dir.entryList( QDir::Dirs | QDir::NoDotAndDotDot | QDir::Files, QDir::Name );
  foreach ( QString name, fileEntries )
  {
    QString path = dir.absoluteFilePath( name );
    QFileInfo fileInfo( path );

    // vsizip support was added to GDAL/OGR 1.6 but GDAL_VERSION_NUM not available here
    //   so we assume it's available anyway
    {
      QgsDataItem * item = QgsZipItem::itemFromPath( this, path, name, mPath + "/" + name );
      if ( item )
      {
        children.append( item );
        continue;
      }
    }

    foreach ( QLibrary *library, mLibraries )
    {
      // we could/should create separate list of providers for each purpose

      // TODO: use existing fileVectorFilters(),directoryDrivers() ?
      dataCapabilities_t * dataCapabilities = ( dataCapabilities_t * ) cast_to_fptr( library->resolve( "dataCapabilities" ) );
      if ( !dataCapabilities )
      {
        continue;
      }

      int capabilities = dataCapabilities();

      if ( !(( fileInfo.isFile() && ( capabilities & QgsDataProvider::File ) ) ||
             ( fileInfo.isDir() && ( capabilities & QgsDataProvider::Dir ) ) ) )
      {
        continue;
      }

      dataItem_t * dataItem = ( dataItem_t * ) cast_to_fptr( library->resolve( "dataItem" ) );
      if ( ! dataItem )
      {
        QgsDebugMsg( library->fileName() + " does not have dataItem" );
        continue;
      }

      QgsDataItem * item = dataItem( path, this );
      if ( item )
      {
        children.append( item );
      }
    }
  }

  return children;
}

bool QgsDirectoryItem::equal( const QgsDataItem *other )
{
  //QgsDebugMsg ( mPath + " x " + other->mPath );
  if ( type() != other->type() )
  {
    return false;
  }
  return ( path() == other->path() );
}

QWidget * QgsDirectoryItem::paramWidget()
{
  return new QgsDirectoryParamWidget( mPath );
}

QgsDirectoryParamWidget::QgsDirectoryParamWidget( QString path, QWidget* parent )
    : QTreeWidget( parent )
{
  setRootIsDecorated( false );

  // name, size, date, permissions, owner, group, type
  setColumnCount( 7 );
  QStringList labels;
  labels << tr( "Name" ) << tr( "Size" ) << tr( "Date" ) << tr( "Permissions" ) << tr( "Owner" ) << tr( "Group" ) << tr( "Type" );
  setHeaderLabels( labels );

  QStyle* style = QApplication::style();
  QIcon iconDirectory = QIcon( style->standardPixmap( QStyle::SP_DirClosedIcon ) );
  QIcon iconFile = QIcon( style->standardPixmap( QStyle::SP_FileIcon ) );
  QIcon iconLink = QIcon( style->standardPixmap( QStyle::SP_FileLinkIcon ) ); // TODO: symlink to directory?

  QList<QTreeWidgetItem *> items;

  QDir dir( path );
  QStringList entries = dir.entryList( QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase );
  foreach ( QString name, entries )
  {
    QFileInfo fi( dir.absoluteFilePath( name ) );
    QStringList texts;
    texts << name;
    QString size;
    if ( fi.size() > 1024 )
    {
      size = size.sprintf( "%.1f KiB", fi.size() / 1024.0 );
    }
    else if ( fi.size() > 1.048576e6 )
    {
      size = size.sprintf( "%.1f MiB", fi.size() / 1.048576e6 );
    }
    else
    {
      size = QString( "%1 B" ).arg( fi.size() );
    }
    texts << size;
    texts << fi.lastModified().toString( Qt::SystemLocaleShortDate );
    QString perm;
    perm += fi.permission( QFile::ReadOwner ) ? 'r' : '-';
    perm += fi.permission( QFile::WriteOwner ) ? 'w' : '-';
    perm += fi.permission( QFile::ExeOwner ) ? 'x' : '-';
    // QFile::ReadUser, QFile::WriteUser, QFile::ExeUser
    perm += fi.permission( QFile::ReadGroup ) ? 'r' : '-';
    perm += fi.permission( QFile::WriteGroup ) ? 'w' : '-';
    perm += fi.permission( QFile::ExeGroup ) ? 'x' : '-';
    perm += fi.permission( QFile::ReadOther ) ? 'r' : '-';
    perm += fi.permission( QFile::WriteOther ) ? 'w' : '-';
    perm += fi.permission( QFile::ExeOther ) ? 'x' : '-';
    texts << perm;

    texts << fi.owner();
    texts << fi.group();

    QString type;
    QIcon icon;
    if ( fi.isDir() )
    {
      type = tr( "folder" );
      icon = iconDirectory;
    }
    else if ( fi.isFile() )
    {
      type = tr( "file" );
      icon = iconFile;
    }
    else if ( fi.isSymLink() )
    {
      type = tr( "link" );
      icon = iconLink;
    }

    texts << type;

    QTreeWidgetItem *item = new QTreeWidgetItem( texts );
    item->setIcon( 0, icon );
    items << item;
  }

  addTopLevelItems( items );

  // hide columns that are not requested
  QSettings settings;
  QList<QVariant> lst = settings.value( "/dataitem/directoryHiddenColumns" ).toList();
  foreach ( QVariant colVariant, lst )
  {
    setColumnHidden( colVariant.toInt(), true );
  }
}

void QgsDirectoryParamWidget::mousePressEvent( QMouseEvent* event )
{
  if ( event->button() == Qt::RightButton )
  {
    // show the popup menu
    QMenu popupMenu;

    QStringList labels;
    labels << tr( "Name" ) << tr( "Size" ) << tr( "Date" ) << tr( "Permissions" ) << tr( "Owner" ) << tr( "Group" ) << tr( "Type" );
    for ( int i = 0; i < labels.count(); i++ )
    {
      QAction* action = popupMenu.addAction( labels[i], this, SLOT( showHideColumn() ) );
      action->setObjectName( QString::number( i ) );
      action->setCheckable( true );
      action->setChecked( !isColumnHidden( i ) );
    }

    popupMenu.exec( event->globalPos() );
  }
}

void QgsDirectoryParamWidget::showHideColumn()
{
  QAction* action = qobject_cast<QAction*>( sender() );
  if ( !action )
    return; // something is wrong

  int columnIndex = action->objectName().toInt();
  setColumnHidden( columnIndex, !isColumnHidden( columnIndex ) );

  // save in settings
  QSettings settings;
  QList<QVariant> lst;
  for ( int i = 0; i < columnCount(); i++ )
  {
    if ( isColumnHidden( i ) )
      lst.append( QVariant( i ) );
  }
  settings.setValue( "/dataitem/directoryHiddenColumns", lst );
}


QgsErrorItem::QgsErrorItem( QgsDataItem* parent, QString error, QString path )
    : QgsDataItem( QgsDataItem::Error, parent, error, path )
{
  mIconName = "/mIconDelete.png";

  setState( Populated ); // no more children
}

QgsErrorItem::~QgsErrorItem()
{
}

QgsFavouritesItem::QgsFavouritesItem( QgsDataItem* parent, QString name, QString path )
    : QgsDataCollectionItem( parent, name, "favourites:" )
{
  Q_UNUSED( path );
  mCapabilities |= Fast;
  mType = Favourites;
  mIconName = "/mIconFavourites.png";
  populate();
}

QgsFavouritesItem::~QgsFavouritesItem()
{
}

QVector<QgsDataItem*> QgsFavouritesItem::createChildren()
{
  QVector<QgsDataItem*> children;

  QSettings settings;
  QStringList favDirs = settings.value( "/browser/favourites", QVariant() ).toStringList();

  foreach ( QString favDir, favDirs )
  {
    QString pathName = favDir;
    pathName.replace( QRegExp( "[\\\\/]" ), "|" );
    QgsDataItem *item = new QgsDirectoryItem( this, favDir, favDir, mPath + "/" + pathName );
    if ( item )
    {
      children.append( item );
    }
  }

  return children;
}

void QgsFavouritesItem::addDirectory( QString favDir )
{
  QSettings settings;
  QStringList favDirs = settings.value( "/browser/favourites" ).toStringList();
  favDirs.append( favDir );
  settings.setValue( "/browser/favourites", favDirs );

  if ( state() == Populated )
    addChildItem( new QgsDirectoryItem( this, favDir, favDir ), true );
}

void QgsFavouritesItem::removeDirectory( QgsDirectoryItem *item )
{
  if ( !item )
    return;

  QSettings settings;
  QStringList favDirs = settings.value( "/browser/favourites" ).toStringList();
  favDirs.removeAll( item->dirPath() );
  settings.setValue( "/browser/favourites", favDirs );

  int idx = findItem( mChildren, item );
  if ( idx < 0 )
  {
    QgsDebugMsg( QString( "favourites item %1 not found" ).arg( item->path() ) );
    return;
  }

  if ( state() == Populated )
    deleteChildItem( mChildren[idx] );
}

//-----------------------------------------------------------------------
QStringList QgsZipItem::mProviderNames = QStringList();
QVector<dataItem_t *> QgsZipItem::mDataItemPtr = QVector<dataItem_t*>();


QgsZipItem::QgsZipItem( QgsDataItem* parent, QString name, QString path )
    : QgsDataCollectionItem( parent, name, path )
{
  mDirPath = path;
  init();
}

QgsZipItem::QgsZipItem( QgsDataItem* parent, QString name, QString dirPath, QString path )
    : QgsDataCollectionItem( parent, name, path )
    , mDirPath( dirPath )
{
  init();
}

void QgsZipItem::init()
{
  mType = Collection; //Zip??
  mIconName = "/mIconZip.png";
  mVsiPrefix = vsiPrefix( mDirPath );

  if ( mProviderNames.size() == 0 )
  {
    // QStringList keys = QgsProviderRegistry::instance()->providerList();
    // only use GDAL and OGR providers as we use the VSIFILE mechanism
    QStringList keys;
    // keys << "ogr" << "gdal";
    keys << "gdal" << "ogr";

    QStringList::const_iterator i;
    for ( i = keys.begin(); i != keys.end(); ++i )
    {
      QString k( *i );
      QgsDebugMsg( "provider " + k );
      // some providers hangs with empty uri (Postgis) etc...
      // -> using libraries directly
      QLibrary *library = QgsProviderRegistry::instance()->providerLibrary( k );
      if ( library )
      {
        dataCapabilities_t * dataCapabilities = ( dataCapabilities_t * ) cast_to_fptr( library->resolve( "dataCapabilities" ) );
        if ( !dataCapabilities )
        {
          QgsDebugMsg( library->fileName() + " does not have dataCapabilities" );
          continue;
        }
        if ( dataCapabilities() == QgsDataProvider::NoDataCapabilities )
        {
          QgsDebugMsg( library->fileName() + " has NoDataCapabilities" );
          continue;
        }
        QgsDebugMsg( QString( "%1 dataCapabilities : %2" ).arg( library->fileName() ).arg( dataCapabilities() ) );

        dataItem_t * dataItem = ( dataItem_t * ) cast_to_fptr( library->resolve( "dataItem" ) );
        if ( ! dataItem )
        {
          QgsDebugMsg( library->fileName() + " does not have dataItem" );
          continue;
        }

        // mLibraries.append( library );
        mDataItemPtr.append( dataItem );
        mProviderNames.append( k );
      }
      else
      {
        //QgsDebugMsg ( "Cannot get provider " + k );
      }
    }
  }

}

QgsZipItem::~QgsZipItem()
{
}

// internal function to scan a vsidir (zip or tar file) recursively
// GDAL trunk has this since r24423 (05/16/12) - VSIReadDirRecursive()
// use a copy of the function internally for now,
// but use char ** and CSLAddString, because CPLStringList was added in gdal-1.9
char **VSIReadDirRecursive1( const char *pszPath )
{
  // CPLStringList oFiles = NULL;
  char **papszOFiles = NULL;
  char **papszFiles1 = NULL;
  char **papszFiles2 = NULL;
  VSIStatBufL psStatBuf;
  CPLString osTemp1, osTemp2;
  int i, j;
  int nCount1, nCount2;

  // get listing
  papszFiles1 = VSIReadDir( pszPath );
  if ( ! papszFiles1 )
    return NULL;

  // get files and directories inside listing
  nCount1 = CSLCount( papszFiles1 );
  for ( i = 0; i < nCount1; i++ )
  {
    // build complete file name for stat
    osTemp1.clear();
    osTemp1.append( pszPath );
    osTemp1.append( "/" );
    osTemp1.append( papszFiles1[i] );

    // if is file, add it
    if ( VSIStatL( osTemp1.c_str(), &psStatBuf ) == 0 &&
         VSI_ISREG( psStatBuf.st_mode ) )
    {
      // oFiles.AddString( papszFiles1[i] );
      papszOFiles = CSLAddString( papszOFiles, papszFiles1[i] );
    }
    else if ( VSIStatL( osTemp1.c_str(), &psStatBuf ) == 0 &&
              VSI_ISDIR( psStatBuf.st_mode ) )
    {
      // add directory entry
      osTemp2.clear();
      osTemp2.append( papszFiles1[i] );
      osTemp2.append( "/" );
      // oFiles.AddString( osTemp2.c_str() );
      papszOFiles = CSLAddString( papszOFiles, osTemp2.c_str() );

      // recursively add files inside directory
      papszFiles2 = VSIReadDirRecursive1( osTemp1.c_str() );
      if ( papszFiles2 )
      {
        nCount2 = CSLCount( papszFiles2 );
        for ( j = 0; j < nCount2; j++ )
        {
          osTemp2.clear();
          osTemp2.append( papszFiles1[i] );
          osTemp2.append( "/" );
          osTemp2.append( papszFiles2[j] );
          // oFiles.AddString( osTemp2.c_str() );
          papszOFiles = CSLAddString( papszOFiles, osTemp2.c_str() );
        }
        CSLDestroy( papszFiles2 );
      }
    }
  }
  CSLDestroy( papszFiles1 );

  // return oFiles.StealList();
  return papszOFiles;
}

QVector<QgsDataItem*> QgsZipItem::createChildren()
{
  QVector<QgsDataItem*> children;
  QString tmpPath;
  QString childPath;
  QSettings settings;
  QString scanZipSetting = settings.value( "/qgis/scanZipInBrowser2", "basic" ).toString();

  mZipFileList.clear();

  QgsDebugMsgLevel( QString( "path = %1 name= %2 scanZipSetting= %3 vsiPrefix= %4" ).arg( path() ).arg( name() ).arg( scanZipSetting ).arg( mVsiPrefix ), 2 );

  // if scanZipBrowser == no: skip to the next file
  if ( scanZipSetting == "no" )
  {
    return children;
  }

  // first get list of files
  getZipFileList();

  // loop over files inside zip
  foreach ( QString fileName, mZipFileList )
  {
    QFileInfo info( fileName );
    tmpPath = mVsiPrefix + path() + "/" + fileName;
    QgsDebugMsgLevel( "tmpPath = " + tmpPath, 3 );

    // foreach( dataItem_t *dataItem, mDataItemPtr )
    for ( int i = 0; i < mProviderNames.size(); i++ )
    {
      // ugly hack to remove .dbf file if there is a .shp file
      if ( mProviderNames[i] == "ogr" )
      {
        if ( info.suffix().toLower() == "dbf" )
        {
          if ( mZipFileList.indexOf( fileName.left( fileName.count() - 4 ) + ".shp" ) != -1 )
            continue;
        }
        if ( info.completeSuffix().toLower() == "shp.xml" )
        {
          continue;
        }
      }

      // try to get data item from provider
      dataItem_t *dataItem = mDataItemPtr[i];
      if ( dataItem )
      {
        QgsDebugMsgLevel( QString( "trying to load item %1 with %2" ).arg( tmpPath ).arg( mProviderNames[i] ), 3 );
        QgsDataItem * item = dataItem( tmpPath, this );
        if ( item )
        {
          QgsDebugMsgLevel( "loaded item", 3 );
          childPath = tmpPath;
          children.append( item );
          break;
        }
        else
        {
          QgsDebugMsgLevel( "not loaded item", 3 );
        }
      }
    }

  }

  if ( children.size() == 1 )
  {
    // save the name of the only child so we can get a normal data item from it
    mPath = childPath;
  }

  return children;
}

QgsDataItem* QgsZipItem::itemFromPath( QgsDataItem* parent, QString path, QString name )
{
  return itemFromPath( parent, path, name, path );
}

QgsDataItem* QgsZipItem::itemFromPath( QgsDataItem* parent, QString dirPath, QString name, QString path )
{
  QSettings settings;
  QString scanZipSetting = settings.value( "/qgis/scanZipInBrowser2", "basic" ).toString();
  QString vsiPath = path;
  int zipFileCount = 0;
  QStringList zipFileList;
  QFileInfo fileInfo( dirPath );
  QString vsiPrefix = QgsZipItem::vsiPrefix( dirPath );
  QgsZipItem * zipItem = 0;
  bool populated = false;

  QgsDebugMsgLevel( QString( "path = %1 name= %2 scanZipSetting= %3 vsiPrefix= %4" ).arg( path ).arg( name ).arg( scanZipSetting ).arg( vsiPrefix ), 3 );

  // don't scan if scanZipBrowser == no
  if ( scanZipSetting == "no" )
    return 0;

  // don't scan if this file is not a /vsizip/ or /vsitar/ item
  if (( vsiPrefix != "/vsizip/" && vsiPrefix != "/vsitar/" ) )
    return 0;

  zipItem = new QgsZipItem( parent, name, dirPath, path );

  if ( zipItem )
  {
    // force populate zipItem if it has less than 10 items and is not a .tgz or .tar.gz file (slow loading)
    // for other items populating will be delayed until item is opened
    // this might be polluting the tree with empty items but is necessary for performance reasons
    // could also accept all files smaller than a certain size and add options for file count and/or size

    // first get list of files inside .zip or .tar files
    if ( path.endsWith( ".zip", Qt::CaseInsensitive ) ||
         path.endsWith( ".tar", Qt::CaseInsensitive ) )
    {
      zipFileList = zipItem->getZipFileList();
    }
    // force populate if less than 10 items
    if ( zipFileList.count() > 0 && zipFileList.count() <= 10 )
    {
      zipItem->populate( zipItem->createChildren() );
      populated = true; // there is no QgsDataItem::isPopulated() function
      QgsDebugMsgLevel( QString( "Got zipItem with %1 children, path=%2, name=%3" ).arg( zipItem->rowCount() ).arg( zipItem->path() ).arg( zipItem->name() ), 3 );
    }
    else
    {
      QgsDebugMsgLevel( QString( "Delaying populating zipItem with path=%1, name=%2" ).arg( zipItem->path() ).arg( zipItem->name() ), 3 );
    }
  }

  // only display if has children or if is not populated
  if ( zipItem && ( !populated || zipItem->rowCount() > 1 ) )
  {
    QgsDebugMsgLevel( "returning zipItem", 3 );
    return zipItem;
  }
  // if 1 or 0 child found, create a single data item using the normal path or the full path given by QgsZipItem
  else
  {
    if ( zipItem )
    {
      vsiPath = zipItem->path();
      zipFileCount = zipFileList.count();
      delete zipItem;
    }

    QgsDebugMsgLevel( QString( "will try to create a normal dataItem from path= %2 or %3" ).arg( path ).arg( vsiPath ), 3 );

    // try to open using registered providers (gdal and ogr)
    for ( int i = 0; i < mProviderNames.size(); i++ )
    {
      dataItem_t *dataItem = mDataItemPtr[i];
      if ( dataItem )
      {
        QgsDataItem *item = 0;
        // try first with normal path (Passthru)
        // this is to simplify .qml handling, and without this some tests will fail
        // (e.g. testZipItemVectorTransparency(), second test)
        if (( mProviderNames[i] == "ogr" ) ||
            ( mProviderNames[i] == "gdal" && zipFileCount == 1 ) )
          item = dataItem( path, parent );
        // try with /vsizip/
        if ( ! item )
          item = dataItem( vsiPath, parent );
        if ( item )
          return item;
      }
    }
  }

  return 0;
}

const QStringList & QgsZipItem::getZipFileList()
{
  if ( ! mZipFileList.isEmpty() )
    return mZipFileList;

  QString tmpPath;
  QSettings settings;
  QString scanZipSetting = settings.value( "/qgis/scanZipInBrowser2", "basic" ).toString();

  QgsDebugMsgLevel( QString( "mDirPath = %1 name= %2 scanZipSetting= %3 vsiPrefix= %4" ).arg( mDirPath ).arg( name() ).arg( scanZipSetting ).arg( mVsiPrefix ), 3 );

  // if scanZipBrowser == no: skip to the next file
  if ( scanZipSetting == "no" )
  {
    return mZipFileList;
  }

  // get list of files inside zip file
  QgsDebugMsgLevel( QString( "Open file %1 with gdal vsi" ).arg( mVsiPrefix + path() ), 3 );
  char **papszSiblingFiles = VSIReadDirRecursive1( QString( mVsiPrefix + mDirPath ).toLocal8Bit().constData() );
  if ( papszSiblingFiles )
  {
    for ( int i = 0; i < CSLCount( papszSiblingFiles ); i++ )
    {
      tmpPath = papszSiblingFiles[i];
      QgsDebugMsgLevel( QString( "Read file %1" ).arg( tmpPath ), 3 );
      // skip directories (files ending with /)
      if ( tmpPath.right( 1 ) != "/" )
        mZipFileList << tmpPath;
    }
    CSLDestroy( papszSiblingFiles );
  }
  else
  {
    QgsDebugMsg( QString( "Error reading %1" ).arg( mDirPath ) );
  }

  return mZipFileList;
}

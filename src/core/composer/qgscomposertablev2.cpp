/***************************************************************************
                         qgscomposertablev2.cpp
                         ------------------
    begin                : July 2014
    copyright            : (C) 2014 by Nyall Dawson, Marco Hugentobler
    email                : nyall dot dawson at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgscomposertablev2.h"
#include "qgscomposerutils.h"
#include "qgscomposertablecolumn.h"
#include "qgssymbollayerv2utils.h"
#include "qgscomposerframe.h"

QgsComposerTableV2::QgsComposerTableV2( QgsComposition *composition, bool createUndoCommands )
    : QgsComposerMultiFrame( composition, createUndoCommands )
    , mCellMargin( 1.0 )
    , mHeaderFontColor( Qt::black )
    , mHeaderHAlignment( FollowColumn )
    , mHeaderMode( FirstFrame )
    , mContentFontColor( Qt::black )
    , mShowGrid( true )
    , mGridStrokeWidth( 0.5 )
    , mGridColor( Qt::black )
{
  //get default composer font from settings
  QSettings settings;
  QString defaultFontString = settings.value( "/Composer/defaultFont" ).toString();
  if ( !defaultFontString.isEmpty() )
  {
    mHeaderFont.setFamily( defaultFontString );
    mContentFont.setFamily( defaultFontString );
  }
}

QgsComposerTableV2::QgsComposerTableV2()
    : QgsComposerMultiFrame( 0, false )
{

}

QgsComposerTableV2::~QgsComposerTableV2()
{
  qDeleteAll( mColumns );
  mColumns.clear();
}

bool QgsComposerTableV2::writeXML( QDomElement& elem, QDomDocument & doc, bool ignoreFrames ) const
{
  elem.setAttribute( "cellMargin", QString::number( mCellMargin ) );
  elem.setAttribute( "headerFont", mHeaderFont.toString() );
  elem.setAttribute( "headerFontColor", QgsSymbolLayerV2Utils::encodeColor( mHeaderFontColor ) );
  elem.setAttribute( "headerHAlignment", QString::number(( int )mHeaderHAlignment ) );
  elem.setAttribute( "headerMode", QString::number(( int )mHeaderMode ) );
  elem.setAttribute( "contentFont", mContentFont.toString() );
  elem.setAttribute( "contentFontColor", QgsSymbolLayerV2Utils::encodeColor( mContentFontColor ) );
  elem.setAttribute( "gridStrokeWidth", QString::number( mGridStrokeWidth ) );
  elem.setAttribute( "gridColor", QgsSymbolLayerV2Utils::encodeColor( mGridColor ) );
  elem.setAttribute( "showGrid", mShowGrid );

  //columns
  QDomElement displayColumnsElem = doc.createElement( "displayColumns" );
  QList<QgsComposerTableColumn*>::const_iterator columnIt = mColumns.constBegin();
  for ( ; columnIt != mColumns.constEnd(); ++columnIt )
  {
    QDomElement columnElem = doc.createElement( "column" );
    ( *columnIt )->writeXML( columnElem, doc );
    displayColumnsElem.appendChild( columnElem );
  }
  elem.appendChild( displayColumnsElem );

  bool state = _writeXML( elem, doc, ignoreFrames );
  return state;
}

bool QgsComposerTableV2::readXML( const QDomElement &itemElem, const QDomDocument &doc, bool ignoreFrames )
{
  deleteFrames();

  //first create the frames
  if ( !_readXML( itemElem, doc, ignoreFrames ) )
  {
    return false;
  }

  if ( itemElem.isNull() )
  {
    return false;
  }

  mHeaderFont.fromString( itemElem.attribute( "headerFont", "" ) );
  mHeaderFontColor = QgsSymbolLayerV2Utils::decodeColor( itemElem.attribute( "headerFontColor", "0,0,0,255" ) );
  mHeaderHAlignment = QgsComposerTableV2::HeaderHAlignment( itemElem.attribute( "headerHAlignment", "0" ).toInt() );
  mHeaderMode = QgsComposerTableV2::HeaderMode( itemElem.attribute( "headerMode", "0" ).toInt() );
  mContentFont.fromString( itemElem.attribute( "contentFont", "" ) );
  mContentFontColor = QgsSymbolLayerV2Utils::decodeColor( itemElem.attribute( "contentFontColor", "0,0,0,255" ) );
  mCellMargin = itemElem.attribute( "cellMargin", "1.0" ).toDouble();
  mGridStrokeWidth = itemElem.attribute( "gridStrokeWidth", "0.5" ).toDouble();
  mShowGrid = itemElem.attribute( "showGrid", "1" ).toInt();
  mGridColor = QgsSymbolLayerV2Utils::decodeColor( itemElem.attribute( "gridColor", "0,0,0,255" ) );

  //restore column specifications
  qDeleteAll( mColumns );
  mColumns.clear();
  QDomNodeList columnsList = itemElem.elementsByTagName( "displayColumns" );
  if ( columnsList.size() > 0 )
  {
    QDomElement columnsElem =  columnsList.at( 0 ).toElement();
    QDomNodeList columnEntryList = columnsElem.elementsByTagName( "column" );
    for ( int i = 0; i < columnEntryList.size(); ++i )
    {
      QDomElement columnElem = columnEntryList.at( i ).toElement();
      QgsComposerTableColumn* column = new QgsComposerTableColumn;
      column->readXML( columnElem );
      mColumns.append( column );
    }
  }

  return true;
}

QSizeF QgsComposerTableV2::totalSize() const
{
  //TODO - handle multiple cell headers
  //also check height calculation function

  return mTableSize;
}


QPair< int, int > QgsComposerTableV2::rowRange( const QRectF extent, const int frameIndex ) const
{
  //calculate row height
  //TODO - need to traverse all previous frames to calculate what is visible in each
  //as the entire height of a frame may not be used for content
  double headerHeight = 0;
  double firstHeaderHeight = 2 * ( mShowGrid ? mGridStrokeWidth : 0 ) + 2 * mCellMargin +  QgsComposerUtils::fontAscentMM( mHeaderFont );

  if (( mHeaderMode == QgsComposerTableV2::FirstFrame && frameIndex < 1 )
      || ( mHeaderMode == QgsComposerTableV2::AllFrames ) )
  {
    //frame has a header
    headerHeight = firstHeaderHeight;
  }
  else
  {
    headerHeight = ( mShowGrid ? mGridStrokeWidth : 0 );
  }

  //remaining height available for content rows
  double contentHeight = extent.height() - headerHeight;
  double rowHeight = ( mShowGrid ? mGridStrokeWidth : 0 ) + 2 * mCellMargin + QgsComposerUtils::fontAscentMM( mContentFont );

  //using zero based indexes
  int firstVisible = qMax( floor(( extent.top() - firstHeaderHeight )  / rowHeight ), 0.0 );
  int rowsVisible = qMax( floor( contentHeight / rowHeight ), 0.0 );
  int lastVisible = qMin( firstVisible + rowsVisible, mTableContents.length() );

  return qMakePair( firstVisible, lastVisible );
}


void QgsComposerTableV2::render( QPainter *p, const QRectF &renderExtent, const int frameIndex )
{
  if ( !p )
  {
    return;
  }

  //calculate which rows to show in this frame
  QPair< int, int > rowsToShow = rowRange( renderExtent, frameIndex );

  if ( mComposition->plotStyle() == QgsComposition::Print ||
       mComposition->plotStyle() == QgsComposition::Postscript )
  {
    //exporting composition, so force an attribute refresh
    //we do this in case vector layer has changed via an external source (eg, another database user)
    refreshAttributes();
  }

  p->save();
  //antialiasing on
  p->setRenderHint( QPainter::Antialiasing, true );

  p->setPen( Qt::SolidLine );

  //now draw the text
  double currentX = ( mShowGrid ? mGridStrokeWidth : 0 );
  double currentY;

  QList<QgsComposerTableColumn*>::const_iterator columnIt = mColumns.constBegin();

  int col = 0;
  double cellHeaderHeight = QgsComposerUtils::fontAscentMM( mHeaderFont ) + 2 * mCellMargin;
  double cellBodyHeight = QgsComposerUtils::fontAscentMM( mContentFont ) + 2 * mCellMargin;
  QRectF cell;

  //calculate whether a header is required
  bool drawHeader = (( mHeaderMode == QgsComposerTableV2::FirstFrame && frameIndex < 1 )
                     || ( mHeaderMode == QgsComposerTableV2::AllFrames ) );

  for ( ; columnIt != mColumns.constEnd(); ++columnIt )
  {
    currentY = ( mShowGrid ? mGridStrokeWidth : 0 );
    currentX += mCellMargin;

    if ( drawHeader )
    {
      //draw the header
      cell = QRectF( currentX, currentY, mMaxColumnWidthMap[col], cellHeaderHeight );

      //calculate alignment of header
      Qt::AlignmentFlag headerAlign = Qt::AlignLeft;
      switch ( mHeaderHAlignment )
      {
        case FollowColumn:
          headerAlign = ( *columnIt )->hAlignment();
          break;
        case HeaderLeft:
          headerAlign = Qt::AlignLeft;
          break;
        case HeaderCenter:
          headerAlign = Qt::AlignHCenter;
          break;
        case HeaderRight:
          headerAlign = Qt::AlignRight;
          break;
      }


      QgsComposerUtils::drawText( p, cell, ( *columnIt )->heading(), mHeaderFont, mHeaderFontColor, headerAlign, Qt::AlignVCenter, Qt::TextDontClip );

      currentY += cellHeaderHeight;
      currentY += ( mShowGrid ? mGridStrokeWidth : 0 );
    }

    //draw the attribute values
    for ( int row = rowsToShow.first; row < rowsToShow.second; ++row )
    {
      cell = QRectF( currentX, currentY, mMaxColumnWidthMap[col], cellBodyHeight );

      QVariant cellContents = mTableContents.at( row ).at( col );
      QString str = cellContents.toString();
      QgsComposerUtils::drawText( p, cell, str, mContentFont, mContentFontColor, ( *columnIt )->hAlignment(), Qt::AlignVCenter, Qt::TextDontClip );

      currentY += cellBodyHeight;
      currentY += ( mShowGrid ? mGridStrokeWidth : 0 );
    }

    currentX += mMaxColumnWidthMap[ col ];
    currentX += mCellMargin;
    currentX += ( mShowGrid ? mGridStrokeWidth : 0 );
    col++;
  }


  //and the borders
  if ( mShowGrid )
  {
    QPen gridPen;
    gridPen.setWidthF( mGridStrokeWidth );
    gridPen.setColor( mGridColor );
    gridPen.setJoinStyle( Qt::MiterJoin );
    p->setPen( gridPen );
    drawHorizontalGridLines( p, rowsToShow.second - rowsToShow.first, drawHeader );
    drawVerticalGridLines( p, mMaxColumnWidthMap, rowsToShow.second - rowsToShow.first, drawHeader );
  }

  p->restore();

}

void QgsComposerTableV2::setCellMargin( const double margin )
{
  if ( margin == mCellMargin )
  {
    return;
  }

  mCellMargin = margin;

  //since spacing has changed, we need to recalculate the table size
  adjustFrameToSize();

  emit changed();
}

void QgsComposerTableV2::setHeaderFont( const QFont &font )
{
  if ( font == mHeaderFont )
  {
    return;
  }

  mHeaderFont = font;
  //since font attributes have changed, we need to recalculate the table size
  adjustFrameToSize();

  emit changed();
}

void QgsComposerTableV2::setHeaderFontColor( const QColor &color )
{
  if ( color == mHeaderFontColor )
  {
    return;
  }

  mHeaderFontColor = color;
  repaint();

  emit changed();
}

void QgsComposerTableV2::setHeaderHAlignment( const QgsComposerTableV2::HeaderHAlignment alignment )
{
  if ( alignment == mHeaderHAlignment )
  {
    return;
  }

  mHeaderHAlignment = alignment;
  repaint();

  emit changed();
}

void QgsComposerTableV2::setHeaderMode( const QgsComposerTableV2::HeaderMode mode )
{
  if ( mode == mHeaderMode )
  {
    return;
  }

  mHeaderMode = mode;
  adjustFrameToSize();

  emit changed();
}

void QgsComposerTableV2::setContentFont( const QFont &font )
{
  if ( font == mContentFont )
  {
    return;
  }

  mContentFont = font;
  //since font attributes have changed, we need to recalculate the table size
  adjustFrameToSize();

  emit changed();
}

void QgsComposerTableV2::setContentFontColor( const QColor &color )
{
  if ( color == mContentFontColor )
  {
    return;
  }

  mContentFontColor = color;
  repaint();

  emit changed();
}

void QgsComposerTableV2::setShowGrid( const bool showGrid )
{
  if ( showGrid == mShowGrid )
  {
    return;
  }

  mShowGrid = showGrid;
  //since grid spacing has changed, we need to recalculate the table size
  adjustFrameToSize();

  emit changed();
}

void QgsComposerTableV2::setGridStrokeWidth( const double width )
{
  if ( width == mGridStrokeWidth )
  {
    return;
  }

  mGridStrokeWidth = width;
  //since grid spacing has changed, we need to recalculate the table size
  adjustFrameToSize();

  emit changed();
}

void QgsComposerTableV2::setGridColor( const QColor &color )
{
  if ( color == mGridColor )
  {
    return;
  }

  mGridColor = color;
  repaint();

  emit changed();
}

void QgsComposerTableV2::setColumns( QgsComposerTableColumns columns )
{
  //remove existing columns
  qDeleteAll( mColumns );
  mColumns.clear();

  mColumns.append( columns );
}

QMap<int, QString> QgsComposerTableV2::headerLabels() const
{
  QMap<int, QString> headers;

  QgsComposerTableColumns::const_iterator columnIt = mColumns.constBegin();
  int col = 0;
  for ( ; columnIt != mColumns.constEnd(); ++columnIt )
  {
    headers.insert( col, ( *columnIt )->heading() );
    col++;
  }
  return headers;
}

QSizeF QgsComposerTableV2::fixedFrameSize( const int frameIndex ) const
{
  Q_UNUSED( frameIndex );
  return QSizeF( mTableSize.width(), 0 );
}

QSizeF QgsComposerTableV2::minFrameSize( const int frameIndex ) const
{
  double height = 0;
  if (( mHeaderMode == QgsComposerTableV2::FirstFrame && frameIndex < 1 )
      || ( mHeaderMode == QgsComposerTableV2::AllFrames ) )
  {
    //header required, force frame to be high enough for header
    height = 2 * ( mShowGrid ? mGridStrokeWidth : 0 ) + 2 * mCellMargin +  QgsComposerUtils::fontAscentMM( mHeaderFont );
  }
  return QSizeF( 0, height );
}

void QgsComposerTableV2::refreshAttributes()
{
  mMaxColumnWidthMap.clear();
  mTableContents.clear();

  //get new contents
  if ( !getTableContents( mTableContents ) )
  {
    return;
  }

  //since contents have changed, we also need to recalculate the column widths
  //and size of table
  adjustFrameToSize();
}

bool QgsComposerTableV2::calculateMaxColumnWidths()
{
  mMaxColumnWidthMap.clear();

  //first, go through all the column headers and calculate the max width values
  QgsComposerTableColumns::const_iterator columnIt = mColumns.constBegin();
  int col = 0;
  for ( ; columnIt != mColumns.constEnd(); ++columnIt )
  {
    double width = 0;
    if ( mHeaderMode != QgsComposerTableV2::NoHeaders )
    {
      width = QgsComposerUtils::textWidthMM( mHeaderFont, ( *columnIt )->heading() );
    }
    mMaxColumnWidthMap.insert( col, width );
    col++;
  }

  //next, go through all the table contents and calculate the max width values
  QgsComposerTableContents::const_iterator rowIt = mTableContents.constBegin();
  double currentCellTextWidth;
  for ( ; rowIt != mTableContents.constEnd(); ++rowIt )
  {
    QgsComposerTableRow::const_iterator colIt = rowIt->constBegin();
    int columnNumber = 0;
    for ( ; colIt != rowIt->constEnd(); ++colIt )
    {
      currentCellTextWidth = QgsComposerUtils::textWidthMM( mContentFont, ( *colIt ).toString() );
      mMaxColumnWidthMap[ columnNumber ] = qMax( currentCellTextWidth, mMaxColumnWidthMap[ columnNumber ] );
      columnNumber++;
    }
  }

  return true;
}

double QgsComposerTableV2::totalWidth()
{
  //check how much space each column needs
  if ( !calculateMaxColumnWidths() )
  {
    return 0;
  }

  //adapt frame to total width
  double totalWidth = 0;
  QMap<int, double>::const_iterator maxColWidthIt = mMaxColumnWidthMap.constBegin();
  for ( ; maxColWidthIt != mMaxColumnWidthMap.constEnd(); ++maxColWidthIt )
  {
    totalWidth += maxColWidthIt.value();
  }
  totalWidth += ( 2 * mMaxColumnWidthMap.size() * mCellMargin );
  totalWidth += ( mMaxColumnWidthMap.size() + 1 ) * ( mShowGrid ? mGridStrokeWidth : 0 );

  return totalWidth;
}

double QgsComposerTableV2::totalHeight() const
{
  //calculate height
  int n = mTableContents.size();
  double totalHeight = QgsComposerUtils::fontAscentMM( mHeaderFont )
                       + n * QgsComposerUtils::fontAscentMM( mContentFont )
                       + ( n + 1 ) * mCellMargin * 2
                       + ( n + 2 ) * ( mShowGrid ? mGridStrokeWidth : 0 );
  return totalHeight;
}

void QgsComposerTableV2::drawHorizontalGridLines( QPainter *painter, const int rows, const bool drawHeaderLines ) const
{
  //horizontal lines
  if ( rows < 1 && !drawHeaderLines )
  {
    return;
  }

  double halfGridStrokeWidth = ( mShowGrid ? mGridStrokeWidth : 0 ) / 2.0;
  double currentY = 0;
  currentY = halfGridStrokeWidth;
  if ( drawHeaderLines )
  {
    painter->drawLine( QPointF( halfGridStrokeWidth, currentY ), QPointF( mTableSize.width() - halfGridStrokeWidth, currentY ) );
    currentY += ( mShowGrid ? mGridStrokeWidth : 0 );
    currentY += ( QgsComposerUtils::fontAscentMM( mHeaderFont ) + 2 * mCellMargin );
  }
  for ( int row = 0; row < rows; ++row )
  {
    painter->drawLine( QPointF( halfGridStrokeWidth, currentY ), QPointF( mTableSize.width() - halfGridStrokeWidth, currentY ) );
    currentY += ( mShowGrid ? mGridStrokeWidth : 0 );
    currentY += ( QgsComposerUtils::fontAscentMM( mContentFont ) + 2 * mCellMargin );
  }
  painter->drawLine( QPointF( halfGridStrokeWidth, currentY ), QPointF( mTableSize.width() - halfGridStrokeWidth, currentY ) );
}

void QgsComposerTableV2::drawVerticalGridLines( QPainter *painter, const QMap<int, double> &maxWidthMap, const int numberRows, const bool hasHeader ) const
{
  //vertical lines
  if ( numberRows < 1 && !hasHeader )
  {
    return;
  }

  //calculate height of table within frame
  double tableHeight = 0;
  if ( hasHeader )
  {
    tableHeight += ( mShowGrid ? mGridStrokeWidth : 0 ) + mCellMargin * 2 + QgsComposerUtils::fontAscentMM( mHeaderFont );
  }

  tableHeight += numberRows * (( mShowGrid ? mGridStrokeWidth : 0 ) + mCellMargin * 2 + QgsComposerUtils::fontAscentMM( mContentFont ) );
  tableHeight += ( mShowGrid ? mGridStrokeWidth : 0 );

  double halfGridStrokeWidth = ( mShowGrid ? mGridStrokeWidth : 0 ) / 2.0;
  double currentX = halfGridStrokeWidth;
  painter->drawLine( QPointF( currentX, halfGridStrokeWidth ), QPointF( currentX, tableHeight - halfGridStrokeWidth ) );
  currentX += ( mShowGrid ? mGridStrokeWidth : 0 );
  QMap<int, double>::const_iterator maxColWidthIt = maxWidthMap.constBegin();
  for ( ; maxColWidthIt != maxWidthMap.constEnd(); ++maxColWidthIt )
  {
    currentX += ( maxColWidthIt.value() + 2 * mCellMargin );
    painter->drawLine( QPointF( currentX, halfGridStrokeWidth ), QPointF( currentX, tableHeight - halfGridStrokeWidth ) );
    currentX += ( mShowGrid ? mGridStrokeWidth : 0 );
  }
}

void QgsComposerTableV2::adjustFrameToSize()
{
  mTableSize = QSizeF( totalWidth(), totalHeight() );
  //force recalculation of frame rects, so that they are set to the correct
  //fixed and minimum frame sizes
  recalculateFrameRects();

  recalculateFrameSizes();
}
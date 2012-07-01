/*

Copyright 2012 Adam Reichold

This file is part of qpdfview.

qpdfview is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

qpdfview is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with qpdfview.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "documentview.h"

qreal DocumentView::s_pageSpacing = 5.0;
qreal DocumentView::s_thumbnailSpacing = 3.0;

qreal DocumentView::s_thumbnailSize = 150.0;

qreal DocumentView::s_minimumScaleFactor = 0.1;
qreal DocumentView::s_maximumScaleFactor = 10.0;

qreal DocumentView::pageSpacing()
{
    return s_pageSpacing;
}

void DocumentView::setPageSpacing(qreal pageSpacing)
{
    if(pageSpacing >= 0.0)
    {
        s_pageSpacing = pageSpacing;
    }
}

qreal DocumentView::thumbnailSpacing()
{
    return s_thumbnailSpacing;
}

void DocumentView::setThumbnailSpacing(qreal thumbnailSpacing)
{
    if(thumbnailSpacing >= 0.0)
    {
        s_thumbnailSpacing = thumbnailSpacing;
    }
}

qreal DocumentView::thumbnailSize()
{
    return s_thumbnailSize;
}

void DocumentView::setThumbnailSize(qreal thumbnailSize)
{
    if(thumbnailSize >= 0.0)
    {
        s_thumbnailSize = thumbnailSize;
    }
}

qreal DocumentView::minimumScaleFactor()
{
    return s_minimumScaleFactor;
}

qreal DocumentView::maximumScaleFactor()
{
    return s_maximumScaleFactor;
}

DocumentView::DocumentView(QWidget* parent) : QGraphicsView(parent),
    m_settings(0),
    m_mutex(),
    m_document(0),
    m_filePath(),
    m_numberOfPages(-1),
    m_currentPage(-1),
    m_returnToPage(-1),
    m_continuousMode(false),
    m_twoPagesMode(false),
    m_scaleMode(ScaleFactor),
    m_scaleFactor(1.0),
    m_rotation(Poppler::Page::Rotate0),
    m_highlightAll(false),
    m_pagesScene(0),
    m_pages(),
    m_heightToIndex(),
    m_thumbnailsScene(0),
    m_thumbnails(),
    m_highlight(0),
    m_outlineModel(0),
    m_propertiesModel(0),
    m_results(),
    m_currentResult(m_results.end()),
    m_search(0)
{
    m_pagesScene = new QGraphicsScene(this);
    m_thumbnailsScene = new QGraphicsScene(this);

    m_outlineModel = new QStandardItemModel(this);
    m_propertiesModel = new QStandardItemModel(this);

    setScene(m_pagesScene);

    setDragMode(QGraphicsView::ScrollHandDrag);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    setAcceptDrops(false);

    connect(verticalScrollBar(), SIGNAL(valueChanged(int)), SLOT(on_verticalScrollBar_valueChanged(int)));

    // highlight

    m_highlight = new QGraphicsRectItem();

    m_highlight->setVisible(false);
    scene()->addItem(m_highlight);

    QColor highlightColor = QApplication::palette().color(QPalette::Highlight);

    highlightColor.setAlpha(192);
    m_highlight->setBrush(QBrush(highlightColor));

    highlightColor.setAlpha(255);
    m_highlight->setPen(QPen(highlightColor));

    // search

    m_search = new QFutureWatcher< QPair< int, QList< QRectF > > >(this);

    connect(m_search, SIGNAL(resultReadyAt(int)), SLOT(on_search_resultReadyAt(int)));
    connect(m_search, SIGNAL(progressValueChanged(int)), SLOT(on_search_progressValueChanged(int)));
    connect(m_search, SIGNAL(canceled()), SIGNAL(searchCanceled()));
    connect(m_search, SIGNAL(finished()), SIGNAL(searchFinished()));

    // auto-refresh

    m_autoRefreshWatcher = new QFileSystemWatcher(this);
    m_autoRefreshTimer = new QTimer(this);

    m_autoRefreshTimer->setInterval(500);
    m_autoRefreshTimer->setSingleShot(true);

    connect(m_autoRefreshWatcher, SIGNAL(fileChanged(QString)), m_autoRefreshTimer, SLOT(start()));
    connect(m_autoRefreshTimer, SIGNAL(timeout()), this, SLOT(refresh()));

    // settings

    m_settings = new QSettings(this);

    m_continuousMode = m_settings->value("documentView/continuousMode", false).toBool();
    m_twoPagesMode = m_settings->value("documentView/twoPagesMode", false).toBool();
    m_scaleMode = static_cast< ScaleMode >(m_settings->value("documentView/scaleMode", 0).toUInt());
    m_scaleFactor = m_settings->value("documentView/scaleFactor", 1.0).toReal();
    m_rotation = static_cast< Poppler::Page::Rotation >(m_settings->value("documentView/rotation", 0).toUInt());
}

DocumentView::~DocumentView()
{
    qDeleteAll(m_pages);
    qDeleteAll(m_thumbnails);

    m_search->cancel();
    m_search->waitForFinished();

    if(m_document != 0)
    {
        delete m_document;
    }

    // settings

    m_settings->setValue("documentView/continuousMode", m_continuousMode);
    m_settings->setValue("documentView/twoPagesMode", m_twoPagesMode);
    m_settings->setValue("documentView/scaleMode", static_cast< uint >(m_scaleMode));
    m_settings->setValue("documentView/scaleFactor", m_scaleFactor);
    m_settings->setValue("documentView/rotation", static_cast< uint >(m_rotation));
}

const QString& DocumentView::filePath() const
{
    return m_filePath;
}

int DocumentView::numberOfPages() const
{
    return m_numberOfPages;
}

int DocumentView::currentPage() const
{
    return m_currentPage;
}

bool DocumentView::continousMode() const
{
    return m_continuousMode;
}

void DocumentView::setContinousMode(bool continousMode)
{
    if(m_continuousMode != continousMode)
    {
        m_continuousMode = continousMode;

        prepareView();

        emit continousModeChanged(m_continuousMode);
    }
}

bool DocumentView::twoPagesMode() const
{
    return m_twoPagesMode;
}

void DocumentView::setTwoPagesMode(bool twoPagesMode)
{
    if(m_twoPagesMode != twoPagesMode)
    {
        m_twoPagesMode = twoPagesMode;

        if(m_twoPagesMode)
        {
            if(m_currentPage != (m_currentPage % 2 != 0 ? m_currentPage :  m_currentPage - 1))
            {
                m_currentPage = m_currentPage % 2 != 0 ? m_currentPage :  m_currentPage - 1;

                emit currentPageChanged(m_currentPage);
            }
        }

        prepareScene();
        prepareView();

        emit twoPagesModeChanged(m_twoPagesMode);
    }
}

DocumentView::ScaleMode DocumentView::scaleMode() const
{
    return m_scaleMode;
}

void DocumentView::setScaleMode(ScaleMode scaleMode)
{
    if(m_scaleMode != scaleMode)
    {
        m_scaleMode = scaleMode;

        prepareScene();
        prepareView();

        emit scaleModeChanged(m_scaleMode);
    }
}

qreal DocumentView::scaleFactor() const
{
    return m_scaleFactor;
}

void DocumentView::setScaleFactor(qreal scaleFactor)
{
    if(m_scaleFactor != scaleFactor && scaleFactor >= s_minimumScaleFactor && scaleFactor <= s_maximumScaleFactor)
    {
        m_scaleFactor = scaleFactor;

        if(m_scaleMode == ScaleFactor)
        {
            prepareScene();
            prepareView();
        }

        emit scaleFactorChanged(m_scaleFactor);
    }
}

Poppler::Page::Rotation DocumentView::rotation() const
{
    return m_rotation;
}

void DocumentView::setRotation(Poppler::Page::Rotation rotation)
{
    if(m_rotation != rotation)
    {
        m_rotation = rotation;

        prepareScene();
        prepareView();

        emit rotationChanged(m_rotation);
    }
}

bool DocumentView::highlightAll() const
{
    return m_highlightAll;
}

void DocumentView::setHighlightAll(bool highlightAll)
{
    if(m_highlightAll != highlightAll)
    {
        m_highlightAll = highlightAll;

        for(int index = 0; index < m_numberOfPages; index++)
        {
            PageItem* page = m_pages.at(index);

            page->setHighlights(m_highlightAll ? m_results.values(index) : QList< QRectF >());
        }

        emit highlightAllChanged(m_highlightAll);
    }
}

QStandardItemModel* DocumentView::outlineModel() const
{
    return m_outlineModel;
}

QStandardItemModel* DocumentView::propertiesModel() const
{
    return m_propertiesModel;
}

QStandardItemModel* DocumentView::fontsModel()
{
    m_mutex.lock();

    QList< Poppler::FontInfo > fonts = m_document->fonts();

    m_mutex.unlock();

    QStandardItemModel* fontsModel = new QStandardItemModel();

    fontsModel->setRowCount(fonts.count());
    fontsModel->setColumnCount(5);

    fontsModel->setHorizontalHeaderLabels(QStringList() << tr("Name") << tr("Type") << tr("Embedded") << tr("Subset") << tr("File"));

    for(int index = 0; index < fonts.count(); index++)
    {
        Poppler::FontInfo font = fonts.at(index);

        fontsModel->setItem(index, 0, new QStandardItem(font.name()));
        fontsModel->setItem(index, 1, new QStandardItem(font.typeName()));
        fontsModel->setItem(index, 2, new QStandardItem(font.isEmbedded() ? tr("Yes") : tr("No")));
        fontsModel->setItem(index, 3, new QStandardItem(font.isSubset() ? tr("Yes") : tr("No")));
        fontsModel->setItem(index, 4, new QStandardItem(font.file()));
    }

    return fontsModel;
}

QGraphicsScene* DocumentView::thumbnailsScene() const
{
    return m_thumbnailsScene;
}

QGraphicsItem* DocumentView::thumbnailsItem(int page) const
{
    return m_thumbnails.value(page - 1, 0);
}

bool DocumentView::open(const QString& filePath)
{
    Poppler::Document* document = Poppler::Document::load(filePath);

    if(document != 0)
    {
        if(document->isLocked())
        {
            QString password = QInputDialog::getText(this, tr("Unlock document"), tr("Password:"), QLineEdit::Password);

            if(document->unlock(password.toLatin1(), password.toLatin1()))
            {
                delete document;
                return false;
            }
        }

        m_filePath = filePath;
        m_numberOfPages = document->numPages();
        m_currentPage = 1;
        m_returnToPage = -1;

        prepareDocument(document);

        emit filePathChanged(m_filePath);
        emit numberOfPagesChanged(m_numberOfPages);
        emit currentPageChanged(m_currentPage);
    }

    return document != 0;
}

bool DocumentView::refresh()
{
    Poppler::Document* document = Poppler::Document::load(m_filePath);

    if(document != 0)
    {
        if(document->isLocked())
        {
            QString password = QInputDialog::getText(this, tr("Unlock document"), tr("Password:"), QLineEdit::Password);

            if(document->unlock(password.toLatin1(), password.toLatin1()))
            {
                delete document;
                return false;
            }
        }

        m_numberOfPages = document->numPages();
        m_currentPage = m_currentPage <= m_numberOfPages ? m_currentPage : 1;
        m_returnToPage = m_returnToPage <= m_numberOfPages ? m_returnToPage : -1;

        prepareDocument(document);

        emit numberOfPagesChanged(m_numberOfPages);
        emit currentPageChanged(m_currentPage);
    }

    return document != 0;
}

bool DocumentView::saveCopy(const QString& filePath)
{
    m_mutex.lock();

    Poppler::PDFConverter* pdfConverter = m_document->pdfConverter();

    pdfConverter->setOutputFileName(filePath);
    bool ok = pdfConverter->convert();

    delete pdfConverter;

    m_mutex.unlock();

    return ok;
}

bool DocumentView::print(QPrinter* printer)
{
    int fromPage = printer->fromPage() != 0 ? printer->fromPage() : 1;
    int toPage = printer->toPage() != 0 ? printer->toPage() : m_numberOfPages;

#ifdef WITH_CUPS

    int num_dests = 0;
    cups_dest_t* dests = 0;

    int num_options = 0;
    cups_option_t* options = 0;

    cups_dest_t* dest = 0;
    int jobId = 0;

    num_dests = cupsGetDests(&dests);

    dest = cupsGetDest(printer->printerName().toLocal8Bit(), 0, num_dests, dests);

    if(dest != 0)
    {
        for(int index = 0; index < dest->num_options; index++)
        {
            num_options = cupsAddOption(dest->options[index].name, dest->options[index].value, num_options, &options);
        }

        num_options = cupsAddOption("page-ranges", QString("%1-%2").arg(fromPage).arg(toPage).toLocal8Bit(), num_options, &options);

        num_options = cupsAddOption("copies", QString("%1").arg(printer->copyCount()).toLocal8Bit(), num_options, &options);

        num_options = cupsAddOption("collate", QString("%1").arg(printer->collateCopies()).toLocal8Bit(), num_options, &options);

        switch(printer->duplex())
        {
        case QPrinter::DuplexNone:
            num_options = cupsAddOption("sides", "one-sided", num_options, &options);
            break;
        case QPrinter::DuplexAuto:
            break;
        case QPrinter::DuplexLongSide:
            num_options = cupsAddOption("sides", "two-sided-long-edge", num_options, &options);
            break;
        case QPrinter::DuplexShortSide:
            num_options = cupsAddOption("sides", "two-sided-short-edge", num_options, &options);
            break;
        }

        switch(printer->pageOrder())
        {
        case QPrinter::FirstPageFirst:
            num_options = cupsAddOption("outputorder", "normal", num_options, &options);
            break;
        case QPrinter::LastPageFirst:
            num_options = cupsAddOption("outputorder", "reverse", num_options, &options);
            break;
        }

        switch(printer->colorMode())
        {
        case QPrinter::Color:
            break;
        case QPrinter::GrayScale:
            num_options = cupsAddOption("ColorModel", "Gray", num_options, &options);
            break;
        }

        QFileInfo fileInfo(m_filePath);

        jobId = cupsPrintFile(dest->name, fileInfo.absoluteFilePath().toLocal8Bit(), fileInfo.completeBaseName().toLocal8Bit(), num_options, options);

        if(jobId < 1)
        {
            qDebug() << cupsLastErrorString();
        }
    }
    else
    {
        qDebug() << cupsLastErrorString();
    }

    cupsFreeDests(num_dests, dests);
    cupsFreeOptions(num_options, options);

    return jobId >= 1;

#else

    QProgressDialog* progressDialog = new QProgressDialog(this);
    progressDialog->setLabelText(tr("Printing '%1'...").arg(m_filePath));
    progressDialog->setRange(fromPage - 1, toPage);

    QPainter painter;
    painter.begin(printer);

    for(int index = fromPage - 1; index <= toPage - 1; index++)
    {
        progressDialog->setValue(index);

        QApplication::processEvents();

        {
            m_mutex.lock();

            Poppler::Page* page = m_document->page(index);

            qreal pageWidth =  printer->physicalDpiX() / 72.0 * page->pageSizeF().width();
            qreal pageHeight = printer->physicalDpiY() / 72.0 * page->pageSizeF().height();

            QImage image = page->renderToImage(printer->physicalDpiX(), printer->physicalDpiY());

            delete page;

            m_mutex.unlock();

            qreal scaleFactor = qMin(printer->width() / pageWidth, printer->height() / pageHeight);

            painter.setTransform(QTransform::fromScale(scaleFactor, scaleFactor));
            painter.drawImage(QPointF(), image);
        }

        if(index < toPage - 1)
        {
            printer->newPage();
        }

        QApplication::processEvents();

        if(progressDialog->wasCanceled())
        {
            delete progressDialog;
            return false;
        }
    }

    painter.end();

    delete progressDialog;
    return true;

#endif // WITH_CUPS
}

void DocumentView::previousPage()
{
    jumpToPage(currentPage() - (m_twoPagesMode ? 2 : 1));
}

void DocumentView::nextPage()
{
    jumpToPage(currentPage() + (m_twoPagesMode ? 2 : 1));
}

void DocumentView::firstPage()
{
    jumpToPage(1);
}

void DocumentView::lastPage()
{
    jumpToPage(numberOfPages());
}

void DocumentView::jumpToPage(int page, qreal changeLeft, qreal changeTop)
{
    if(page >= 1 && page <= m_numberOfPages && changeLeft >= 0.0 && changeLeft <= 1.0 && changeTop >= 0.0 && changeTop <= 1.0)
    {
        if(m_twoPagesMode)
        {
            if(m_currentPage != (page % 2 != 0 ? page : page - 1) || changeLeft != 0.0 || changeTop != 0.0)
            {
                m_returnToPage = m_currentPage;
                m_currentPage = page % 2 != 0 ? page : page - 1;

                prepareView(changeLeft, changeTop);

                emit currentPageChanged(m_currentPage);
            }
        }
        else
        {
            if(m_currentPage != page || changeLeft != 0.0 || changeTop != 0.0)
            {
                m_returnToPage = m_currentPage;
                m_currentPage = page;

                prepareView(changeLeft, changeTop);

                emit currentPageChanged(m_currentPage);
            }
        }
    }
}

void DocumentView::startSearch(const QString& text, bool matchCase)
{
    cancelSearch();

    QList< int > indices;

    indices.reserve(m_numberOfPages);

    for(int index = m_currentPage - 1; index < m_numberOfPages; index++)
    {
        indices.append(index);
    }

    for(int index = 0; index < m_currentPage - 1; index++)
    {
        indices.append(index);
    }

    m_search->setFuture(QtConcurrent::mapped(indices, Search(&m_mutex, m_document, text, matchCase)));
}

void DocumentView::cancelSearch()
{
    m_search->cancel();
    m_search->waitForFinished();

    m_results.clear();
    m_currentResult = m_results.end();

    if(m_highlightAll)
    {
        for(int index = 0; index < m_numberOfPages; index++)
        {
            PageItem* page = m_pages.at(index);

            page->setHighlights(QList< QRectF >());
        }
    }

    prepareHighlight();
}

void DocumentView::findPrevious()
{
    if(m_currentResult != m_results.end())
    {
        if(m_currentResult.key() == m_currentPage - 1 || (m_twoPagesMode && m_currentResult.key() == m_currentPage))
        {
            --m_currentResult;
        }
        else
        {
            m_currentResult = --m_results.upperBound(m_currentPage - 1);
        }
    }
    else
    {
        m_currentResult = --m_results.upperBound(m_currentPage - 1);
    }

    if(m_currentResult == m_results.end())
    {
        m_currentResult = --m_results.upperBound(m_numberOfPages - 1);
    }

    prepareHighlight();
}

void DocumentView::findNext()
{
    if(m_currentResult != m_results.end())
    {
        if(m_currentResult.key() == m_currentPage - 1 || (m_twoPagesMode && m_currentResult.key() == m_currentPage))
        {
            ++m_currentResult;
        }
        else
        {
            m_currentResult = --m_results.upperBound(m_currentPage - 1);
        }
    }
    else
    {
        m_currentResult = --m_results.upperBound(m_currentPage - 1);
    }

    if(m_currentResult == m_results.end())
    {
        m_currentResult = m_results.lowerBound(0);
    }

    prepareHighlight();
}

void DocumentView::zoomIn()
{
    if(scaleMode() != ScaleFactor)
    {
        PageItem* page = m_pages.at(m_currentPage - 1);

        setScaleFactor(qMin(page->scaleFactor() + 0.1, s_maximumScaleFactor));
        setScaleMode(ScaleFactor);
    }
    else
    {
        setScaleFactor(qMin(scaleFactor() + 0.1, s_maximumScaleFactor));
    }
}

void DocumentView::zoomOut()
{
    if(scaleMode() != ScaleFactor)
    {
        PageItem* page = m_pages.at(m_currentPage - 1);

        setScaleFactor(qMax(page->scaleFactor() - 0.1, s_minimumScaleFactor));
        setScaleMode(ScaleFactor);
    }
    else
    {
        setScaleFactor(qMax(scaleFactor() - 0.1, s_minimumScaleFactor));
    }
}

void DocumentView::originalSize()
{
    setScaleFactor(1.0);
    setScaleMode(ScaleFactor);
}

void DocumentView::rotateLeft()
{
    switch(rotation())
    {
    case Poppler::Page::Rotate0:
        setRotation(Poppler::Page::Rotate270);
        break;
    case Poppler::Page::Rotate90:
        setRotation(Poppler::Page::Rotate0);
        break;
    case Poppler::Page::Rotate180:
        setRotation(Poppler::Page::Rotate90);
        break;
    case Poppler::Page::Rotate270:
        setRotation(Poppler::Page::Rotate180);
        break;
    }
}

void DocumentView::rotateRight()
{
    switch(rotation())
    {
    case Poppler::Page::Rotate0:
        setRotation(Poppler::Page::Rotate90);
        break;
    case Poppler::Page::Rotate90:
        setRotation(Poppler::Page::Rotate180);
        break;
    case Poppler::Page::Rotate180:
        setRotation(Poppler::Page::Rotate270);
        break;
    case Poppler::Page::Rotate270:
        setRotation(Poppler::Page::Rotate0);
        break;
    }
}

void DocumentView::presentation()
{
    PresentationView* presentationView = new PresentationView(&m_mutex, m_document);

    presentationView->jumpToPage(currentPage());

    presentationView->show();
    presentationView->setAttribute(Qt::WA_DeleteOnClose);
}

void DocumentView::on_verticalScrollBar_valueChanged(int value)
{
    if(m_continuousMode)
    {
        QMap< qreal, int >::const_iterator lowerBound = m_heightToIndex.lowerBound(-value);

        if(lowerBound != m_heightToIndex.end())
        {
            int page = lowerBound.value() + 1;

            if(m_currentPage != page)
            {
                m_currentPage = page;

                emit currentPageChanged(m_currentPage);
            }
        }
    }
}

void DocumentView::on_pages_linkClicked(int page, qreal left, qreal top)
{
    page = page >= 1 ? page : 1;
    page = page <= m_numberOfPages ? page : m_numberOfPages;

    left = left >= 0.0 ? left : 0.0;
    left = left <= 1.0 ? left : 1.0;

    top = top >= 0.0 ? top : 0.0;
    top = top <= 1.0 ? top : 1.0;

    jumpToPage(page, left, top);
}

void DocumentView::on_pages_linkClicked(const QString& url)
{
    if(m_settings->value("documentView/openUrl", false).toBool())
    {
        QDesktopServices::openUrl(QUrl(url));
    }
    else
    {
        QMessageBox::information(this, tr("Information"), tr("Opening URL is disabled in the settings."));
    }
}

void DocumentView::on_thumbnails_pageClicked(int page)
{
    page = page >= 1 ? page : 1;
    page = page <= m_numberOfPages ? page : m_numberOfPages;

    jumpToPage(page);
}

void DocumentView::on_search_resultReadyAt(int resultIndex)
{
    int pageIndex = m_search->resultAt(resultIndex).first;
    QList< QRectF > results = m_search->resultAt(resultIndex).second;

    for(int index = results.count() - 1; index >= 0; index--)
    {
        m_results.insertMulti(pageIndex, results.at(index));
    }

    if(m_highlightAll)
    {
        PageItem* page = m_pages.at(pageIndex);

        page->setHighlights(results);
    }

    if(pageIndex >= m_currentPage - 1 && !results.isEmpty() && m_currentResult == m_results.end())
    {
        findNext();
    }
}

void DocumentView::on_search_progressValueChanged(int progressValue)
{
    emit searchProgressed(100 * (progressValue - m_search->progressMinimum()) / (m_search->progressMaximum() - m_search->progressMinimum()));
}

void DocumentView::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);

    if(!event->spontaneous())
    {
        prepareView();
    }
}

void DocumentView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);

    if(m_scaleMode != ScaleFactor)
    {
        prepareScene();
        prepareView();
    }
}

void DocumentView::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* menu = new QMenu();

    QAction* returnAction = menu->addAction(tr("&Return"));
    returnAction->setShortcut(QKeySequence(Qt::Key_Return));
    returnAction->setEnabled(m_returnToPage != -1);

    QAction* action = menu->exec(event->globalPos());

    if(action == returnAction)
    {
        jumpToPage(m_returnToPage);
    }

    delete menu;
}

void DocumentView::keyPressEvent(QKeyEvent* event)
{
    if(event->modifiers() == Qt::NoModifier)
    {
        if(event->key() == Qt::Key_Return)
        {
            jumpToPage(m_returnToPage);

            event->accept();
            return;
        }

        if(!m_continuousMode)
        {
            if(event->key() == Qt::Key_PageUp && verticalScrollBar()->value() == verticalScrollBar()->minimum() && m_currentPage > 1)
            {
                previousPage();

                verticalScrollBar()->setValue(verticalScrollBar()->maximum());

                event->accept();
                return;
            }
            else if(event->key() == Qt::Key_PageDown && verticalScrollBar()->value() == verticalScrollBar()->maximum() && !currentPageIsLastPage())
            {
                nextPage();

                verticalScrollBar()->setValue(verticalScrollBar()->minimum());

                event->accept();
                return;
            }
        }
    }

    QGraphicsView::keyPressEvent(event);
}

void DocumentView::wheelEvent(QWheelEvent* event)
{
    if(event->modifiers() == Qt::ControlModifier)
    {
        if(event->delta() > 0)
        {
            zoomIn();
        }
        else
        {
            zoomOut();
        }

        event->accept();
        return;
    }
    else if(event->modifiers() == Qt::ShiftModifier)
    {
        if(event->delta() > 0)
        {
            rotateLeft();
        }
        else
        {
            rotateRight();
        }

        event->accept();
        return;
    }
    else if(event->modifiers() == Qt::NoModifier)
    {
        if(!m_continuousMode)
        {
            if(event->delta() > 0 && verticalScrollBar()->value() == verticalScrollBar()->minimum() && m_currentPage > 1)
            {
                previousPage();

                verticalScrollBar()->setValue(verticalScrollBar()->maximum());

                event->accept();
                return;
            }
            else if(event->delta() < 0 && verticalScrollBar()->value() == verticalScrollBar()->maximum() && !currentPageIsLastPage())
            {
                nextPage();

                verticalScrollBar()->setValue(verticalScrollBar()->minimum());

                event->accept();
                return;
            }
        }
    }

    QGraphicsView::wheelEvent(event);
}

bool DocumentView::currentPageIsLastPage()
{
    if(m_twoPagesMode)
    {
        return m_currentPage == (m_numberOfPages % 2 != 0 ? m_numberOfPages : m_numberOfPages - 1);
    }
    else
    {
        return m_currentPage == m_numberOfPages;
    }
}

void DocumentView::prepareDocument(Poppler::Document* document)
{
    qDeleteAll(m_pages);
    qDeleteAll(m_thumbnails);

    cancelSearch();

    if(m_document != 0)
    {
        delete m_document;

        if(!m_autoRefreshWatcher->files().isEmpty())
        {
            m_autoRefreshWatcher->removePaths(m_autoRefreshWatcher->files());
        }
    }

    m_document = document;

    if(m_settings->value("documentView/autoRefresh", false).toBool())
    {
        m_autoRefreshWatcher->addPath(m_filePath);
    }

    m_document->setRenderHint(Poppler::Document::Antialiasing, m_settings->value("documentView/antialiasing", true).toBool());
    m_document->setRenderHint(Poppler::Document::TextAntialiasing, m_settings->value("documentView/textAntialiasing", true).toBool());
    m_document->setRenderHint(Poppler::Document::TextHinting, m_settings->value("documentView/textHinting", false).toBool());

    preparePages();
    prepareThumbnails();
    prepareOutline();
    prepareProperties();

    prepareScene();
    prepareView();
}

void DocumentView::preparePages()
{
    m_pages.clear();
    m_pages.reserve(m_numberOfPages);

    for(int index = 0; index < m_numberOfPages; index++)
    {
        PageItem* page = new PageItem(&m_mutex, m_document, index);

        page->setPhysicalDpi(physicalDpiX(), physicalDpiY());

        m_pagesScene->addItem(page);
        m_pages.append(page);

        connect(page, SIGNAL(linkClicked(int,qreal,qreal)), SLOT(on_pages_linkClicked(int,qreal,qreal)));
        connect(page, SIGNAL(linkClicked(QString)), SLOT(on_pages_linkClicked(QString)));
    }

    if(m_settings->value("pageItem/decoratePages", true).toBool())
    {
        m_pagesScene->setBackgroundBrush(QBrush(Qt::darkGray));
    }
    else
    {
        m_pagesScene->setBackgroundBrush(QBrush(Qt::white));
    }
}

void DocumentView::prepareThumbnails()
{
    m_thumbnails.clear();
    m_thumbnails.reserve(m_numberOfPages);

    qreal left = 0.0;
    qreal right = 0.0;
    qreal height = s_thumbnailSpacing;

    for(int index = 0; index < m_numberOfPages; index++)
    {
        ThumbnailItem* page = new ThumbnailItem(&m_mutex, m_document, index);

        page->setPhysicalDpi(physicalDpiX(), physicalDpiY());

        m_thumbnailsScene->addItem(page);
        m_thumbnails.append(page);

        connect(page, SIGNAL(pageClicked(int)), SLOT(on_thumbnails_pageClicked(int)));

        {
            // prepare scale factor

            QSizeF size = page->size();

            qreal pageWidth = physicalDpiX() / 72.0 * size.width();
            qreal pageHeight = physicalDpiY() / 72.0 * size.height();

            page->setScaleFactor(qMin(s_thumbnailSize / pageWidth, s_thumbnailSize / pageHeight));
        }

        {
            // prepare layout

            QRectF boundingRect = page->boundingRect();

            page->setPos(-boundingRect.left() - 0.5 * boundingRect.width(), height - boundingRect.top());

            left = qMin(left, -0.5 * boundingRect.width() - s_thumbnailSpacing);
            right = qMax(right, 0.5 * boundingRect.width() + s_thumbnailSpacing);
            height += boundingRect.height() + s_thumbnailSpacing;
        }

        QGraphicsSimpleTextItem* text = m_thumbnailsScene->addSimpleText(QString::number(index + 1));

        text->setPos(-0.5 * text->boundingRect().width(), height);

        height += text->boundingRect().height() + s_thumbnailSpacing;
    }

    if(m_settings->value("pageItem/decoratePages", true).toBool())
    {
        m_thumbnailsScene->setBackgroundBrush(QBrush(Qt::darkGray));
    }
    else
    {
        m_thumbnailsScene->setBackgroundBrush(QBrush(Qt::white));
    }

    m_thumbnailsScene->setSceneRect(left, 0.0, right - left, height);
}

void DocumentView::prepareOutline()
{
    m_outlineModel->clear();

    QDomDocument* toc = m_document->toc();

    if(toc != 0)
    {
        prepareOutline(toc->firstChild(), m_outlineModel->invisibleRootItem());

        delete toc;
    }
}

void DocumentView::prepareOutline(const QDomNode& node, QStandardItem* parent)
{
    QDomElement element = node.toElement();

    QStandardItem* item = new QStandardItem();

    item->setFlags(Qt::ItemIsEnabled);

    item->setText(element.tagName());
    item->setToolTip(element.tagName());

    Poppler::LinkDestination* linkDestination = 0;

    if(element.hasAttribute("Destination"))
    {
        linkDestination = new Poppler::LinkDestination(element.attribute("Destination"));
    }
    else if(element.hasAttribute("DestinationName"))
    {
        linkDestination = m_document->linkDestination(element.attribute("DestinationName"));
    }

    if(linkDestination != 0)
    {
        int page = linkDestination->pageNumber();
        qreal left = 0.0;
        qreal top = 0.0;

        page = page >= 1 ? page : 1;
        page = page <= m_numberOfPages ? page : m_numberOfPages;

        if(linkDestination->isChangeLeft())
        {
            left = linkDestination->left();

            left = left >= 0.0 ? left : 0.0;
            left = left <= 1.0 ? left : 1.0;
        }

        if(linkDestination->isChangeTop())
        {
            top = linkDestination->top();

            top = top >= 0.0 ? top : 0.0;
            top = top <= 1.0 ? top : 1.0;
        }

        item->setData(page, Qt::UserRole + 1);
        item->setData(left, Qt::UserRole + 2);
        item->setData(top, Qt::UserRole + 3);

        delete linkDestination;
    }

    parent->appendRow(item);

    QDomNode siblingNode = node.nextSibling();
    if(!siblingNode.isNull())
    {
        prepareOutline(siblingNode, parent);
    }

    QDomNode childNode = node.firstChild();
    if(!childNode.isNull())
    {
        prepareOutline(childNode, item);
    }
}

void DocumentView::prepareProperties()
{
    m_propertiesModel->clear();

    QStringList keys = m_document->infoKeys();

    m_propertiesModel->setRowCount(keys.count());
    m_propertiesModel->setColumnCount(2);

    for(int index = 0; index < keys.count(); index++)
    {
        QString key = keys.at(index);
        QString value = m_document->info(key);

        if(value.startsWith("D:"))
        {
            value = m_document->date(key).toString();
        }

        m_propertiesModel->setItem(index, 0, new QStandardItem(key));
        m_propertiesModel->setItem(index, 1, new QStandardItem(value));
    }
}

void DocumentView::prepareScene()
{
    // prepare scale factor and rotation

    for(int index = 0; index < m_numberOfPages; index++)
    {
        PageItem* page = m_pages.at(index);
        QSizeF size = page->size();

        if(m_scaleMode != ScaleFactor)
        {
            qreal visibleWidth = 0.0;
            qreal visibleHeight = 0.0;

            qreal pageWidth = 0.0;
            qreal pageHeight = 0.0;

            qreal scaleFactor = 1.0;

            if(m_twoPagesMode)
            {
                visibleWidth = 0.5 * (viewport()->width() - 6 - 3.0 * s_pageSpacing);
            }
            else
            {
                visibleWidth = viewport()->width() - 6 - 2.0 * s_pageSpacing;
            }

            visibleHeight = viewport()->height() - 2.0 * s_pageSpacing;

            switch(m_rotation)
            {
            case Poppler::Page::Rotate0:
            case Poppler::Page::Rotate180:
                pageWidth = physicalDpiX() / 72.0 * size.width();
                pageHeight = physicalDpiY() / 72.0 * size.height();
                break;
            case Poppler::Page::Rotate90:
            case Poppler::Page::Rotate270:
                pageWidth = physicalDpiX() / 72.0 * size.height();
                pageHeight = physicalDpiY() / 72.0 * size.width();
                break;
            }

            switch(m_scaleMode)
            {
            case ScaleFactor:
                break;
            case FitToPageWidth:
                scaleFactor = visibleWidth / pageWidth;
                break;
            case FitToPageSize:
                scaleFactor = qMin(visibleWidth / pageWidth, visibleHeight / pageHeight);
                break;
            }

            page->setScaleFactor(scaleFactor);
        }
        else
        {
            page->setScaleFactor(m_scaleFactor);
        }

        page->setRotation(m_rotation);
    }

    // prepare layout

    m_heightToIndex.clear();

    qreal pageHeight = 0.0;

    qreal left = 0.0;
    qreal right = 0.0;
    qreal height = s_pageSpacing;

    for(int index = 0; index < m_numberOfPages; index++)
    {
        PageItem* page = m_pages.at(index);
        QRectF boundingRect = page->boundingRect();

        if(m_twoPagesMode)
        {
            if(index % 2 == 0)
            {
                page->setPos(-boundingRect.left() - boundingRect.width() - 0.5 * s_pageSpacing, height - boundingRect.top());

                m_heightToIndex.insert(-height + s_pageSpacing + 0.3 * pageHeight, index);

                pageHeight = boundingRect.height();

                left = qMin(left, -boundingRect.width() - 1.5 * s_pageSpacing);
            }
            else
            {
                page->setPos(-boundingRect.left() + 0.5 * s_pageSpacing, height - boundingRect.top());

                pageHeight = qMax(pageHeight, boundingRect.height());

                right = qMax(right, boundingRect.width() + 1.5 * s_pageSpacing);
                height += pageHeight + s_pageSpacing;
            }
        }
        else
        {
            page->setPos(-boundingRect.left() - 0.5 * boundingRect.width(), height - boundingRect.top());

            m_heightToIndex.insert(-height + s_pageSpacing + 0.3 * pageHeight, index);

            pageHeight = boundingRect.height();

            left = qMin(left, -0.5 * boundingRect.width() - s_pageSpacing);
            right = qMax(right, 0.5 * boundingRect.width() + s_pageSpacing);
            height += pageHeight + s_pageSpacing;
        }
    }

    m_pagesScene->setSceneRect(left, 0.0, right - left, height);
}

void DocumentView::prepareView(qreal changeLeft, qreal changeTop)
{
    qreal left = m_pagesScene->sceneRect().left();
    qreal top = 0.0;
    qreal width = m_pagesScene->sceneRect().width();
    qreal height = m_pagesScene->sceneRect().height();

    int horizontalValue = 0;
    int verticalValue = 0;

    for(int index = 0; index < m_pages.count(); index++)
    {
        PageItem* page = m_pages.at(index);

        if(m_continuousMode)
        {
            page->setVisible(true);

            if(index == m_currentPage - 1)
            {
                QRectF boundingRect = page->boundingRect().translated(page->pos());

                horizontalValue = qFloor(boundingRect.left() + changeLeft * boundingRect.width());
                verticalValue = qFloor(boundingRect.top() + changeTop * boundingRect.height());
            }
        }
        else
        {
            if(index == m_currentPage - 1)
            {
                page->setVisible(true);

                QRectF boundingRect = page->boundingRect().translated(page->pos());

                top = boundingRect.top() - s_pageSpacing;
                height = boundingRect.height() + 2.0 * s_pageSpacing;

                horizontalValue = qFloor(boundingRect.left() + changeLeft * boundingRect.width());
                verticalValue = qFloor(boundingRect.top() + changeTop * boundingRect.height());
            }
            else if(m_twoPagesMode && index == m_currentPage)
            {
                page->setVisible(true);

                QRectF boundingRect = page->boundingRect().translated(page->pos());

                top = qMin(top, boundingRect.top() - s_pageSpacing);
                height = qMax(height, boundingRect.height() + 2.0 * s_pageSpacing);
            }
            else
            {
                page->setVisible(false);
            }
        }

        if(m_currentResult != m_results.end())
        {
            if(m_currentResult.key() == index)
            {
                m_highlight->setPos(page->pos());
                m_highlight->setTransform(page->transform());
                page->stackBefore(m_highlight);
            }
        }
    }

    setSceneRect(left, top, width, height);

    horizontalScrollBar()->setValue(horizontalValue);
    verticalScrollBar()->setValue(verticalValue);
}

void DocumentView::prepareHighlight()
{
    if(m_currentResult != m_results.end())
    {
        jumpToPage(m_currentResult.key() + 1);

        PageItem* page = m_pages.at(m_currentResult.key());

        m_highlight->setPos(page->pos());
        m_highlight->setTransform(page->transform());
        page->stackBefore(m_highlight);

        m_highlight->setRect(m_currentResult.value());

        m_highlight->setVisible(true);

        disconnect(verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(on_verticalScrollBar_valueChanged(int)));
        centerOn(m_highlight);
        connect(verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(on_verticalScrollBar_valueChanged(int)));
    }
    else
    {
        m_highlight->setVisible(false);
    }
}

DocumentView::Search::Search(QMutex *mutex, Poppler::Document *document, const QString &text, bool matchCase) :
    m_mutex(mutex),
    m_document(document),
    m_text(text),
    m_matchCase(matchCase)
{
}

QPair< int, QList< QRectF > > DocumentView::Search::operator()(int index)
{
    QList< QRectF > results;

    m_mutex->lock();

    Poppler::Page* page = m_document->page(index);

#if defined(HAS_POPPLER_22)

    results = page->search(m_text, m_matchCase ? Poppler::Page::CaseSensitive : Poppler::Page::CaseInsensitive);

#elif defined(HAS_POPPLER_14)

    double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;

    while(page->search(m_text, left, top, right, bottom, Poppler::Page::NextResult, m_matchCase ? Poppler::Page::CaseSensitive : Poppler::Page::CaseInsensitive))
    {
        QRectF rect;
        rect.setLeft(left);
        rect.setTop(top);
        rect.setRight(right);
        rect.setBottom(bottom);

        results.append(rect);
    }

#else

    QRectF rect;

    while(page->search(m_text, rect, Poppler::Page::NextResult, m_matchCase ? Poppler::Page::CaseSensitive : Poppler::Page::CaseInsensitive))
    {
        results.append(rect);
    }

#endif

    delete page;

    m_mutex->unlock();

    return qMakePair(index, results);
}
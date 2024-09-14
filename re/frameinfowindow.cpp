#include "frameinfowindow.h"
#include "ui_frameinfowindow.h"
#include "mainwindow.h"
#include "helpwindow.h"
#include <QtDebug>
#include <vector>
#include "filterutility.h"
#include "qcpaxistickerhex.h"

const QColor FrameInfoWindow::byteGraphColors[8] = {Qt::blue, Qt::green,  Qt::black, Qt::red, //0 1 2 3
                                                    Qt::gray, Qt::darkYellow, Qt::cyan,  Qt::darkMagenta}; //4 5 6 7

QVector<QPen> FrameInfoWindow::bytePens;

const int numIntervalHistBars = 20;

FrameInfoWindow::FrameInfoWindow(const QVector<CANFrame> *frames, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FrameInfoWindow)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);

    readSettings();

    modelFrames = frames;

    // Using lambda expression to strip away the possible filter label before passing the ID to updateDetailsWindow
    connect(ui->listFrameID, &QListWidget::currentTextChanged, 
        [this](QString itemText)
            {
            FrameInfoWindow::updateDetailsWindow(FilterUtility::getId(itemText));
            } );

    connect(MainWindow::getReference(), &MainWindow::framesUpdated, this, &FrameInfoWindow::updatedFrames);
    connect(ui->btnSave, &QAbstractButton::clicked, this, &FrameInfoWindow::saveDetails);

    ui->splitter->setStretchFactor(0, 1); //idx, stretch factor
    ui->splitter->setStretchFactor(1, 4); //goal is to make right hand side larger by default

    ui->gridUpper->addWidget(new QLabel("Heatmap"), 0, 0);
    heatmap = new CANDataGrid();
    heatmap->setMode(GridMode::HEAT_VIEW);
    ui->gridUpper->addWidget(heatmap, 1, 0);

    ui->gridUpper->addWidget(new QLabel("Bit Histogram"), 0, 1);
    graphHistogram = new QCustomPlot();
    ui->gridUpper->addWidget(graphHistogram, 1, 1);

    ui->gridUpper->setRowMinimumHeight(0, 20);
    ui->gridUpper->setRowStretch(0, 1);
    ui->gridUpper->setRowStretch(1, 10);

    graphHistogram->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectAxes |
                                    QCP::iSelectLegend);

    graphHistogram->xAxis->setRange(0, 63);
    graphHistogram->yAxis->setRange(0, 100);
    QSharedPointer<QCPAxisTickerLog> graphHistoLogTicker(new QCPAxisTickerLog);
    graphHistogram->yAxis->setTicker(graphHistoLogTicker);
    graphHistogram->yAxis->setNumberFormat("eb"); // e = exponential, b = beautiful decimal powers
    graphHistogram->yAxis->setNumberPrecision(0); //log ticker always picks powers of 10 so no need or use for precision

    graphHistogram->setBufferDevicePixelRatio(1);

    graphHistogram->xAxis->setLabel("Bits");
    graphHistogram->yAxis->setLabel("Instances");

    graphHistogram->legend->setVisible(false);

    ui->timeHistogram->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectAxes |
                                        QCP::iSelectLegend);

    ui->timeHistogram->xAxis->setRange(0, numIntervalHistBars);
    ui->timeHistogram->yAxis->setRange(0, 100);
    QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
    ui->timeHistogram->yAxis->setTicker(logTicker);
    ui->timeHistogram->yAxis->setScaleType(QCPAxis::stLogarithmic);
    ui->timeHistogram->yAxis->setNumberFormat("eb"); // e = exponential, b = beautiful decimal powers
    ui->timeHistogram->yAxis->setNumberPrecision(0); //log ticker always picks powers of 10 so no need or use for precision

    ui->timeHistogram->xAxis->setLabel("Interval (ms)");
    ui->timeHistogram->yAxis->setLabel("Occurrences");

    ui->timeHistogram->legend->setVisible(false);
    ui->timeHistogram->setBufferDevicePixelRatio(1);

    if (useOpenGL)
    {
        graphHistogram->setAntialiasedElements(QCP::aeAll);
        graphHistogram->setOpenGl(true);
        ui->timeHistogram->setAntialiasedElements(QCP::aeAll);
        ui->timeHistogram->setOpenGl(true);
    }
    else
    {
        graphHistogram->setOpenGl(false);
        graphHistogram->setAntialiasedElements(QCP::aeNone);
        ui->timeHistogram->setOpenGl(false);
        ui->timeHistogram->setAntialiasedElements(QCP::aeNone);
    }

    // Prevent annoying accidental horizontal scrolling when filter list is populated with long interpreted message names
    ui->listFrameID->horizontalScrollBar()->setEnabled(false);

    installEventFilter(this);

    for (int i = 0; i < 8; i++)
    {
        QPen pen;
        pen.setColor(byteGraphColors[i]);
        pen.setWidth(1);
        bytePens.append(pen);
    }

    connect(graphHistogram, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(mousePress()));
    connect(graphHistogram, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(mouseWheel()));
    connect(ui->timeHistogram, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(mousePress()));
    connect(ui->timeHistogram, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(mouseWheel()));

    dbcHandler = DBCHandler::getReference();
}

void FrameInfoWindow::setupByteGraph(QCustomPlot *plot, int num)
{
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectAxes |
                                    QCP::iSelectLegend);

    plot->xAxis->setRange(0, 63);
    plot->yAxis->setRange(0, 265);
    if (useHexTicker)
    {
        QSharedPointer<QCPAxisTickerHex> hexTicker(new QCPAxisTickerHex);
        plot->yAxis->setTicker(hexTicker);
    }

    plot->xAxis->setLabel("Time [" + QString::number(num) + "]");
    plot->yAxis->setLabel("");

    plot->legend->setVisible(false);
    plot->setBufferDevicePixelRatio(1);

    if (useOpenGL)
    {
        plot->setAntialiasedElements(QCP::aeAll);
        plot->setOpenGl(true);
    }
    else
    {
        plot->setOpenGl(false);
        plot->setAntialiasedElements(QCP::aeNone);
    }

    connect(plot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(mousePress()));
    connect(plot, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(mouseWheel()));
    connect(plot, &QCustomPlot::mouseDoubleClick, this, &FrameInfoWindow::mouseDoubleClick);
}

void FrameInfoWindow::mousePress()
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot) return;
    // if an axis is selected, only allow the direction of that axis to be dragged
    // if no axis is selected, both directions may be dragged

    if (plot->xAxis->selectedParts().testFlag(QCPAxis::spAxis))
        plot->axisRect()->setRangeDrag(plot->xAxis->orientation());
    else if (plot->yAxis->selectedParts().testFlag(QCPAxis::spAxis))
        plot->axisRect()->setRangeDrag(plot->yAxis->orientation());
    else
        plot->axisRect()->setRangeDrag(Qt::Horizontal|Qt::Vertical);
}

void FrameInfoWindow::mouseWheel()
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot) return;

    // if an axis is selected, only allow the direction of that axis to be zoomed
    // if no axis is selected, both directions may be zoomed

    if (plot->xAxis->selectedParts().testFlag(QCPAxis::spAxis))
        plot->axisRect()->setRangeZoom(plot->xAxis->orientation());
    else if (plot->yAxis->selectedParts().testFlag(QCPAxis::spAxis))
        plot->axisRect()->setRangeZoom(plot->yAxis->orientation());
    else
        plot->axisRect()->setRangeZoom(Qt::Horizontal|Qt::Vertical);
}

void FrameInfoWindow::mouseDoubleClick()
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    bool hideMode = true;

    for (int i = 0; i < graphByte.size(); i++)
    {
        if (graphByte[i]->isHidden()) hideMode = false;
    }

    if (hideMode)
    {
        for (int i = 0; i < graphByte.size(); i++)
        {
            if (graphByte[i] == plot) qDebug() << "Idx " << i << " matched!";
            else graphByte[i]->setHidden(true);
        }
    }
    else
    {
        for (int i = 0; i < graphByte.size(); i++)
        {
            graphByte[i]->setHidden(false);
        }
    }
}

void FrameInfoWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    readSettings();
    refreshIDList();
    if (ui->listFrameID->count() > 0)
    {
        updateDetailsWindow(FilterUtility::getId(ui->listFrameID->item(0)));
        ui->listFrameID->setCurrentRow(0);
    }
}

bool FrameInfoWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key())
        {
        case Qt::Key_F1:
            HelpWindow::getRef()->showHelp("framedetails.md");
            break;
        }
        return true;
    } else {
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
}

FrameInfoWindow::~FrameInfoWindow()
{
    removeEventFilter(this);
    delete ui;
}

void FrameInfoWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    writeSettings();
    emit rejected();
}

void FrameInfoWindow::readSettings()
{
    QSettings settings;
 
    if (settings.value("Main/FilterLabeling", false).toBool())
    {
        ui->listFrameID->setMinimumWidth(250);
    }
    else
    {
        ui->listFrameID->setMinimumWidth(120);    
    }

    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        resize(settings.value("FrameInfo/WindowSize", QSize(794, 694)).toSize());
        move(Utility::constrainedWindowPos(settings.value("FrameInfo/WindowPos", QPoint(50, 50)).toPoint()));
    }
    useOpenGL = settings.value("Main/UseOpenGL", false).toBool();
    useHexTicker = settings.value("InfoCompare/GraphHex", false).toBool();
}

void FrameInfoWindow::writeSettings()
{
    QSettings settings;

    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        settings.setValue("FrameInfo/WindowSize", size());
        settings.setValue("FrameInfo/WindowPos", pos());
    }
}

void FrameInfoWindow::updatedFrames(int numFrames)
{
    if (numFrames == -1) //all frames deleted. Kill the display
    {
        ui->listFrameID->clear();
        ui->treeDetails->clear();
        foundID.clear();
        refreshIDList();
    }
    else if (numFrames == -2) //all new set of frames. Reset
    {
        ui->listFrameID->clear();
        ui->treeDetails->clear();
        foundID.clear();
        refreshIDList();
        if (ui->listFrameID->count() > 0)
        {
            updateDetailsWindow(FilterUtility::getId(ui->listFrameID->item(0)));
            ui->listFrameID->setCurrentRow(0);
        }
    }
    else //just got some new frames. See if they are relevant.
    {
        if (numFrames > modelFrames->count()) return;

        unsigned int currID = 0;
        if (ui->listFrameID->currentItem())
            currID = static_cast<unsigned int>(FilterUtility::getIdAsInt(ui->listFrameID->currentItem()));
        bool thisID = false;
        for (int x = modelFrames->count() - numFrames; x < modelFrames->count(); x++)
        {
            CANFrame thisFrame = modelFrames->at(x);
            int32_t id = static_cast<int32_t>(thisFrame.frameId());
            if (!foundID.contains(id))
            {
                foundID.append(id);
                FilterUtility::createFilterItem(id, ui->listFrameID);
            }

            if (currID == modelFrames->at(x).frameId())
            {
                thisID = true;
                break;
            }
        }
        if (thisID)
        {
            //updateDetailsWindow(ui->listFrameID->currentItem()->text());
        }
        //default is to sort in ascending order
        ui->listFrameID->sortItems();
        ui->lblUniqueID->setText("(" + QString::number(ui->listFrameID->count()) + tr(" unique ids)"));
    }
}

void FrameInfoWindow::updateDetailsWindow(QString newID)
{
    int targettedID;
    int minLen, maxLen, thisLen;
    int64_t avgInterval;
    int64_t minInterval;
    int64_t maxInterval;
    int64_t thisInterval;
    QVector<int> minData, maxData;
    QVector<QVector<int>> dataHistogram;
    QVector<int> bitfieldHistogram;
    QVector<double> histGraphX, histGraphY;
    QVector<double> timeGraphX, timeGraphY;
    QHash<QString, QHash<QString, int>> signalInstances;
    double maxY = -1000.0;
    QVector<uint8_t> changedBits;
    QVector<uint8_t> referenceBits;
    QVector<uint8_t> heatVals;

    //these two used by bitflip heatmap functionality
    QVector<uint8_t> refByte;
    QVector<double> bitFlipHeat;

    QTreeWidgetItem *baseNode, *dataBase, *histBase, *tempItem;

    if (modelFrames->count() == 0) return;

    targettedID = static_cast<int>(Utility::ParseStringToNum(newID));

    qDebug() << "Started update details window with id " << targettedID;

    avgInterval = 0;

    if (targettedID > -1)
    {
        frameCache.clear();
        for (int i = 0; i < modelFrames->count(); i++)
        {
            CANFrame thisFrame = modelFrames->at(i);
            if (thisFrame.frameId() == static_cast<uint32_t>(targettedID)) frameCache.append(thisFrame);
        }

        if (frameCache.count() == 0) return; //nothing to do if there are no frames!

        const unsigned char *data = reinterpret_cast<const unsigned char *>(frameCache.at(0).payload().constData());
        int dataLen = frameCache.at(0).payload().length();

        ui->treeDetails->clear();

        if (frameCache.count() == 0) return;

        baseNode = new QTreeWidgetItem();
        baseNode->setText(0, QString("ID: ") + newID );

        if (frameCache[0].hasExtendedFrameFormat()) //if these frames seem to be extended then try for J1939 decoding
        {
            // ------- J1939 decoding ----------
            J1939ID jid;
            jid.src = targettedID & 0xFF;
            jid.priority = targettedID >> 26;
            jid.pgn = (targettedID >> 8) & 0x3FFFF; //18 bits
            jid.pf = (targettedID >> 16) & 0xFF;
            jid.ps = (targettedID >> 8) & 0xFF;

            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, tr("J1939 decoding"));
            baseNode->addChild(tempItem);

            if (jid.pf > 0xEF)
            {
                jid.isBroadcast = true;
                jid.dest = 0xFFFF;
                tempItem = new QTreeWidgetItem();
                tempItem->setText(0, tr("   Broadcast Frame"));
                baseNode->addChild(tempItem);
            }
            else
            {
                jid.dest = jid.ps;
                tempItem = new QTreeWidgetItem();
                tempItem->setText(0, tr("   Destination ID: ") + Utility::formatNumber(static_cast<uint64_t>(jid.dest)));
                baseNode->addChild(tempItem);
            }
            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, tr("   SRC: ") + Utility::formatNumber(static_cast<uint64_t>(jid.src)));
            baseNode->addChild(tempItem);

            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, tr("   PGN: ") + Utility::formatNumber(static_cast<uint64_t>(jid.pgn)) + "(" + QString::number(jid.pgn) + ")");
            baseNode->addChild(tempItem);

            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, tr("   PF: ") + Utility::formatNumber(static_cast<uint64_t>(jid.pf)));
            baseNode->addChild(tempItem);

            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, tr("   PS: ") + Utility::formatNumber(static_cast<uint64_t>(jid.ps)));
            baseNode->addChild(tempItem);

            // ------- GMLAN 29bit decoding ----------
            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, tr("GMLAN 29bit decoding"));
            baseNode->addChild(tempItem);

            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, tr("   Priority bits: ") + Utility::formatNumber( (uint64_t)FilterUtility::getGMLanPriorityBits(targettedID)));
            baseNode->addChild(tempItem);
            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, tr("   Arbitration Id: ") + Utility::formatNumber( (uint64_t)FilterUtility::getGMLanArbitrationId(targettedID)));
            baseNode->addChild(tempItem);
            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, tr("   Sender Id: ") + Utility::formatNumber( (uint64_t)FilterUtility::getGMLanSenderId(targettedID)));
            baseNode->addChild(tempItem);
        }

        tempItem = new QTreeWidgetItem();
        tempItem->setText(0, tr("# of frames: ") + QString::number(frameCache.count(),10));
        baseNode->addChild(tempItem);

        //clear out all the counters and accumulators
        minLen = 64;
        maxLen = 0;
        minInterval = 0x7FFFFFFF;
        maxInterval = 0;
        minData.fill(256, 64);
        maxData.fill(-1, 64);
        dataHistogram.resize(256);
        for (int i = 0; i < 256; i++) dataHistogram[i].fill(0, 64);
        bitfieldHistogram.fill(0, 512);
        bitFlipHeat.fill(0, 512);
        signalInstances.clear();

        data = reinterpret_cast<const unsigned char *>(frameCache.at(0).payload().constData());
        dataLen = frameCache.at(0).payload().length();

        changedBits.fill(0, dataLen);
        referenceBits.fill(0, dataLen);
        refByte.fill(0, dataLen);
        for (int c = 0; c < dataLen; c++)
        {
            referenceBits[c] = data[c];
            refByte[c] = data[c];
        }

        std::vector<int64_t> sortedIntervals;
        int64_t intervalSum = 0;

        DBC_MESSAGE *msg = dbcHandler->findMessageForFilter(targettedID, nullptr);

        QVector<double> byteGraphX;
        QVector<QVector<double>> byteGraphY;
        byteGraphY.resize(64);

        //then find all data points
        for (int j = 0; j < frameCache.count(); j++)
        {
            data = reinterpret_cast<const unsigned char *>(frameCache.at(j).payload().constData());
            dataLen = frameCache.at(j).payload().length();

            byteGraphX.append(j);
            for (int bytcnt = 0; bytcnt < dataLen && bytcnt < 64; bytcnt++)
            {
                byteGraphY[bytcnt].append(data[bytcnt]);
            }

            if (j != 0)
            {
                if (frameCache[j].timeStamp().microSeconds() > frameCache[j-1].timeStamp().microSeconds())
                    thisInterval = (frameCache[j].timeStamp().microSeconds() - frameCache[j-1].timeStamp().microSeconds());
                else
                    thisInterval = (frameCache[j-1].timeStamp().microSeconds() - frameCache[j].timeStamp().microSeconds());

                sortedIntervals.push_back(thisInterval);
                intervalSum += thisInterval;
                if (thisInterval > maxInterval) maxInterval = thisInterval;
                if (thisInterval < minInterval) minInterval = thisInterval;
                avgInterval += thisInterval;
            }
            thisLen = dataLen;
            if (thisLen > maxLen) maxLen = thisLen;
            if (thisLen < minLen) minLen = thisLen;
            for (int c = 0; c < thisLen; c++)
            {
                unsigned char dat = data[c];
                if (minData[c] > dat) minData[c] = dat;
                if (maxData[c] < dat) maxData[c] = dat;
                dataHistogram[dat][c]++; //add one to count for this
                for (int l = 0; l < 8; l++)
                {
                    int bit = dat & (1 << l);
                    if (bit == (1 << l))
                    {
                        bitfieldHistogram[c * 8 + l]++;
                    }
                }
                changedBits[c] |= referenceBits[c] ^ dat;

                if (refByte[c] != dat) //if this byte doesn't match the value it last had
                {
                    uint8_t newBits = refByte[c] ^ dat; //get changed bits since last ref
                    for (int l = 0; l < 8; l++)
                    {
                        if (newBits & (1 << l)) bitFlipHeat[(c * 8) + l]++;
                    }
                    refByte[c] = dat; //set the reference to this current byte now that processing is done
                }
            }

            //Search every signal in the selected message and give output of the range the signal took and
            //how many messages contained each discrete value.
            if (msg)
            {
                int numSignals = msg->sigHandler->getCount();
                for (int i = 0; i < numSignals; i++)
                {
                    DBC_SIGNAL *sig = msg->sigHandler->findSignalByIdx(i);
                    if (sig)
                    {
                        if (sig->isSignalInMessage(frameCache.at(j)))
                        {
                            QString sigVal;
                            if (sig->processAsText(frameCache.at(j), sigVal, false))
                            {
                                signalInstances[sig->name][sigVal] = signalInstances[sig->name][sigVal] + 1;
                            }
                        }
                    }
                }
            }
        }

        //Divide all the bit flip heat values by the number of frames to get a ratio
        for (int j = 0; j < 512; j++) bitFlipHeat[j] /= (double)frameCache.count();

        std::sort(sortedIntervals.begin(), sortedIntervals.end());
        int64_t intervalStdDiv = 0, intervalPctl5 = 0, intervalPctl95 = 0, intervalMean = 0, intervalVariance = 0;

        int maxTimeCounter = -1;
        if (sortedIntervals.size() > 0)
        {
            intervalMean = intervalSum / sortedIntervals.size();

            for(int l = 0; l < static_cast<int>(sortedIntervals.size()); l++) {
                intervalVariance += ((sortedIntervals[l] - intervalMean) * (sortedIntervals[l] - intervalMean));
            }

            intervalVariance /= sortedIntervals.size();
            intervalStdDiv = static_cast<int>(sqrt(intervalVariance));

            intervalPctl5 = sortedIntervals[static_cast<unsigned int>(floor(0.05 * sortedIntervals.size()))];
            intervalPctl95 = sortedIntervals[static_cast<unsigned int>(floor(0.95 * sortedIntervals.size()))];

            uint64_t step = static_cast<unsigned int>(ceil((maxInterval - minInterval) / numIntervalHistBars));
            qDebug() << "Step: " << step << " minInt: " << minInterval << " maxInt: " << maxInterval;
            unsigned int index = 0;
            int counter = 0;
            for(int l = 0; l <= numIntervalHistBars; l++) {
                int64_t currentMax = maxInterval - ((numIntervalHistBars - l) * step);	// avoid missing the biggest value due to rounding errors
                qDebug() << "CurrentMax: " << currentMax;
                while(index < sortedIntervals.size()) {
                    if(sortedIntervals[index] <= currentMax) {
                        counter++;
                        index++;
                    }
                    else {
                        break;
                    }
                }
                timeGraphX.append(currentMax / 1000.0);
                timeGraphY.append(counter);
                if(counter > maxTimeCounter) maxTimeCounter = counter;
                counter = 0;
            }
        }

        if (frameCache.count() > 1)
            avgInterval = avgInterval / (frameCache.count() - 1);
        else avgInterval = 0;

        //now that data processing is done, create all of our output

        tempItem = new QTreeWidgetItem();

        if (minLen < maxLen)
            tempItem->setText(0, tr("Data Length: ") + QString::number(minLen) + tr(" to ") + QString::number(maxLen));
        else
            tempItem->setText(0, tr("Data Length: ") + QString::number(minLen));

        baseNode->addChild(tempItem);

        tempItem = new QTreeWidgetItem();
        tempItem->setText(0, tr("Average inter-frame interval: ") + QString::number(avgInterval / 1000.0) + "ms");
        baseNode->addChild(tempItem);
        tempItem = new QTreeWidgetItem();
        tempItem->setText(0, tr("Minimum inter-frame interval: ") + QString::number(minInterval / 1000.0) + "ms");
        baseNode->addChild(tempItem);
        tempItem = new QTreeWidgetItem();
        tempItem->setText(0, tr("Maximum inter-frame interval: ") + QString::number(maxInterval / 1000.0) + "ms");
        baseNode->addChild(tempItem);
        tempItem = new QTreeWidgetItem();
        tempItem->setText(0, tr("Inter-frame interval variation: ") + QString::number((maxInterval - minInterval) / 1000.0) + "ms");
        baseNode->addChild(tempItem);
        tempItem = new QTreeWidgetItem();
        tempItem->setText(0, tr("Interval standard deviation: ") + QString::number(intervalStdDiv / 1000.0) + "ms");
        baseNode->addChild(tempItem);
        tempItem = new QTreeWidgetItem();
        tempItem->setText(0, tr("Minimum range to fit 90% of inter-frame intervals: ") + QString::number((intervalPctl95 - intervalPctl5) / 1000.0) + "ms");
        baseNode->addChild(tempItem);

        //display accumulated data for all the bytes in the message
        for (int c = 0; c < maxLen; c++)
        {
            dataBase = new QTreeWidgetItem();
            histBase = new QTreeWidgetItem();

            dataBase->setText(0, tr("Data Byte ") + QString::number(c));
            baseNode->addChild(dataBase);

            tempItem = new QTreeWidgetItem();
            QString builder;
            builder = tr("Changed bits: 0x") + QString::number(changedBits[c], 16) + "  (" + Utility::formatByteAsBinary(changedBits[c]) + ")";
            tempItem->setText(0, builder);
            dataBase->addChild(tempItem);

            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, tr("Range: ") + Utility::formatNumber((unsigned int)minData[c]) + tr(" to ") + Utility::formatNumber((unsigned int)maxData[c]));
            dataBase->addChild(tempItem);
            histBase->setText(0, tr("Histogram"));
            dataBase->addChild(histBase);

            for (int d = 0; d < 256; d++)
            {
                if (dataHistogram[d][c] > 0)
                {
                    tempItem = new QTreeWidgetItem();
                    tempItem->setText(0, QString::number(d) + "/0x" + QString::number(d, 16) +" (" + Utility::formatByteAsBinary(static_cast<uint8_t>(d)) +") -> " + QString::number(dataHistogram[d][c]));
                    histBase->addChild(tempItem);
                }
            }
        }

        dataBase = new QTreeWidgetItem();
        dataBase->setText(0, tr("Bitfield Histogram"));
        for (int c = 0; c < 8 * maxLen; c++)
        {
            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, QString::number(c) + " (Byte " + QString::number(c / 8) + " Bit "
                            + QString::number(c % 8) + ") : " + QString::number(bitfieldHistogram[c]));

            dataBase->addChild(tempItem);
            histGraphX.append(c);
            histGraphY.append(bitfieldHistogram[c]);
            if (bitfieldHistogram[c] > maxY) maxY = bitfieldHistogram[c];
        }
        baseNode->addChild(dataBase);

        //heat map output
        dataBase = new QTreeWidgetItem();
        dataBase->setText(0, tr("Bitchange Heatmap"));
        heatVals.fill(0, 512); //always clear the array before populating it.
        for (int c = 0; c < 8 * maxLen; c++)
        {
            tempItem = new QTreeWidgetItem();
            tempItem->setText(0, QString::number(c) + " (Byte " + QString::number(c / 8) + " Bit "
                            + QString::number(c % 8) + ") : " + QString::number(bitFlipHeat[c] * 100.0, 'f', 2));

            dataBase->addChild(tempItem);
            histGraphX.append(c);
            histGraphY.append(bitfieldHistogram[c]);
            if (bitfieldHistogram[c] > maxY) maxY = bitfieldHistogram[c];
            uint8_t heat = bitFlipHeat[c] * 255;
            if ((heat < 1) && (bitFlipHeat[c] > 0.0001)) heat = 1; //make sure any little bit of heat causes at least some output
            heatVals[c] = heat;
        }
        baseNode->addChild(dataBase);
        heatmap->setHeat(heatVals.data());

        QHash<QString, QHash<QString, int>>::const_iterator it = signalInstances.constBegin();
        while (it != signalInstances.constEnd()) {
            dataBase = new QTreeWidgetItem();
            dataBase->setText(0, it.key());
            QHash<QString,int>::const_iterator itVal = signalInstances[it.key()].constBegin();
            while (itVal != signalInstances[it.key()].constEnd())
            {
                tempItem = new QTreeWidgetItem();
                tempItem->setText(0, itVal.key() + ": " + QString::number(itVal.value()));
                dataBase->addChild(tempItem);
                ++itVal;
            }
            baseNode->addChild(dataBase);
            ++it;
        }

        ui->treeDetails->insertTopLevelItem(0, baseNode);

        graphHistogram->clearGraphs();
        graphHistogram->addGraph();
        graphHistogram->graph()->setData(histGraphX, histGraphY);
        graphHistogram->graph()->setLineStyle(QCPGraph::lsStepLeft); //connect points with lines
        QBrush graphBrush;
        graphBrush.setColor(Qt::red);
        graphBrush.setStyle(Qt::SolidPattern);
        graphHistogram->graph()->setPen(Qt::NoPen);
        graphHistogram->graph()->setBrush(graphBrush);
        graphHistogram->yAxis->setRange(0.8, maxY * 1.2);
        graphHistogram->yAxis->setScaleType(QCPAxis::stLogarithmic);
        graphHistogram->axisRect()->setupFullAxesBox();
        graphHistogram->replot();

        // Clear existing graphs
        for (auto plot : graphByte)
        {
            ui->gridLower->removeWidget(plot);
            delete plot;
        }
        graphByte.clear();
        graphRef.clear();

        // Create new graphs
        for (int graphs = 0; graphs < maxLen && graphs < 64; graphs++)
        {
            QCustomPlot* plot = new QCustomPlot();
            setupByteGraph(plot, graphs);
            ui->gridLower->addWidget(plot, graphs / 4, graphs % 4);
            graphByte.append(plot);

            plot->addGraph();
            graphRef.append(plot->graph());
            plot->graph()->setData(byteGraphX, byteGraphY[graphs]);
            plot->graph()->setPen(bytePens[graphs % 8]); // Reuse colors if more than 8 bytes
            plot->xAxis->setRange(0, byteGraphX.count());
            plot->replot();
        }

        ui->timeHistogram->clearGraphs();
        ui->timeHistogram->addGraph();
        ui->timeHistogram->graph()->setData(timeGraphX, timeGraphY);
        ui->timeHistogram->graph()->setLineStyle(QCPGraph::lsStepLeft); //connect points with lines
        graphBrush.setColor(Qt::red);
        graphBrush.setStyle(Qt::SolidPattern);
        ui->timeHistogram->graph()->setPen(Qt::NoPen);
        ui->timeHistogram->graph()->setBrush(graphBrush);
        ui->timeHistogram->axisRect()->setupFullAxesBox();
        ui->timeHistogram->rescaleAxes();
        ui->timeHistogram->replot();
    }
    else
    {
        // Handle the case when targettedID is not valid
    }

    QSettings settings;
    if (settings.value("InfoCompare/AutoExpand", false).toBool())
    {
        ui->treeDetails->expandAll();
    }
}

void FrameInfoWindow::refreshIDList()
{
    int id;
    for (int i = 0; i < modelFrames->count(); i++)
    {
        CANFrame thisFrame = modelFrames->at(i);
        id = (int)thisFrame.frameId();
        if (!foundID.contains(id))
        {
            foundID.append(id);
            FilterUtility::createFilterItem(id, ui->listFrameID);
        }
    }
    //default is to sort in ascending order
    ui->listFrameID->sortItems();
    ui->lblUniqueID->setText("(" + QString::number(ui->listFrameID->count()) + tr(" unique ids)"));
}

void FrameInfoWindow::saveDetails()
{
    QString filename;
    QFileDialog dialog(this);
    QSettings settings;

    QStringList filters;
    filters.append(QString(tr("Text File (*.txt)")));

    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(filters);
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDirectory(settings.value("FrameInfo/LoadSaveDirectory", dialog.directory().path()).toString());

    if (dialog.exec() == QDialog::Accepted)
    {
        settings.setValue("FrameInfo/LoadSaveDirectory", dialog.directory().path());
        filename = dialog.selectedFiles()[0];
        if (!filename.contains('.')) filename += ".txt";
        if (dialog.selectedNameFilter() == filters[0])
        {
            QFile *outFile = new QFile(filename);

            if (!outFile->open(QIODevice::WriteOnly | QIODevice::Text))
            {
                delete outFile;
                return;
            }

            //go through all IDs, recalculate the data, and then save it to file
            for (int i = 0; i < ui->listFrameID->count(); i++)
            {
                updateDetailsWindow(FilterUtility::getId(ui->listFrameID->item(i)));
                dumpNode(ui->treeDetails->invisibleRootItem(), outFile, 0);
                outFile->write("\n\n");
            }

            outFile->close();
            delete outFile;
        }
    }
}

void FrameInfoWindow::dumpNode(QTreeWidgetItem* item, QFile *file, int indent)
{
    for (int i = 0; i < indent; i++) file->write("\t");
    file->write(item->text(0).toUtf8());
    file->write("\n");
    for( int i = 0; i < item->childCount(); ++i )
        dumpNode( item->child(i), file, indent + 1 );
}
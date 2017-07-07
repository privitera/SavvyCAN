#include <QDateTime>

#include "canconmanager.h"


CANConManager* CANConManager::mInstance = NULL;

CANConManager* CANConManager::getInstance()
{
    if(!mInstance)
        mInstance = new CANConManager();

    return mInstance;
}


CANConManager::CANConManager(QObject *parent): QObject(parent)
{
    connect(&mTimer, SIGNAL(timeout()), this, SLOT(refreshCanList()));
    mTimer.setInterval(20); /*Tick 50 times per second to allow for good resolution in reception where needed. GUI updates *MUCH* more slowly*/
    mTimer.setSingleShot(false);
    mTimer.start();

    mNumActiveBuses = 0;

    resetTimeBasis();
}

void CANConManager::resetTimeBasis()
{
    mTimestampBasis = QDateTime::currentMSecsSinceEpoch() * 1000;
}

CANConManager::~CANConManager()
{
    mTimer.stop();
    mInstance = NULL;
}


void CANConManager::add(CANConnection* pConn_p)
{
    mConns.append(pConn_p);
}


void CANConManager::remove(CANConnection* pConn_p)
{
    //disconnect(pConn_p, 0, this, 0);
    mConns.removeOne(pConn_p);
}

//Get total number of buses currently registered with the program
int CANConManager::getNumBuses()
{
    int buses = 0;
    foreach(CANConnection* conn_p, mConns)
    {
        buses += conn_p->getNumBuses();
    }
    return buses;
}

void CANConManager::refreshCanList()
{
    QObject* sender_p = QObject::sender();

    if( sender_p != &mTimer)
    {
        /* if we are not the sender, the signal is coming from a connection */
        /* refresh only the given connection */
        if(mConns.contains((CANConnection*) sender_p))
            refreshConnection((CANConnection*)sender_p);
    }
    else
    {
        foreach (CANConnection* conn_p, mConns)
            refreshConnection((CANConnection*)conn_p);
    }
}

uint64_t CANConManager::getTimeBasis()
{
    return mTimestampBasis;
}

QList<CANConnection*>& CANConManager::getConnections()
{
    return mConns;
}


CANConnection* CANConManager::getByName(const QString& pName) const
{
    foreach(CANConnection* conn_p, mConns)
    {
        if(conn_p->getPort() == pName)
            return conn_p;
    }

    return NULL;
}


void CANConManager::refreshConnection(CANConnection* pConn_p)
{
    int buses = 0;
    foreach(CANConnection* conn_p, mConns)
    {
        if (conn_p->getStatus() == CANCon::CONNECTED) buses += conn_p->getNumBuses();
    }
    if (buses != mNumActiveBuses)
    {
        mNumActiveBuses = buses;
        emit connectionStatusUpdated(buses);
    }

    if (pConn_p->getQueue().peek() == NULL) return;

    CANFrame* frame_p = NULL;
    QVector<CANFrame> frames;

    //Each connection only knows about its own bus numbers
    //so this variable is used to fix that up to turn local bus numbers
    //into system global bus numbers for display.
    int busBase = 0;

    foreach (CANConnection* conn, mConns)
    {
        if (conn != pConn_p) busBase += conn->getNumBuses();
        else break;
    }

    //qDebug() << "Bus fixup number: " << busBase;

    while( (frame_p = pConn_p->getQueue().peek() ) ) {
        frame_p->bus += busBase;
        //qDebug() << "Rx of frame from bus: " << frame_p->bus;
        frames.append(*frame_p);
        pConn_p->getQueue().dequeue();
    }

    if(frames.size())
        emit framesReceived(pConn_p, frames);
}

/*
 * Uses the requested bus to look up which CANConnection object handles this bus based on the order of
 * the objects and how many buses they implement. For instance, if the request is to send on bus 2
 * and there is a GVRET object first then a socketcan object it'll send on the socketcan object as
 * gvret will have claimed buses 0 and 1 and socketcan bus 2. But, each actual CANConnection expects
 * its own bus numbers to start at zero so the frame bus number has to be offset accordingly.
 * Also keep in mind that the CANConnection "sendFrame" function uses a blocking queued connection
 * and so will force the frame to be delivered before it keeps going. This allows on the stack variables
 * to be used but is slow. This function uses an on the stack copy of the frame so the way it works
 * is a good thing but performance will suffer. TODO: Investigate a way to use non-blocking calls.
*/
bool CANConManager::sendFrame(const CANFrame& pFrame)
{
    int busBase = 0;
    CANFrame workingFrame = pFrame;
    CANFrame *txFrame;

    foreach (CANConnection* conn, mConns)
    {
        //check if this CAN connection is supposed to handle the requested bus
        if (pFrame.bus < (uint32_t)(busBase + conn->getNumBuses()))
        {
            workingFrame.bus -= busBase;
            workingFrame.isReceived = false;
            workingFrame.timestamp = ((QDateTime::currentMSecsSinceEpoch() * 1000) - mTimestampBasis);
            txFrame = conn->getQueue().get();
            *txFrame = workingFrame;
            conn->getQueue().queue();
            return conn->sendFrame(workingFrame);
        }
        busBase += conn->getNumBuses();
    }
    return false;
}

bool CANConManager::sendFrames(const QList<CANFrame>& pFrames)
{
    foreach(const CANFrame& frame, pFrames)
    {
        if(!sendFrame(frame))
            return false;
    }

    return true;
}

//For each device associated with buses go through and see if that device has a bus
//that the filter should apply to. If so forward the data on but fudge
//the bus numbers if bus wasn't -1 so that they're local to the device
bool CANConManager::addTargettedFrame(int pBusId, uint32_t ID, uint32_t mask, QObject *receiver)
{
    int tempBusVal;
    int busBase = 0;

    foreach (CANConnection* conn, mConns)
    {
        if (pBusId == -1) conn->addTargettedFrame(pBusId, ID, mask, receiver);
        else
        {
            tempBusVal = pBusId >> busBase;
            tempBusVal &= ((1 << conn->getNumBuses()) - 1);
            if (tempBusVal) {
                qDebug() << "Forwarding targetted frame setting to a connection object";
                conn->addTargettedFrame(tempBusVal, ID, mask, receiver);
            }
        }
        busBase += conn->getNumBuses();
    }
    return true;
}

bool CANConManager::removeTargettedFrame(int pBusId, uint32_t ID, uint32_t mask, QObject *receiver)
{
    int tempBusVal;
    int busBase = 0;

    foreach (CANConnection* conn, mConns)
    {
        if (pBusId == -1) conn->removeTargettedFrame(pBusId, ID, mask, receiver);
        else
        {
            tempBusVal = pBusId >> busBase;
            tempBusVal &= ((1 << conn->getNumBuses()) - 1);
            if (tempBusVal) conn->removeTargettedFrame(tempBusVal, ID, mask, receiver);
        }
        busBase += conn->getNumBuses();
    }
    return true;
}

bool CANConManager::removeAllTargettedFrames(QObject *receiver)
{
    foreach (CANConnection* conn, mConns)
    {
        conn->removeAllTargettedFrames(receiver);
    }

    return true;
}
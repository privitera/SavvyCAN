#ifndef FRAMEINFOWINDOW_H
#define FRAMEINFOWINDOW_H

#include <QDialog>
#include <QFile>
#include <QListWidget>
#include <QTreeWidget>
#include <candatagrid.h>
#include "can_structs.h"
#include "bus_protocols/j1939_handler.h"
#include "dbc/dbchandler.h"

#include "qcustomplot.h"

namespace Ui {
class FrameInfoWindow;
}

class FrameInfoWindow : public QDialog
{
    Q_OBJECT

public:
    explicit FrameInfoWindow(const QVector<CANFrame> *frames, QWidget *parent = 0);
    ~FrameInfoWindow();
    void showEvent(QShowEvent*);

private slots:
    void updateDetailsWindow(QString);
    void updatedFrames(int);
    void saveDetails();
    void mousePress();
    void mouseWheel();
    void mouseDoubleClick();

private:
    Ui::FrameInfoWindow *ui;
    QVector<QCustomPlot*> graphByte;
    QCustomPlot *graphHistogram;
    CANDataGrid *heatmap;

    QList<int> foundID;
    QList<CANFrame> frameCache;
    const QVector<CANFrame> *modelFrames;
    bool useOpenGL;
    bool useHexTicker;
    static const QColor byteGraphColors[8];
    static QVector<QPen> bytePens;
    DBCHandler *dbcHandler;

    QVector<QCPGraph*> graphRef;

    void refreshIDList();
    void closeEvent(QCloseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);
    void setupByteGraph(QCustomPlot *plot, int num);
    void readSettings();
    void writeSettings();
    void dumpNode(QTreeWidgetItem* item, QFile *file, int indent);
};

#endif // FRAMEINFOWINDOW_H
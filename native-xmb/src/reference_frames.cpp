// Extract timestamped evidence from a user-supplied recording. This reads a file;
// it never captures or controls the recorded application.
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QUrl>
#include <QImage>
#include <QPainter>
#include <QLoggingCategory>
#include <algorithm>

int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()<3 || args.size()>6){qCritical()<<"Usage: xmb-reference-frames recording.mp4 output-directory [interval-ms=200] [start-ms=0] [end-ms=-1]";return 2;}
    bool intervalOk=true,startOk=true,endOk=true;
    const int interval=args.size()>3?args[3].toInt(&intervalOk):200;
    const qint64 start=args.size()>4?args[4].toLongLong(&startOk):0,end=args.size()>5?args[5].toLongLong(&endOk):-1;
    if(!intervalOk || !startOk || !endOk || interval<0 || interval>1000 || start<0 || (end!=-1 && end<=start)){qCritical()<<"Invalid sample interval or time range";return 2;}
    const QDir output(args[2]);if(!QDir().mkpath(output.absolutePath()))return 2;
    QMediaPlayer player;QVideoSink sink;player.setVideoSink(&sink);
    QJsonArray frames;QVector<QImage> thumbnails;
    qint64 last=-1000000;bool finished=false;
    const auto finish=[&](int code) {
        if(finished)return;finished=true;player.stop();
        QFile manifest(output.filePath("frames.json"));
        if(manifest.open(QIODevice::WriteOnly))manifest.write(QJsonDocument(QJsonObject{{"source",args[1]},{"durationMs",double(player.duration())},{"sampleIntervalMs",interval},{"startMs",double(start)},{"endMs",double(end)},{"frames",frames},{"exitCode",code}}).toJson());
        constexpr int columns=5,rows=4;
        for(int start=0;start<thumbnails.size();start+=columns*rows) {
            QImage sheet(columns*320,rows*204,QImage::Format_RGB32);sheet.fill(QColor(20,20,20));QPainter p(&sheet);
            for(int i=start;i<std::min(int(thumbnails.size()),start+columns*rows);++i) {
                const int cell=i-start,x=(cell%columns)*320,y=(cell/columns)*204;
                const auto size=thumbnails[i].size().scaled(320,180,Qt::KeepAspectRatio);
                p.drawImage(QRect(x+(320-size.width())/2,y,size.width(),size.height()),thumbnails[i]);p.setPen(Qt::white);
                p.drawText(QRect(x+6,y+180,308,24),QString::number(frames[i].toObject()["timestampMs"].toDouble()/1000.,'f',2)+"s");
            }
            p.end();sheet.save(output.filePath(QString("sheet-%1.jpg").arg(start/(columns*rows),3,10,QChar('0'))),"JPEG",90);
        }
        qInfo()<<"Reference recording:"<<player.duration()<<"ms; captured"<<frames.size()<<"frames to"<<output.absolutePath();app.exit(code);
    };
    QObject::connect(&sink,&QVideoSink::videoFrameChanged,&app,[&](const QVideoFrame& frame) {
        if(!frame.isValid() || frame.startTime()<0)return;
        if(frame.startTime()/1000<start)return;
        if(end>=0 && frame.startTime()/1000>end){finish(0);return;}
        if(frame.startTime()-last<qint64(interval)*1000)return;
        const auto image=frame.toImage();if(image.isNull())return;
        const bool precise=interval<100;
        const qint64 timestamp=frame.startTime()/1000;const QString name=QString("frame-%1.").arg(timestamp,8,10,QChar('0'))+(precise?"png":"jpg");
        const auto scaled=precise?image:image.scaled(1280,800,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        if(!scaled.save(output.filePath(name),precise?"PNG":"JPEG",precise?-1:94)){finish(2);return;}
        frames.push_back(QJsonObject{{"timestampMs",double(timestamp)},{"file",name},{"width",image.width()},{"height",image.height()}});
        thumbnails.push_back(image.scaled(320,180,Qt::KeepAspectRatio,Qt::SmoothTransformation));last=frame.startTime();
        if(frames.size()>=6000){qCritical()<<"Frame limit reached before the requested range ended";finish(1);}
    });
    QObject::connect(&player,&QMediaPlayer::mediaStatusChanged,&app,[&](QMediaPlayer::MediaStatus status){
        if(status==QMediaPlayer::LoadedMedia){player.setPosition(start);player.setPlaybackRate(interval<100?1.:4.);player.play();}
        if(status==QMediaPlayer::EndOfMedia)finish(0);
    });
    QObject::connect(&player,&QMediaPlayer::errorOccurred,&app,[&](QMediaPlayer::Error,const QString& error){qCritical()<<error;finish(1);});
    QTimer::singleShot(120000,&app,[&]{qCritical()<<"Recording extraction exceeded 120 seconds";finish(1);});
    player.setSource(QUrl::fromLocalFile(args[1]));return app.exec();
}

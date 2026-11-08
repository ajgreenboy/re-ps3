#include "musicvis.h"
#include <QAudioBufferOutput>
#include <QAudioFormat>
#include <QDateTime>
#include <QMediaPlayer>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

namespace xmb {

namespace {
constexpr int fftSize=2048,hop=512;
constexpr double minHz=50.,maxHz=16000.,pi=3.14159265358979323846;
}

// Everything in here runs on the worker thread only.
class MusicTap::Worker : public QObject {
public:
    explicit Worker(MusicTap* owner):owner_(owner),window_(fftSize),ring_(fftSize,0.f),work_(fftSize) {
        for(int i=0;i<fftSize;++i)window_[i]=float(.5-.5*std::cos(2.*pi*i/(fftSize-1)));
    }
    void feed(const QAudioBuffer& buffer) {
        if(!buffer.isValid())return;
        const QAudioFormat format=buffer.format();
        const int channels=std::max(1,format.channelCount()),rate=std::max(8000,format.sampleRate());
        const qsizetype frames=buffer.frameCount();
        if(frames<=0)return;
        if(rate!=rate_){rate_=rate;buildBands();}
        const qint64 startMs=buffer.startTime()>=0?buffer.startTime()/1000:0;
        for(qsizetype f=0;f<frames;++f) {
            float sum=0.f;
            for(int c=0;c<channels;++c)sum+=sampleAt(buffer,format.sampleFormat(),f*channels+c);
            ring_[write_]=sum/float(channels);
            write_=(write_+1)%fftSize;
            if(filled_<fftSize){++filled_;continue;}
            if(++sinceHop_>=hop) {
                sinceHop_=0;
                // The window ends at this frame; centre its time half a window earlier.
                const qint64 endMs=startMs+qint64(f)*1000/rate;
                analyse(endMs-qint64(fftSize/2)*1000/rate);
            }
        }
    }
private:
    static float sampleAt(const QAudioBuffer& b,QAudioFormat::SampleFormat fmt,qsizetype i) {
        switch(fmt) {
        case QAudioFormat::UInt8:return (b.constData<quint8>()[i]-128)/128.f;
        case QAudioFormat::Int16:return b.constData<qint16>()[i]/32768.f;
        case QAudioFormat::Int32:return float(b.constData<qint32>()[i]/2147483648.);
        case QAudioFormat::Float:return b.constData<float>()[i];
        default:return 0.f;
        }
    }
    void buildBands() {
        const double binHz=double(rate_)/fftSize,top=std::min(maxHz,rate_/2.-binHz);
        edges_.assign(MusicSpectrum::bands,{0,0});
        centres_.assign(MusicSpectrum::bands,0.f);
        for(int b=0;b<MusicSpectrum::bands;++b) {
            const double f0=minHz*std::pow(top/minHz,double(b)/MusicSpectrum::bands);
            const double f1=minHz*std::pow(top/minHz,double(b+1)/MusicSpectrum::bands);
            int lo=std::max(1,int(std::floor(f0/binHz))),hi=std::max(lo,int(std::ceil(f1/binHz))-1);
            hi=std::min(hi,fftSize/2-1);lo=std::min(lo,hi);
            edges_[b]={lo,hi};
            centres_[b]=float(std::sqrt(f0*f1));
        }
    }
    void fft() {   // in-place iterative radix-2 on work_
        for(int i=1,j=0;i<fftSize;++i) {
            int bit=fftSize>>1;
            for(;j&bit;bit>>=1)j^=bit;
            j^=bit;
            if(i<j)std::swap(work_[i],work_[j]);
        }
        for(int len=2;len<=fftSize;len<<=1) {
            const double angle=-2.*pi/len;
            const std::complex<float> step(float(std::cos(angle)),float(std::sin(angle)));
            for(int i=0;i<fftSize;i+=len) {
                std::complex<float> w(1.f,0.f);
                for(int k=0;k<len/2;++k) {
                    const auto u=work_[i+k],v=work_[i+k+len/2]*w;
                    work_[i+k]=u+v;work_[i+k+len/2]=u-v;
                    w*=step;
                }
            }
        }
    }
    void analyse(qint64 centreMs) {
        double energy=0.;
        for(int i=0;i<fftSize;++i) {
            const float s=ring_[(write_+i)%fftSize];   // oldest first
            work_[i]={s*window_[i],0.f};
        }
        // Loudness over the newest hop, not the whole window.
        for(int i=fftSize-hop;i<fftSize;++i){const float s=ring_[(write_+i)%fftSize];energy+=double(s)*s;}
        fft();
        MusicSpectrum out;
        for(int b=0;b<MusicSpectrum::bands;++b) {
            float peak=0.f;
            for(int k=edges_[b].first;k<=edges_[b].second;++k)peak=std::max(peak,std::abs(work_[k]));
            // A full-scale sine under a Hann window reads N/4 in its bin.
            const double magnitude=peak/(fftSize/4.);
            double db=20.*std::log10(magnitude+1e-9);
            db+=2.*std::log2(std::max(50.f,centres_[b])/1000.);   // +2 dB/octave tilt so treble is not flat-lined
            out.band[b]=float(std::clamp((db+62.)/54.,0.,1.));
        }
        const double rms=std::sqrt(energy/hop);
        out.rms=float(std::clamp((20.*std::log10(rms+1e-9)+50.)/45.,0.,1.));
        out.timeMs=centreMs;out.valid=true;
        owner_->push(out);
    }
    MusicTap* owner_;
    std::vector<float> window_,ring_;
    std::vector<std::complex<float>> work_;
    std::vector<std::pair<int,int>> edges_;
    std::vector<float> centres_;
    int rate_=0,write_=0,filled_=0,sinceHop_=0;
};

MusicTap::MusicTap(QMediaPlayer* player,QObject* parent):QObject(parent),player_(player) {
    worker_=new Worker(this);
    worker_->moveToThread(&thread_);
    thread_.setObjectName(QStringLiteral("music-spectrum"));
    thread_.start(QThread::LowPriority);
    // Default-constructed format = the stream's own; every sample format is handled in Worker::sampleAt.
    output_=new QAudioBufferOutput(this);
    player_->setAudioBufferOutput(output_);
    // Direct: runs on the decoder's thread, only to count and forward; the FFT is on the worker.
    connect(output_,&QAudioBufferOutput::audioBufferReceived,this,[this](const QAudioBuffer& buffer) {
        if(!wanted())return;
        if(pending_.load()>6){++dropped_;return;}   // bound the queue: a slow worker drops buffers, never grows
        ++pending_;
        QMetaObject::invokeMethod(worker_,[this,buffer]{worker_->feed(buffer);--pending_;++analysed_;},Qt::QueuedConnection);
    },Qt::DirectConnection);
}
MusicTap::~MusicTap() {
    player_->setAudioBufferOutput(nullptr);
    disconnect(output_,nullptr,this,nullptr);
    thread_.quit();thread_.wait();
    delete worker_;
}
void MusicTap::request() { requestedAt_=QDateTime::currentMSecsSinceEpoch(); }
bool MusicTap::wanted() const { return QDateTime::currentMSecsSinceEpoch()-requestedAt_.load()<500; }
void MusicTap::push(const MusicSpectrum& spectrum) {
    QMutexLocker lock(&mutex_);
    // A jump backwards in media time is a seek or a new track: older windows no longer describe what plays.
    if(!history_.empty() && spectrum.timeMs+250<history_.back().timeMs)history_.clear();
    history_.push_back(spectrum);
    while(history_.size()>maxHistory)history_.pop_front();
}
void MusicTap::reset() { QMutexLocker lock(&mutex_);history_.clear(); }
bool MusicTap::sample(qint64 positionMs,MusicSpectrum& out) const {
    QMutexLocker lock(&mutex_);
    if(history_.empty())return false;
    const MusicSpectrum* best=&history_.back();
    qint64 bestDistance=std::numeric_limits<qint64>::max();
    for(const auto& s:history_) {
        const qint64 d=std::llabs(s.timeMs-positionMs);
        if(d<bestDistance){bestDistance=d;best=&s;}
    }
    if(bestDistance>400)best=&history_.back();
    out=*best;
    return true;
}

}

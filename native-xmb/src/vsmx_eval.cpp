#include "vsmx_eval.h"
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>

namespace xmb {
QVector4D sample(const VsmxTimeline& timeline,const QString& actor,VsmxKey::Kind kind,double t) {
    const auto keys=timeline.actors.value(actor);
    const VsmxKey* previous=nullptr;
    for(const auto& key:keys)if(key.kind==kind) {
        if(key.time>t) {
            if(!previous)return key.value;
            const double span=key.time-previous->time;
            return span>0?previous->value+(key.value-previous->value)*float((t-previous->time)/span):key.value;
        }
        previous=&key;
    }
    return previous?previous->value:QVector4D();
}

bool evalVsmx(const QByteArray& data,VsmxTimeline* out,QString* error) {
    const auto fail=[&](const QString& message){if(error)*error=message;return false;};
    if(!out || data.size()<0x34 || !data.startsWith("VSMX"))return fail(QStringLiteral("Invalid VSMX header"));
    const auto le=[&](qsizetype p){return qFromLittleEndian<quint32>(data.constData()+p);};
    const auto valid=[&](quint32 p,quint32 n){return quint64(p)+n<=quint64(data.size());};
    const quint32 code=le(8),size=le(12);
    if(!valid(code,size) || size%8)return fail(QStringLiteral("Invalid VSMX code region"));
    const auto table=[&](int p,bool wide,QStringList& result) {
        const quint32 off=le(p),len=le(p+4);
        if(!valid(off,len) || (wide && len%2))return false;
        if(!wide){for(const auto& s:data.mid(off,len).split('\0'))result.append(QString::fromLatin1(s));}
        else {QString s;for(quint32 i=0;i<len;i+=2){const ushort c=qFromLittleEndian<quint16>(data.constData()+off+i);if(c)s.append(QChar(c));else {result.append(s);s.clear();}}if(!s.isEmpty())result.append(s);}
        return true;
    };
    QStringList names,props,text;
    if(!table(0x10,true,text) || !table(0x1c,true,props) || !table(0x28,false,names))return fail(QStringLiteral("Invalid VSMX string table"));
    *out={};
    struct Value { enum Type { Unknown, Number, String, Ref, Object, Array, Function, Method } type=Unknown; double n=0; QString s,property; QVector<double> array; quint32 target=0; };
    const auto number=[](double n){Value v;v.type=Value::Number;v.n=n;return v;};
    QMap<QString,Value> vars;
    const auto resolve=[&](Value v){if(v.type==Value::Ref && vars.contains(v.s))return vars.value(v.s);return v;};
    const auto note=[&](const QString& s){if(!out->notEvaluated.contains(s))out->notEvaluated.append(s);};
    note(QStringLiteral("System.resolution assumed 1080 (HD); scene properties and non-linear easing are not evaluated"));
    const auto key=[&](const QString& actor,VsmxKey::Kind kind,double time,const QVector4D& value){out->actors[actor].append({kind,time,value});std::stable_sort(out->actors[actor].begin(),out->actors[actor].end(),[](const VsmxKey& a,const VsmxKey& b){return a.time<b.time;});};
    int budget=100000;
    std::function<bool(quint32,double,int)> run;
    run=[&](quint32 pc,double now,int depth)->bool {
        if(depth>32)return false;
        QVector<Value> stack;QMap<QString,Value> locals;
        const auto pop=[&](){if(stack.isEmpty())return Value{};return stack.takeLast();};
        while(pc<size/8 && --budget>0) {
            const quint32 id=le(code+pc*8),v=le(code+pc*8+4),op=id&255; ++pc;
            if(op==0x22){stack.clear();continue;}
            if(op==0x45 || op==0x3f)return true;
            if(op==0x46 || op==0x47 || op==0x4a) {if(op==0x4a)note(QStringLiteral("Opcode 0x4a treated as a no-op for the HD resolution test"));continue;}
            if(op==0x26 || op==0x25){stack.append(number(op==0x25?double(v!=0):double(qint32(v))));continue;}
            if(op==0x27){stack.append(number(std::bit_cast<float>(v)));continue;}
            if(op==0x23){stack.append(Value{});continue;}
            if(op==0x28 || op==0x2e || op==0x2d) {
                Value x;if(op==0x2d){x.type=Value::Ref;x.s=QStringLiteral("#arg%1").arg(v);if(locals.contains(x.s))x=locals.value(x.s);}
                else {const auto& tab=op==0x28?text:names;if(v>=quint32(tab.size()))return false;x.type=op==0x28?Value::String:Value::Ref;x.s=tab[int(v)];}stack.append(x);continue;
            }
            if(op==0x2a){Value x;x.type=Value::Function;x.target=v;stack.append(x);continue;}
            if(op==0x20){if(stack.isEmpty())return false;stack.append(stack.last());continue;}
            if(op==0x49){if(v>quint32(stack.size()))return false;Value x;x.type=Value::Array;x.array.resize(int(v));for(int i=int(v)-1;i>=0;--i){const Value a=resolve(pop());if(a.type!=Value::Number)x.type=Value::Unknown;else x.array[i]=a.n;}stack.append(x);continue;}
            if(op==0x2f || op==0x30){if(v>=quint32(props.size()) || stack.isEmpty())return false;Value a=resolve(pop()),x;const QString p=props[int(v)];if(a.type==Value::Ref && a.s==QStringLiteral("System") && p==QStringLiteral("resolution"))x=number(1080);else {x.type=op==0x30?Value::Method:Value::Unknown;x.s=a.s;x.property=p;}stack.append(x);continue;}
            if(op==0x01){if(stack.size()<2)return false;Value b=resolve(pop()),a=pop();if(a.type==Value::Ref){if(a.s.startsWith(QLatin1Char('#')))locals[a.s]=b;else {vars[a.s]=b;if(b.type==Value::Number)out->constants[a.s]=b.n;else if(b.type==Value::String)out->strings[a.s]=b.s;}}stack.append(b);continue;}
            if(op==0x31){if(v>=quint32(props.size()) || stack.size()<2)return false;Value b=resolve(pop()),a=resolve(pop());if(a.type==Value::Object && b.type==Value::Array && props[int(v)]==QStringLiteral("color") && b.array.size()==4){const QVector4D col(float(b.array[0]),float(b.array[1]),float(b.array[2]),float(b.array[3]));key(a.s,VsmxKey::Colour,now,col);key(a.s,VsmxKey::Alpha,now,QVector4D(0,0,0,col.w()));}stack.append(b);continue;}
            if(op==0x08 || op==0x07 || op==0x09){if(stack.isEmpty())return false;Value a=resolve(pop());if(a.type==Value::Number)a.n=op==8?-a.n:op==9?!a.n:a.n;stack.append(a);continue;}
            if((op>=2 && op<=6) || (op>=0x0e && op<=0x15)){if(stack.size()<2)return false;const Value b=resolve(pop()),a=resolve(pop());Value x;if(a.type==Value::Number && b.type==Value::Number){double n=0;switch(op){case 2:n=a.n+b.n;break;case 3:n=a.n-b.n;break;case 4:n=a.n*b.n;break;case 5:if(b.n==0)break;n=a.n/b.n;break;case 6:if(b.n==0)break;n=std::fmod(a.n,b.n);break;case 0x0e:case 0x10:n=a.n==b.n;break;case 0x0f:case 0x11:n=a.n!=b.n;break;case 0x12:n=a.n<b.n;break;case 0x13:n=a.n<=b.n;break;case 0x14:n=a.n>=b.n;break;case 0x15:n=a.n>b.n;break;}x=number(n);}stack.append(x);continue;}
            if(op==0x39){if(v>=size/8)return false;pc=v;continue;}
            if(op==0x3a || op==0x3b){if(stack.isEmpty())return false;const Value a=resolve(pop());if(a.type!=Value::Number){note(QStringLiteral("Unresolved conditional at instruction %1; execution stopped").arg(pc-1));return true;}if((a.n!=0)==(op==0x3a)){if(v>=size/8)return false;pc=v;}continue;}
            if(op==0x3c || op==0x3d || op==0x3e){if(v>quint32(stack.size()) || stack.size()<qsizetype(v)+1)return false;QVector<Value> args(int(v),Value{});for(int i=int(v)-1;i>=0;--i)args[i]=resolve(pop());const Value fn=resolve(pop());Value result;
                if(op==0x3e && fn.type==Value::Ref && (fn.s==QStringLiteral("Actor") || fn.s==QStringLiteral("Light") || fn.s==QStringLiteral("Camera")) && !args.isEmpty() && args[0].type==Value::String){result.type=Value::Object;result.s=args[0].s;}
                else if(op==0x3e && fn.type==Value::Ref && fn.s==QStringLiteral("OneShotTimer") && args.size()==2 && args[0].type==Value::Number && args[1].type==Value::Function){if(!run(args[1].target,now+args[0].n,depth+1))return false;}
                else if(op==0x3d && fn.type==Value::Method && args.size()>=2 && args[0].type==Value::Array && args[1].type==Value::Number && !fn.s.isEmpty()){
                    VsmxKey::Kind kind=VsmxKey::Colour;bool recognized=true;if(fn.property==QStringLiteral("setColor"))kind=VsmxKey::Colour;else if(fn.property==QStringLiteral("setPosition"))kind=VsmxKey::Position;else if(fn.property==QStringLiteral("setScale"))kind=VsmxKey::Scale;else recognized=false;
                    if(recognized && args[1].n>=0 && args[0].array.size()<=4){QVector4D val;for(int i=0;i<args[0].array.size();++i)val[i]=float(args[0].array[i]);const auto add=[&](VsmxKey::Kind k,const QVector4D& target){const QVector4D initial=sample(*out,fn.s,k,now);key(fn.s,k,now,initial);key(fn.s,k,now+args[1].n,target);};add(kind,val);if(kind==VsmxKey::Colour && args[0].array.size()==4)add(VsmxKey::Alpha,QVector4D(0,0,0,val.w()));if(args.size()>2)note(QStringLiteral("Interpolation curves approximated linearly"));}
                    else note(QStringLiteral("Unsupported method %1 at instruction %2").arg(fn.property).arg(pc-1));
                } else note(QStringLiteral("Unsupported call at instruction %1").arg(pc-1));stack.append(result);continue;}
            if(op==0x36){if(stack.size()<3)return false;pop();pop();pop();stack.append(Value{});continue;}
            note(QStringLiteral("Unsupported opcode 0x%1 at instruction %2; execution stopped").arg(op,2,16,QChar('0')).arg(pc-1));return true;
        }
        return budget>0;
    };
    if(!run(0,0,0))return fail(QStringLiteral("Invalid VSMX execution or instruction limit exceeded"));
    return true;
}
}

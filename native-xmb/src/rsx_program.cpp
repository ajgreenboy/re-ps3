#include "rsx_program.h"
#include <QtEndian>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace xmb {
namespace {
const char* const kSwizzle="xyzw";
std::string swizzle4(quint32 x,quint32 y,quint32 z,quint32 w) {
    if(x==0 && y==1 && z==2 && w==3)return {};
    return std::string(".")+kSwizzle[x]+kSwizzle[y]+kSwizzle[z]+kSwizzle[w];
}
std::string writeMask(bool x,bool y,bool z,bool w) {
    std::string m;if(x)m+='x';if(y)m+='y';if(z)m+='z';if(w)m+='w';return m;
}
// Condition codes: lt=1 eq=2 gt=4
std::string compareCc(quint32 cond,const std::string& cc) {
    static const char* const fn[8]={"","lessThan","equal","lessThanEqual","greaterThan","notEqual","greaterThanEqual",""};
    return std::string(fn[cond&7])+"("+cc+", vec4(0.))";
}
void assign(std::string& out,const std::string& dest,const std::string& mask,const std::string& value,const std::string& cond) {
    if(mask.empty())return;
    if(cond.empty()){out+="  "+dest+"."+mask+" = ("+value+")."+mask+";\n";return;}
    out+="  { vec4 t_ = vec4("+value+"); bvec4 c_ = "+cond+";";
    for(char c:mask)out+=std::string(" if (c_.")+c+") "+dest+"."+c+" = t_."+c+";";
    out+=" }\n";
}
const char* kLit="vec4 lit_(vec4 v) { vec4 c = v; c.x = max(v.x, 0.); c.y = max(v.y, 0.);"
                 " return vec4(1., c.x, c.x > 0. ? exp(c.w * log(max(c.y, 1e-10))) : 0., 1.); }\n";

// Vertex program: 4 big-endian words per instruction.
std::string translateVertex(const std::vector<quint32>& w,quint32& inputs,int& count) {
    std::string body;int maxConst=0;bool indexed=false;
    for(size_t i=0;i+3<w.size();i+=4) {
        const quint32 d0=w[i],d1=w[i+1],d2=w[i+2],d3=w[i+3];
        const quint32 srcBits[3]={((d2>>23)&0x1FF)|((d1&0xFF)<<9),(d2>>6)&0x1FFFF,((d3>>21)&0x7FF)|((d2&0x3F)<<11)};
        const bool end=d3&1;const quint32 vecOp=(d1>>22)&0x1F,scaOp=(d1>>27)&0x1F;
        ++count;
        if(!(srcBits[0]&3) || !(srcBits[1]&3) || !(srcBits[2]&3))break;
        const quint32 inputSrc=(d1>>8)&0xF,constSrc=(d1>>12)&0x3FF;const bool indexConst=(d3>>1)&1;
        auto src=[&](int n){
            const quint32 s=srcBits[n];std::string r;
            switch(s&3) {
            case 1:r="r["+std::to_string((s>>2)&0x3F)+"]";break;
            case 2:r="in_"+std::to_string(inputSrc);inputs|=1u<<inputSrc;break;
            default:
                if(indexConst){indexed=true;r="vc[clamp(a["+std::to_string((d0>>24)&1)+"]."+kSwizzle[d0&3]+" + "+std::to_string(constSrc)+", 0, 467)]";}
                else if(constSrc<468){maxConst=std::max(maxConst,int(constSrc));r="vc["+std::to_string(constSrc)+"]";}
                else r="vec4(0.)";
                break;
            }
            r+=swizzle4((s>>14)&3,(s>>12)&3,(s>>10)&3,(s>>8)&3);
            if((d0>>(21+n))&1)r="abs("+r+")";
            if((s>>16)&1)r="-"+r;
            return "("+r+")";
        };
        const quint32 cond=(d0>>10)&7;const bool condTest=(d0>>13)&1;
        const std::string cc="cc["+std::to_string((d0>>25)&1)+"]"+swizzle4((d0>>8)&3,(d0>>6)&3,(d0>>4)&3,(d0>>2)&3);
        const std::string cnd=(condTest && cond!=7)?compareCc(cond,cc):"";
        const bool sat=(d0>>26)&1,vecResult=(d0>>30)&1;const quint32 dstOut=(d3>>2)&0x1F;
        auto setDst=[&](bool sca,std::string value,bool address=false){
            if(cond==0)return;
            const std::string mask=sca?writeMask((d3>>20)&1,(d3>>19)&1,(d3>>18)&1,(d3>>17)&1):writeMask((d3>>16)&1,(d3>>15)&1,(d3>>14)&1,(d3>>13)&1);
            if(sat)value="clamp("+value+", 0., 1.)";
            const quint32 tmp=sca?(d3>>7)&0x3F:(d0>>15)&0x3F;const bool result=sca?!vecResult:vecResult;bool wrote=false;
            if(result && dstOut!=0x1F && dstOut<16){assign(body,"o["+std::to_string(dstOut)+"]",mask,value,cnd);wrote=true;}
            if(tmp!=0x3F){
                if(address)assign(body,"a["+std::to_string((d0>>24)&1)+"]",mask,"ivec4("+value+")",cnd);
                else assign(body,"r["+std::to_string(tmp)+"]",mask,value,cnd);
                wrote=true;
            }
            if(!wrote && (!result || ((d0>>14)&1) || ((d0>>29)&1)))assign(body,"cc["+std::to_string((d0>>25)&1)+"]",mask,value,cnd);
        };
        const std::string s0=src(0),s1=src(1),s2=src(2);
        switch(vecOp) {
        case 0x00:break;
        case 0x01:setDst(false,s0);break;
        case 0x02:setDst(false,s0+" * "+s1);break;
        case 0x03:setDst(false,s0+" + "+s2);break;
        case 0x04:setDst(false,"(("+s0+") * ("+s1+") + ("+s2+"))");break;
        case 0x05:setDst(false,"vec4(dot("+s0+".xyz, "+s1+".xyz))");break;
        case 0x06:setDst(false,"vec4(dot(vec4("+s0+".xyz, 1.), "+s1+"))");break;
        case 0x07:setDst(false,"vec4(dot("+s0+", "+s1+"))");break;
        case 0x08:setDst(false,"vec4(1., "+s0+".y * "+s1+".y, "+s0+".z, "+s1+".w)");break;
        case 0x09:setDst(false,"min("+s0+", "+s1+")");break;
        case 0x0A:setDst(false,"max("+s0+", "+s1+")");break;
        case 0x0B:setDst(false,"vec4(lessThan("+s0+", "+s1+"))");break;
        case 0x0C:setDst(false,"vec4(greaterThanEqual("+s0+", "+s1+"))");break;
        case 0x0D:setDst(false,"floor("+s0+")",true);break;
        case 0x0E:setDst(false,"fract("+s0+")");break;
        case 0x0F:setDst(false,"floor("+s0+")");break;
        case 0x10:setDst(false,"vec4(equal("+s0+", "+s1+"))");break;
        case 0x11:setDst(false,"vec4(0.)");break;
        case 0x12:setDst(false,"vec4(greaterThan("+s0+", "+s1+"))");break;
        case 0x13:setDst(false,"vec4(lessThanEqual("+s0+", "+s1+"))");break;
        case 0x14:setDst(false,"vec4(notEqual("+s0+", "+s1+"))");break;
        case 0x15:setDst(false,"vec4(1.)");break;
        case 0x16:setDst(false,"sign("+s0+")");break;
        default:body+="  // unsupported vector op "+std::to_string(vecOp)+"\n";break;
        }
        switch(scaOp) {
        case 0x00:break;
        case 0x01:setDst(true,s2);break;
        case 0x02:setDst(true,"1.0 / "+s2);break;
        case 0x03:setDst(true,"clamp(1.0 / "+s2+", 5.42101e-20, 1.884467e19)");break;
        case 0x04:setDst(true,"vec4(1. / sqrt(max("+s2+".x, 1e-10)))");break;
        case 0x05:setDst(true,"exp("+s2+")");break;
        case 0x06:setDst(true,"log("+s2+")");break;
        case 0x07:setDst(true,"lit_("+s2+")");break;
        case 0x0D:setDst(true,"log2(max("+s2+", 1e-10))");break;
        case 0x0E:setDst(true,"exp2("+s2+")");break;
        case 0x0F:setDst(true,"sin("+s2+")");break;
        case 0x10:setDst(true,"cos("+s2+")");break;
        default:body+="  // unsupported scalar/flow op "+std::to_string(scaOp)+"\n";break;
        }
        if(end)break;
    }
    std::string g="#version 330 core\nuniform vec4 vc["+std::to_string(indexed?468:maxConst+1)+"];\n";
    for(int i=0;i<16;++i)if(inputs&(1u<<i))g+="layout(location="+std::to_string(i)+") in vec4 in_"+std::to_string(i)+";\n";
    g+="out vec4 v_col0, v_col1, v_fog;\nout vec4 v_tc[10];\n";
    g+=kLit;
    g+="void main() {\n  vec4 r[32]; ivec4 a[2] = ivec4[2](ivec4(0), ivec4(0)); vec4 cc[2] = vec4[2](vec4(0.), vec4(0.)); vec4 o[16];\n"
       "  for (int i = 0; i < 32; i++) r[i] = vec4(0.);\n  for (int i = 0; i < 16; i++) o[i] = vec4(0., 0., 0., 1.);\n";
    g+=body;
    // RSX output registers: 0 position, 1/2 colours, 5 fog, 7-14 TEXCOORD0-7, 15 TEXCOORD8, 6 TEXCOORD9
    g+="  gl_Position = o[0];\n  v_col0 = o[1]; v_col1 = o[2]; v_fog = o[5];\n"
       "  for (int i = 0; i < 8; i++) v_tc[i] = o[7 + i];\n  v_tc[8] = o[15]; v_tc[9] = o[6];\n}\n";
    return g;
}

// Fragment program: words are stored with their 16-bit halves swapped; an instruction that reads a
// constant is followed by a 16-byte literal slot.
quint32 fpWord(const QByteArray& u,int at) { const quint32 v=qFromBigEndian<quint32>(u.constData()+at);return (v<<16)|(v>>16); }

std::string translateFragment(const QByteArray& u,bool outputFromH0,bool depthReplace,std::map<int,int>& slotToConstant,QVector<QVector4D>& constants,quint32& samplers,int& count) {
    std::string body;std::vector<std::pair<int,std::string>> closers;int loopDepth=0;bool needs400=false;
    for(int pc=0;pc*16+16<=u.size();++pc) {
        for(auto it=closers.begin();it!=closers.end();) {
            if(it->first==pc){body+=it->second;if(it->second.find("loop")!=std::string::npos)--loopDepth;it=closers.erase(it);} else ++it;
        }
        const int base=pc*16;
        const quint32 dst=fpWord(u,base),s0w=fpWord(u,base+4),s1w=fpWord(u,base+8),s2w=fpWord(u,base+12);
        const quint32 opcode=((dst>>24)&0x3F)|((s1w>>31)<<6);const bool end=dst&1;
        const bool hasConst=((s0w&3)==2) || ((s1w&3)==2) || ((s2w&3)==2);const bool flow=opcode>=0x40;
        ++count;
        std::string fc;
        if(hasConst && !flow && base+32<=u.size()) {
            const int index=constants.size();fc="fc["+std::to_string(index)+"]";slotToConstant[base+16]=index;
            float f[4];for(int k=0;k<4;++k){const quint32 bits=fpWord(u,base+16+k*4);std::memcpy(&f[k],&bits,4);}
            constants.push_back(QVector4D(f[0],f[1],f[2],f[3]));
        }
        const quint32 prec=(s1w>>19)&0x1FF;
        auto src=[&](quint32 s,int n){
            std::string r;
            switch(s&3) {
            case 0:r=std::string(((s>>8)&1)?"h[":"r[")+std::to_string((s>>2)&0x3F)+"]";break;
            case 1:{
                const quint32 id=((s2w>>30)&1)?((s2w>>19)&0x7FF)+4:(dst>>13)&0xF;
                switch(id) {
                case 0:r="gl_FragCoord";break;
                case 1:r="clamp(v_col0, 0., 1.)";break;
                case 2:r="clamp(v_col1, 0., 1.)";break;
                case 3:r="v_fog";break;
                case 14:r="vec4(gl_FrontFacing ? 1. : -1.)";break;
                default:
                    r=id<14?"v_tc["+std::to_string(id-4)+"]":"vec4(0.)";
                    if(id>=4 && id<14 && ((s2w>>31)&1))r="("+r+" * gl_FragCoord.w)";
                    break;
                }
                break;
            }
            case 2:r=fc.empty()?"vec4(0.)":fc;break;
            default:r="vec4(0.)";break;
            }
            r+=swizzle4((s>>9)&3,(s>>11)&3,(s>>13)&3,(s>>15)&3);
            const bool abs=n==0?(s0w>>29)&1:(s>>18)&1;
            if(abs)r="abs("+r+")";
            if(((prec>>(n*3))&7)==4)r="clamp("+r+", 0., 1.)";
            if((s>>17)&1)r="-"+r;
            return "("+r+")";
        };
        const bool lt=(s0w>>18)&1,eq=(s0w>>19)&1,gt=(s0w>>20)&1;
        const quint32 cond=(lt?1:0)|(eq?2:0)|(gt?4:0);
        const std::string cc="cc["+std::to_string((s0w>>31)&1)+"]"+swizzle4((s0w>>21)&3,(s0w>>23)&3,(s0w>>25)&3,(s0w>>27)&3);
        const std::string cnd=cond==7?"":compareCc(cond,cc),anyCnd=cond==7?"true":"any("+cnd+")";
        const std::string mask=writeMask((dst>>9)&1,(dst>>10)&1,(dst>>11)&1,(dst>>12)&1);
        const quint32 tex=(dst>>17)&0xF;
        auto setDst=[&](std::string value){
            if(cond==0)return;
            switch((s1w>>28)&7) {
            case 1:value="("+value+") * 2.";break;case 2:value="("+value+") * 4.";break;case 3:value="("+value+") * 8.";break;
            case 5:value="("+value+") / 2.";break;case 6:value="("+value+") / 4.";break;case 7:value="("+value+") / 8.";break;
            default:break;
            }
            if((dst>>31)&1)value="clamp("+value+", 0., 1.)";
            const std::string ccDst="cc["+std::to_string((s0w>>30)&1)+"]";
            if((dst>>30)&1){if((dst>>8)&1)assign(body,ccDst,mask,value,cnd);return;}
            const std::string d=std::string(((dst>>7)&1)?"h[":"r[")+std::to_string((dst>>1)&0x3F)+"]";
            assign(body,d,mask,value,cnd);
            if((dst>>8)&1)assign(body,ccDst,mask,d,"");
        };
        auto sample=[&](const std::string& kind){
            samplers|=1u<<tex;
            const std::string s="tex"+std::to_string(tex),c=src(s0w,0);std::string v;
            if(kind=="proj")v="texture("+s+", "+c+".xy / "+c+".w)";
            else if(kind=="lod")v="textureLod("+s+", "+c+".xy, "+c+".w)";
            else if(kind=="bias")v="texture("+s+", "+c+".xy, "+c+".w)";
            else if(kind=="grad")v="textureGrad("+s+", "+c+".xy, "+src(s1w,1)+".xy, "+src(s2w,2)+".xy)";
            else v="texture("+s+", "+c+".xy)";
            if((dst>>21)&1)v="("+v+" * 2. - 1.)";
            setDst(v);
        };
        const std::string a=src(s0w,0),b=src(s1w,1),c=src(s2w,2);
        switch(opcode) {
        case 0x00:break;
        case 0x01:setDst(a);break;
        case 0x02:setDst(a+" * "+b);break;
        case 0x03:setDst(a+" + "+b);break;
        case 0x04:setDst("(("+a+") * ("+b+") + ("+c+"))");break;
        case 0x05:setDst("vec4(dot("+a+".xyz, "+b+".xyz))");break;
        case 0x06:setDst("vec4(dot("+a+", "+b+"))");break;
        case 0x07:setDst("vec4(1., "+a+".y * "+b+".y, "+a+".z, "+b+".w)");break;
        case 0x08:setDst("min("+a+", "+b+")");break;
        case 0x09:setDst("max("+a+", "+b+")");break;
        case 0x0A:setDst("vec4(lessThan("+a+", "+b+"))");break;
        case 0x0B:setDst("vec4(greaterThanEqual("+a+", "+b+"))");break;
        case 0x0C:setDst("vec4(lessThanEqual("+a+", "+b+"))");break;
        case 0x0D:setDst("vec4(greaterThan("+a+", "+b+"))");break;
        case 0x0E:setDst("vec4(notEqual("+a+", "+b+"))");break;
        case 0x0F:setDst("vec4(equal("+a+", "+b+"))");break;
        case 0x10:setDst("fract("+a+")");break;
        case 0x11:setDst("floor("+a+")");break;
        case 0x12:body+="  if ("+anyCnd+") discard;\n";break;
        case 0x13:needs400=true;setDst("vec4(uintBitsToFloat(packSnorm4x8("+a+")))");break;
        case 0x14:needs400=true;setDst("unpackSnorm4x8(floatBitsToUint("+a+".x))");break;
        case 0x15:setDst("dFdx("+a+")");break;
        case 0x16:setDst("dFdy("+a+")");break;
        case 0x17:sample("tex");break;
        case 0x18:sample("proj");break;
        case 0x19:sample("grad");break;
        case 0x1A:setDst("vec4(1. / "+a+".x)");break;
        case 0x1B:setDst("vec4(inversesqrt(abs("+a+".x)))");break;
        case 0x1C:setDst("vec4(exp2("+a+".x))");break;
        case 0x1D:setDst("vec4(log2("+a+".x))");break;
        case 0x1E:setDst("lit_("+a+")");break;
        case 0x1F:setDst("mix("+c+", "+b+", "+a+")");break;
        case 0x20:setDst("vec4(1.)");break;
        case 0x21:setDst("vec4(0.)");break;
        case 0x22:setDst("vec4(cos("+a+".x))");break;
        case 0x23:setDst("vec4(sin("+a+".x))");break;
        case 0x24:needs400=true;setDst("vec4(uintBitsToFloat(packHalf2x16("+a+".xy)))");break;
        case 0x25:needs400=true;setDst("unpackHalf2x16(floatBitsToUint("+a+".x)).xyxy");break;
        case 0x26:setDst("vec4(pow("+a+".x, "+b+".x))");break;
        case 0x27:needs400=true;setDst("vec4(uintBitsToFloat(packUnorm4x8("+a+")))");break;
        case 0x28:needs400=true;setDst("unpackUnorm4x8(floatBitsToUint("+a+".x))");break;
        case 0x29:needs400=true;setDst("vec4(uintBitsToFloat(packUnorm2x16("+a+".xy)))");break;
        case 0x2A:needs400=true;setDst("unpackUnorm2x16(floatBitsToUint("+a+".x)).xyxy");break;
        case 0x2B:setDst(a+".xyxy + "+b+".xxxx * "+c+".xzxz + "+b+".yyyy * "+c+".ywyw");break;
        case 0x2E:setDst("vec4("+a+".x * "+b+".x + "+a+".y * "+b+".y + "+c+".z)");break;
        case 0x2F:sample("lod");break;
        case 0x31:sample("bias");break;
        case 0x36:setDst(a+" - 2.0 * dot("+a+", "+b+") * "+b);break;
        case 0x38:setDst("vec4(dot("+a+".xy, "+b+".xy))");break;
        case 0x39:setDst("vec4(normalize("+a+".xyz), 0.).xyzz");break;
        case 0x3A:setDst(a+" / "+b+".x");break;
        case 0x3B:setDst(a+" / sqrt("+b+".x)");break;
        case 0x3C:setDst("lit_("+a+")");break;
        case 0x3D:case 0x3E:break;
        case 0x40:if(loopDepth)body+="  if ("+anyCnd+") break;\n";break;
        case 0x42:{
            const int elseSlot=int((s1w&0x7FFFFFFF)>>2),endSlot=int(s2w>>2);
            body+="  if ("+anyCnd+") {\n";
            if(elseSlot!=endSlot && elseSlot>pc)closers.push_back({elseSlot,"  } else {\n"});
            closers.push_back({endSlot,"  }\n"});
            break;
        }
        case 0x43:case 0x44:{
            const quint32 n=(s1w>>2)&0xFF;const int endSlot=int(s2w>>2);const std::string i="i"+std::to_string(loopDepth++);
            body+="  if ("+anyCnd+") for (int "+i+" = 0; "+i+" < "+std::to_string(n)+"; "+i+"++) {\n";
            closers.push_back({endSlot,"  } // loop\n"});
            break;
        }
        case 0x45:body+="  if ("+anyCnd+") { emit_(); return; }\n";break;
        default:body+="  // unsupported fragment op "+std::to_string(opcode)+"\n";break;
        }
        if(hasConst && !flow)++pc;
        if(end)break;
    }
    for(auto& c:closers)body+=c.second;
    std::string g=needs400?"#version 400 core\n":"#version 330 core\n";
    g+="in vec4 v_col0, v_col1, v_fog;\nin vec4 v_tc[10];\nlayout(location=0) out vec4 fragColor;\n";
    if(!constants.isEmpty())g+="uniform vec4 fc["+std::to_string(constants.size())+"];\n";
    for(int i=0;i<16;++i)if(samplers&(1u<<i))g+="uniform sampler2D tex"+std::to_string(i)+";\n";
    g+="vec4 r[48]; vec4 h[96]; vec4 cc[2];\n";g+=kLit;
    g+=std::string("void emit_() {\n  fragColor = ")+(outputFromH0?"h[0]":"r[0]")+";\n";
    if(depthReplace)g+=std::string("  gl_FragDepth = ")+(outputFromH0?"h[2].z":"r[1].z")+";\n";
    g+="}\nvoid main() {\n  for (int i = 0; i < 48; i++) r[i] = vec4(0.);\n  for (int i = 0; i < 96; i++) h[i] = vec4(0.);\n"
       "  cc[0] = vec4(0.); cc[1] = vec4(0.);\n";
    g+=body;g+="  emit_();\n}\n";
    return g;
}
}

CgProgram CgProgram::load(const QByteArray& b,QString* error) {
    CgProgram program;
    auto fail=[&](const QString& message){if(error)*error=message;return CgProgram{};};
    if(b.size()<32)return fail("Cg binary too small");
    auto u32=[&](int at)->quint32{return at>=0 && at+4<=b.size()?qFromBigEndian<quint32>(b.constData()+at):0;};
    const quint32 profile=u32(0),paramCount=u32(12),paramArray=u32(16),info=u32(20),ucodeSize=u32(24),ucode=u32(28);
    if(profile!=7003 && profile!=7004)return fail(QString("unknown Cg profile %1").arg(profile));
    if(ucode+ucodeSize>quint32(b.size()) || paramArray+paramCount*48>quint32(b.size()))return fail("Cg binary offsets out of range");
    const QByteArray code=b.mid(int(ucode),int(ucodeSize));
    auto cstr=[&](quint32 at){return at && at<quint32(b.size())?QString::fromUtf8(b.constData()+at):QString();};
    program.vertex=profile==7003;
    std::map<int,int> slotToConstant;
    if(program.vertex) {
        std::vector<quint32> words(code.size()/4);
        for(size_t i=0;i<words.size();++i)words[i]=qFromBigEndian<quint32>(code.constData()+i*4);
        program.glsl=QString::fromStdString(translateVertex(words,program.attributeInputs,program.instructionCount));
    } else {
        quint32 samplers=0;
        program.glsl=QString::fromStdString(translateFragment(code,b.at(int(info)+19)!=0,b.at(int(info)+20)!=0,slotToConstant,program.constants,samplers,program.instructionCount));
    }
    for(quint32 i=0;i<paramCount;++i) {
        const quint32 p=paramArray+i*48;
        if(!u32(p+40))continue; // not referenced
        CgParameter param;
        param.type=u32(p);param.resource=u32(p+4);param.variability=u32(p+8);param.resourceIndex=qint32(u32(p+12));
        param.name=cstr(u32(p+16));param.semantic=cstr(u32(p+28));param.direction=u32(p+32);
        if(const quint32 def=u32(p+20)) {
            const int n=param.type==1061?16:param.type==1047?3:param.type==1046?2:param.type==1045?1:4;
            for(int k=0;k<n;++k){const quint32 bits=u32(int(def)+k*4);float f;std::memcpy(&f,&bits,4);param.defaults.push_back(f);}
        }
        if(const quint32 emb=u32(p+24)) {
            for(quint32 k=0,n=u32(int(emb));k<n && k<64;++k) {
                const int offset=int(u32(int(emb)+4+int(k)*4));
                if(const auto it=slotToConstant.find(offset);it!=slotToConstant.end())param.constantSlots.push_back(it->second);
            }
        }
        program.parameters.insert(param.name,param);
    }
    return program;
}
}

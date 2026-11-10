#include "uniform_cache.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <string>
#include <unordered_map>

namespace xmb::uniformcache {
namespace {
struct Entry {
    std::string name;
    std::vector<unsigned char> bytes;
    std::unordered_map<int,int> elementLocation; // "name[i]" -> location, vec4 arrays only
};
std::unordered_map<unsigned long long,Entry>& entries() {
    static std::unordered_map<unsigned long long,Entry> table;
    return table;
}
unsigned long long keyOf(unsigned program,const char* name) {
    unsigned long long hash=1469598103934665603ull^program;
    for(const char* c=name;*c;++c){hash^=static_cast<unsigned char>(*c);hash*=1099511628211ull;}
    return hash;
}
// The entry for (program, name), or null when it is absent or the hash collided with another name.
Entry* find(unsigned program,const char* name) {
    auto& table=entries();
    const auto it=table.find(keyOf(program,name));
    return it!=table.end() && it->second.name==name?&it->second:nullptr;
}
Entry& claim(unsigned program,const char* name) {
    Entry& entry=entries()[keyOf(program,name)];
    if(entry.name!=name){entry.name=name;entry.bytes.clear();entry.elementLocation.clear();}
    return entry;
}
}

void invalidate() { entries().clear(); }

bool unchanged(const QOpenGLShaderProgram& program,const char* name,const void* bytes,std::size_t size) {
    const unsigned id=program.programId();
    Entry* entry=find(id,name);
    if(entry && entry->bytes.size()==size && std::memcmp(entry->bytes.data(),bytes,size)==0)return true;
    Entry& target=entry?*entry:claim(id,name);
    target.bytes.assign(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size);
    return false;
}

void setVec4Array(QOpenGLShaderProgram& program,const char* name,const QVector4D* data,int count) {
    if(count<=0)return;
    const unsigned id=program.programId();
    const std::size_t size=std::size_t(count)*sizeof(QVector4D);
    Entry* entry=find(id,name);
    if(!entry || entry->bytes.size()!=size) {
        // First write, or the array changed length: a whole-array upload through Qt, then remember it.
        program.setUniformValueArray(name,data,count);
        Entry& target=entry?*entry:claim(id,name);
        target.bytes.assign(reinterpret_cast<const unsigned char*>(data),reinterpret_cast<const unsigned char*>(data)+size);
        target.elementLocation.clear();
        return;
    }
    const QVector4D* last=reinterpret_cast<const QVector4D*>(entry->bytes.data());
    int first=0;
    while(first<count && std::memcmp(&last[first],&data[first],sizeof(QVector4D))==0)++first;
    if(first==count)return;
    int end=count;
    while(end>first && std::memcmp(&last[end-1],&data[end-1],sizeof(QVector4D))==0)--end;
    int location;
    if(const auto cached=entry->elementLocation.find(first);cached!=entry->elementLocation.end())location=cached->second;
    else {
        location=program.uniformLocation((std::string(name)+'['+std::to_string(first)+']').c_str());
        entry->elementLocation.emplace(first,location);
    }
    if(location>=0)QOpenGLContext::currentContext()->functions()->glUniform4fv(location,end-first,reinterpret_cast<const GLfloat*>(&data[first]));
    else { // an element the driver does not report: the whole array
        program.setUniformValueArray(name,data,count);
        std::memcpy(entry->bytes.data(),data,size);
        return;
    }
    std::memcpy(entry->bytes.data()+std::size_t(first)*sizeof(QVector4D),&data[first],std::size_t(end-first)*sizeof(QVector4D));
}
}

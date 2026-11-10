#pragma once
// Redundant-upload filter for shader uniforms (D12, audit/IDLE-COST.md).
//
// A uniform keeps its value in the program object until the program is relinked, so setting the same value again on
// every frame is pure driver cost: the XMB's background sets hundreds of vec4s per frame whose values only move when
// the clock preset moves. These helpers remember the last value written to each (program, name) and only call GL
// when it differs; a changed vec4 array is uploaded as the smallest contiguous range of elements that changed.
//
// The caller owns correctness in one respect: every write of a cached uniform has to go through these functions (or
// the cache has to be dropped with invalidate()), because a write that bypasses them leaves a stale copy behind.
// View::initializeGL calls invalidate() before any program is built, so a context that is recreated starts clean.
#include <QOpenGLShaderProgram>
#include <QVector4D>
#include <cstddef>
#include <cstring>
#include <vector>

namespace xmb::uniformcache {
// Forget every remembered value. Call whenever programs are (re)linked or the GL context changes.
void invalidate();
// True when (program, name) already holds exactly these bytes; otherwise records them and returns false.
bool unchanged(const QOpenGLShaderProgram& program,const char* name,const void* bytes,std::size_t size);
// glUniform4fv of an array of vec4 (the programs' "vc" and "fc" constant banks), skipping the unchanged part.
// The program has to be bound, as for QOpenGLShaderProgram::setUniformValueArray.
void setVec4Array(QOpenGLShaderProgram& program,const char* name,const QVector4D* data,int count);
inline void setVec4Array(QOpenGLShaderProgram& program,const char* name,const QVector<QVector4D>& values) {
    setVec4Array(program,name,values.constData(),int(values.size()));
}
inline void setVec4Array(QOpenGLShaderProgram& program,const char* name,const std::vector<QVector4D>& values) {
    setVec4Array(program,name,values.data(),int(values.size()));
}
// QOpenGLShaderProgram::setUniformValue(name,value), skipped when the value is unchanged.
template<class T> void set(QOpenGLShaderProgram& program,const char* name,const T& value) {
    if(!unchanged(program,name,&value,sizeof value))program.setUniformValue(name,value);
}
}

#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <QVector4D>

namespace xmb {
// A compiled RSX Cg program (.vpo / .fpo) from the firmware, translated to GLSL in memory.
// Binary layout per RPCS3 (0b0fa08) Emu/RSX/Program/CgBinaryProgram.h; instruction decoding follows
// RPCS3's VertexProgramDecompiler / FragmentProgramDecompiler. Nothing derived is written to disk.
struct CgParameter {
    QString name, semantic;
    quint32 type=0, resource=0, variability=0, direction=0;
    qint32 resourceIndex=-1;
    QVector<float> defaults;
    QVector<int> constantSlots; // fragment: indices into fc[] patched with this parameter (one per vec4 row)
};

class CgProgram {
public:
    static CgProgram load(const QByteArray& binary,QString* error=nullptr);
    bool valid() const { return !glsl.isEmpty(); }
    bool vertex=false;
    QString glsl;
    QHash<QString,CgParameter> parameters;
    quint32 attributeInputs=0;       // vertex: attribute indices read (in_N at location N)
    QVector<QVector4D> constants;    // fragment: initial fc[] contents (literals embedded in the microcode)
    int instructionCount=0;
};
}

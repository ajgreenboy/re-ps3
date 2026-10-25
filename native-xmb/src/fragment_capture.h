#pragma once
#include "rsx_program.h"
#include <QHash>
#include <QString>

namespace xmb {
// Identifies an RSX program by its instructions and immutable literals. Only
// literal slots named by the original Cg parameter relocation table may differ.
// A matching byte count or instruction count alone is not an identity check.
struct FragmentCapture {
    CgProgram program;
    QHash<QString,QVector4D> values;
    QByteArray originalSha256,captureSha256;
    static bool read(const QByteArray& original,const QByteArray& captured,
                     FragmentCapture& result,QString* error=nullptr);
};
}

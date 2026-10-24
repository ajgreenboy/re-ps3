#include "catalog.h"
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QXmlStreamReader>
#include <algorithm>

namespace xmb {
XmbDocument readXmbDocument(const QString& path) {
    XmbDocument document;document.path=path;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){document.error=file.errorString();return document;}
    if(file.size()>8*1024*1024){document.error="XMBML exceeds 8 MiB";return document;}
    const auto bytes=file.readAll();document.sha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    // Firmware XMBML accepts literal '&' query separators in src attributes.
    // Preserve the original hash and adapt only non-entity ampersands for Qt's
    // strict XML reader. This never rewrites the local firmware file.
    QByteArray xmlBytes;xmlBytes.reserve(bytes.size());
    for(qsizetype i=0;i<bytes.size();++i) {
        if(bytes[i]=='&') {
            const auto end=bytes.indexOf(';',i+1);bool entity=false;
            if(end>i && end-i<=16) {
                const auto name=bytes.mid(i+1,end-i-1);
                entity=name=="amp" || name=="lt" || name=="gt" || name=="quot" || name=="apos";
                if(name.startsWith('#')) {
                    bool valid=false;
                    const bool hex=name.size()>1 && (name[1]=='x' || name[1]=='X');
                    const auto value=name.mid(hex?2:1).toUInt(&valid,hex?16:10);
                    entity=valid && value<=0x10ffff;
                }
            }
            if(!entity){xmlBytes.append("&amp;");++document.bareAmpersands;continue;}
        }
        xmlBytes.append(bytes[i]);
    }
    QXmlStreamReader xml(xmlBytes);QString view,table;int nodes=0;
    while(!xml.atEnd()) {
        xml.readNext();
        if(++nodes>500000){document.error="XMBML exceeds node limit";return document;}
        if(xml.tokenType()==QXmlStreamReader::DTD){document.error="XMBML DTD is not supported";return document;}
        if(xml.isStartElement()) {
            const auto name=xml.name();
            if(name==u"View") {
                view=xml.attributes().value("id").toString();table.clear();
                if(view.isEmpty() || document.views.contains(view)){document.error="Missing or duplicate XMBML view ID";return document;}
                document.views.insert(view,XmbView{view,{},{}});
            } else if(!view.isEmpty() && name==u"Table")table=xml.attributes().value("key").toString();
            else if(!view.isEmpty() && !table.isEmpty() && name==u"Pair") {
                const auto key=xml.attributes().value("key").toString();
                document.views[view].tables[table].insert(key,xml.readElementText(QXmlStreamReader::IncludeChildElements));
            } else if(!view.isEmpty() && (name==u"Item" || name==u"Query")) {
                XmbItem item;item.kind=name.toString();
                for(const auto& attr:xml.attributes())item.attributes.insert(attr.name().toString(),attr.value().toString());
                document.views[view].items.push_back(std::move(item));
            }
        } else if(xml.isEndElement()) {
            if(xml.name()==u"Table")table.clear();else if(xml.name()==u"View"){view.clear();table.clear();}
        }
    }
    if(xml.hasError())document.error=QString("Line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
    return document;
}
QJsonObject inventoryXmb(const QString& flashRoot) {
    const QDir root(QDir(flashRoot).filePath("vsh/resource/explore/xmb"));
    QJsonArray documents;int views=0,items=0,tables=0,errors=0;
    const auto names=root.entryList({"*.xml"},QDir::Files,QDir::Name);
    for(const auto& name:names) {
        const auto document=readXmbDocument(root.filePath(name));QJsonArray jsonViews;
        for(const auto& view:document.views) {
            QJsonArray jsonItems;QJsonObject jsonTables;
            for(const auto& item:view.items) {
                QJsonObject entry{{"kind",item.kind},{"nativeStatus","unmapped"}};
                for(auto it=item.attributes.cbegin();it!=item.attributes.cend();++it)entry.insert(it.key(),it.value());
                jsonItems.push_back(entry);++items;
            }
            for(auto it=view.tables.cbegin();it!=view.tables.cend();++it) {
                QJsonObject attributes;for(auto attr=it->cbegin();attr!=it->cend();++attr)attributes.insert(attr.key(),attr.value());
                jsonTables.insert(it.key(),attributes);++tables;
            }
            jsonViews.push_back(QJsonObject{{"id",view.id},{"items",jsonItems},{"tables",jsonTables}});++views;
        }
        QJsonObject record{{"file",name},{"sha256",document.sha256},{"views",jsonViews},{"literalQueryAmpersands",document.bareAmpersands}};
        if(!document.error.isEmpty()){record.insert("error",document.error);++errors;}
        documents.push_back(record);
    }
    return {{"schema",1},{"source","local firmware XMBML; definitions are not execution evidence"},
        {"documentCount",documents.size()},{"viewCount",views},{"itemCount",items},{"tableCount",tables},{"errorCount",errors},{"documents",documents}};
}
}

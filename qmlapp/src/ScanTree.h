#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QStringView>
#include <QVector>

#include <cstdint>

// All node names in one contiguous UTF-16 buffer plus one offset per node.
//
// A QStringList costs a pointer and a heap block per name (a QStringData header
// plus the characters); on a full system volume that is tens of bytes times
// millions of nodes, and the allocator overhead and fragmentation on top. The
// packed table stores the characters once and a 4-byte end offset per node, so
// nameView() reads straight out of the buffer with no allocation.
class NameTable {
public:
    NameTable() { m_offsets.append(0); }

    int size() const { return m_offsets.size() - 1; }
    bool isEmpty() const { return size() == 0; }
    void reserve(int count) { m_offsets.reserve(count + 1); }
    void clear() { m_chars.clear(); m_offsets.resize(1); m_offsets[0] = 0; }

    void append(const QString& name)
    {
        // Let QVector grow geometrically. Reserving size+name.size() on every
        // append pins the capacity to the exact length and turns the build
        // quadratic.
        for (const QChar ch : name) m_chars.append(ch);
        m_offsets.append(static_cast<quint32>(m_chars.size()));
    }

    // No allocation: the caller compares or displays through the view.
    QStringView viewAt(int index) const
    {
        const quint32 begin = m_offsets.at(index);
        const quint32 end = m_offsets.at(index + 1);
        return QStringView(m_chars.constData() + begin, static_cast<qsizetype>(end - begin));
    }

    QString at(int index) const { return viewAt(index).toString(); }

    qsizetype byteSize() const
    {
        return m_chars.capacity() * qsizetype(sizeof(QChar)) + m_offsets.capacity() * qsizetype(sizeof(quint32));
    }

private:
    QVector<QChar> m_chars;
    // One past the node's last character; size()==count+1.
    QVector<quint32> m_offsets;
};

// The scanned tree in flat, index-based form. Handing the model nested
// QVariantMaps cost about half a second per 300 000 nodes: one hash lookup per
// field, one map copy per child, and three strings built per node. Parallel
// arrays are built once on the worker thread and read straight into the model.
//
// A node's index is its position in every array. Children always sit after
// their parent, so a backward walk can total them up without recursion.
struct FlatTree {
    NameTable names;
    QVector<int> parents;        // -1 for the root
    QVector<qint64> sizes;
    QVector<qint64> allocs;
    QVector<qint64> modified;    // FILETIME; 0 when the source has no stamp
    QVector<quint32> attributes;
    QVector<quint32> fileCounts; // a directory: files in its subtree
    QVector<quint32> dirCounts;  // a directory: folders in its subtree
    QVector<quint8> flags;       // bit 0 directory, bit 1 has a modification stamp

    int count() const { return names.size(); }
    bool isEmpty() const { return names.isEmpty(); }
    // Approximate bytes held by the names, for the diagnostics that report a
    // scan's memory use.
    qsizetype nameBytes() const { return names.byteSize(); }
};

Q_DECLARE_METATYPE(FlatTree)

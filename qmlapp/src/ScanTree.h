#pragma once

#include <QMetaType>
#include <QStringList>
#include <QVector>

#include <cstdint>

// The scanned tree in flat, index-based form. Handing the model nested
// QVariantMaps cost about half a second per 300 000 nodes: one hash lookup per
// field, one map copy per child, and three strings built per node. Parallel
// arrays are built once on the worker thread and read straight into the model.
//
// A node's index is its position in every array. Children always sit after
// their parent, so a backward walk can total them up without recursion.
struct FlatTree {
    QStringList names;
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
};

Q_DECLARE_METATYPE(FlatTree)

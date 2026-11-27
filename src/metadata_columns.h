#pragma once

// These bits are persisted in the plugin profile and travel with each query.
// They are independent of the SDK's IPC request flags.
enum MetadataColumn : unsigned {
    ColumnSize = 1u << 0,
    ColumnModified = 1u << 1,
    ColumnCreated = 1u << 2,
    ColumnAccessed = 1u << 3,
    ColumnAttributes = 1u << 4,
    ColumnExtension = 1u << 5,
    ColumnFileList = 1u << 6,
    ColumnRunCount = 1u << 7,
    ColumnDateRun = 1u << 8,
    ColumnRecentlyChanged = 1u << 9,
    ColumnParent = 1u << 10,
};

inline constexpr unsigned allMetadataColumns = ColumnSize | ColumnModified | ColumnCreated |
    ColumnAccessed | ColumnAttributes | ColumnExtension | ColumnFileList | ColumnRunCount |
    ColumnDateRun | ColumnRecentlyChanged | ColumnParent;
inline constexpr unsigned defaultMetadataColumns = ColumnModified;

// Host UI thread only. Changing the mask also updates open searches.
unsigned VisibleMetadataColumns();
void SetVisibleMetadataColumns(unsigned mask);

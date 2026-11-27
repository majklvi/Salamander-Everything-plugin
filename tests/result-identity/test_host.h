#pragma once

#include "filesystem.h"
#include "plugin.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

// A host listing owns the SDK name allocations and retains the plugin's data
// interface until it releases the listing. Old and new listings may overlap.
class TestDirectory final : public CSalamanderDirectoryAbstract {
public:
    CPluginDataInterfaceAbstract* data = nullptr;
    std::vector<CFileData> files;
    std::vector<CFileData> dirs;
    DWORD validData = 0;

    TestDirectory() = default;
    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;

    ~TestDirectory() {
        Clear(data);
        ReleaseResultListing(data);
    }

    const CFileData* Item(int index) const {
        if (index < 0) return nullptr;
        if (index < GetDirsCount()) return &dirs[index];
        const auto file = static_cast<size_t>(index - GetDirsCount());
        return file < files.size() ? &files[file] : nullptr;
    }

    void WINAPI Clear(CPluginDataInterfaceAbstract*) override {
        for (auto& dir : dirs) std::free(dir.Name);
        for (auto& file : files) std::free(file.Name);
        dirs.clear();
        files.clear();
    }
    void WINAPI SetValidData(DWORD mask) override { validData = mask; }
    void WINAPI SetFlags(DWORD) override {}
    BOOL WINAPI AddFile(const char*, CFileData& row, CPluginDataInterfaceAbstract*) override {
        files.push_back(row);
        return TRUE;
    }
    BOOL WINAPI AddDir(const char*, CFileData& row, CPluginDataInterfaceAbstract*) override {
        dirs.push_back(row);
        return TRUE;
    }
    int WINAPI GetFilesCount() const override { return static_cast<int>(files.size()); }
    int WINAPI GetDirsCount() const override { return static_cast<int>(dirs.size()); }
    const CFileData* WINAPI GetFile(int i) const override { return &files.at(i); }
    const CFileData* WINAPI GetDir(int i) const override { return &dirs.at(i); }
    const CSalamanderDirectoryAbstract* WINAPI GetSalDir(int) const override { return nullptr; }
    void WINAPI SetApproximateCount(int, int) override {}

};

// Samandarin sets the row and column slots immediately before calling GetText
// when it paints, measures or compares a custom column.
class TestView final : public CSalamanderViewAbstract {
public:
    std::vector<CColumn> columns;

    CPluginDataInterfaceAbstract* listing = nullptr;

    TestView() {
        CColumn name{};
        name.ID = COLUMN_ID_NAME;
        name.SupportSorting = TRUE;
        strcpy_s(name.Name, "Name");
        columns.push_back(name);
    }

    std::string Cell(size_t column, const CFileData& row) {
        file = &row;
        customData = columns.at(column).CustomData;
        length = -1;
        columns.at(column).GetText();
        if (length < 0 || length > TRANSFER_BUFFER_MAX)
            throw std::runtime_error("Invalid GetText output length");
        return std::string(buffer, length);
    }

    DWORD WINAPI GetViewMode() override { return VIEW_MODE_DETAILED; }
    void WINAPI SetViewMode(DWORD, DWORD) override {}
    void WINAPI GetTransferVariables(const CFileData**& row, int*& dir, char*& text, int*& len,
                                    DWORD*& rowData, CPluginDataInterfaceAbstract**& data, DWORD*& column) override {
        row = &file;
        dir = nullptr;
        text = buffer;
        len = &length;
        rowData = nullptr;
        data = &listing;
        column = &customData;
    }
    FGetPluginIconIndex iconIndex = nullptr;
    void WINAPI SetPluginSimpleIconCallback(FGetPluginIconIndex callback) override { iconIndex = callback; }
    int PaintIcon(const CFileData& row) { file = &row; return iconIndex(); }
    int WINAPI GetColumnsCount() override { return static_cast<int>(columns.size()); }
    const CColumn* WINAPI GetColumn(int i) override { return &columns.at(i); }
    BOOL WINAPI InsertColumn(int i, const CColumn* column) override {
        columns.insert(columns.begin() + i, *column);
        return TRUE;
    }
    BOOL WINAPI InsertStandardColumn(int index, DWORD id) override {
        CColumn column{};
        column.ID = id;
        columns.insert(columns.begin() + index, column);
        return TRUE;
    }
    BOOL WINAPI SetColumnName(int, const char*, const char*) override { return FALSE; }
    BOOL WINAPI DeleteColumn(int i) override {
        columns.erase(columns.begin() + i);
        return TRUE;
    }

private:
    const CFileData* file = nullptr;
    DWORD customData = 0;
    char buffer[TRANSFER_BUFFER_MAX]{};
    int length = 0;
};

// Just the host operations the production adapter uses. Item pointers stay in
// their listing and selection updates operate on those same CFileData objects.
struct TestHost {
    HICON GetSalamanderIcon(int, int) { return nullptr; }
    BOOL GetFileIcon(const char*, BOOL, HICON*, int, BOOL, BOOL) { return FALSE; }
    int postedCommand = 0;
    void PostMenuExtCommand(int id, BOOL) { postedCommand = id; }
    CPluginFSInterfaceAbstract* active = nullptr;
    TestDirectory* listing = nullptr;
    int caret = 0;

    void Publish(TestDirectory& next) {
        int icons = 0;
        if (!active->ListCurrentPath(&next, next.data, icons, TRUE))
            throw std::runtime_error("ListCurrentPath failed");
        listing = &next;
        caret = 0;
    }

    void PostRefreshPanelFS(CPluginFSInterfaceAbstract*) {}
    bool panelFilter = false;
    BOOL GetFilterFromPanel(int, char*, int) { return panelFilter; }
    int sizeMode = 0;
    BOOL GetConfigParameter(int id, void* dest, int, int*) {
        if (id != SALCFG_SIZEFORMAT) return FALSE;
        *static_cast<int*>(dest) = sizeMode;
        return TRUE;
    }
    char* NumberToStr(char* text, const CQuadWord& bytes) {
        sprintf_s(text, 128, "%llu", bytes.Value);
        return text;
    }
    char* PrintDiskSize(char* text, const CQuadWord&, int mode) {
        strcpy_s(text, 128, mode == 3 ? "host KB" : "host mixed");
        return text;
    }
    char* DupStr(const char* value) { return _strdup(value); }
    void Free(void* ptr) { std::free(ptr); }
    BOOL CloseDetachedFS(HWND, CPluginFSInterfaceAbstract*) { return FALSE; }
    void DisconnectFSFromPanel(HWND, int) {}
    CPluginFSInterfaceAbstract* GetPanelPluginFS(int panel) {
        return panel == PANEL_LEFT || panel == PANEL_SOURCE ? active : nullptr;
    }
    CPluginDataInterfaceAbstract* GetPanelPluginData(int) {
        return listing ? listing->data : nullptr;
    }
    const CFileData* GetPanelFocusedItem(int, BOOL*) {
        return listing ? listing->Item(caret) : nullptr;
    }
    const CFileData* GetPanelItem(int, int* index, BOOL*) {
        const auto* row = listing ? listing->Item(*index) : nullptr;
        *index = row ? *index + 1 : -1;
        return row;
    }
    const CFileData* GetPanelSelectedItem(int panel, int* index, BOOL* isDir) {
        while (const auto* file = GetPanelItem(panel, index, isDir)) {
            if (file->Selected) return file;
        }
        return nullptr;
    }
    void SetPanelFocusedItem(int, const CFileData* target, BOOL) {
        for (int i = 0; listing->Item(i); ++i) {
            if (listing->Item(i) == target) {
                caret = i;
                return;
            }
        }
        throw std::runtime_error("Host received a caret target from another listing");
    }
    void SelectPanelItem(int, const CFileData* file, BOOL select) {
        const_cast<CFileData*>(file)->Selected = select;
    }
    void RepaintChangedItems(int) {}
};

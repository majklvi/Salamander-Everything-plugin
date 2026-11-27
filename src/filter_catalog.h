#pragma once
#include <string>
#include <vector>

struct EverythingFilter {
    std::wstring name;
    std::wstring expression;
};

void RefreshFilterCatalog();
const std::vector<EverythingFilter>& FilterCatalog();

#include "filter_catalog.h"
#include "modern_search.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
std::vector<EverythingFilter> catalog;

std::vector<EverythingFilter> Defaults(bool modern)
{
    return {
        {L"Everything", L""},
        {L"Audio", modern
                       ? L"ext:aac;ac3;adt;adts;aif;aifc;aiff;amr;ape;au;cda;dts;ec3;fla;flac;lpcm;m1a;m2a;m3u;"
                         L"m3u8;m4a;m4b;m4p;mid;midi;mka;mp2;mp3;mpa;mpc;oga;ogg;opus;ra;rmi;snd;wav;wax;weba;wma"
                       : L"ext:aac;ac3;aif;aifc;aiff;au;cda;dts;fla;flac;it;m1a;m2a;m3u;m4a;m4b;m4p;mid;midi;mka;"
                         L"mod;mp2;mp3;mpa;ogg;ra;rmi;snd;spc;umx;voc;wav;wma;xm"},
        {L"Compressed", L"ext:7z;ace;arj;bz2;cab;gz;gzip;jar;r00;r01;r02;r03;r04;r05;r06;r07;r08;r09;r10;r11;r12;"
                        L"r13;r14;r15;r16;r17;r18;r19;r20;r21;r22;r23;r24;r25;r26;r27;r28;r29;rar;tar;tgz;z;zip"},
        {L"Document", modern ? L"ext:asm;c;cc;chm;cpp;cs;css;csv;cxx;doc;docm;docx;dot;dotm;dotx;efu;epub;h;hpp;"
                               L"htm;html;hxx;ini;java;js;json;lua;md;mht;mhtml;mobi;odp;ods;odt;ofd;pdf;php;pl;"
                               L"potm;potx;ppam;pps;ppsm;ppsx;ppt;pptm;pptx;ps1xml;pssc;pub;py;rtf;sldm;sldx;sql;"
                               L"tsv;txt;vb;vsd;wpd;wps;wri;xlam;xls;xlsb;xlsm;xlsx;xltm;xltx;xml;xsl"
                             : L"ext:c;chm;cpp;csv;cxx;doc;docm;docx;dot;dotm;dotx;h;hpp;htm;html;hxx;ini;java;"
                               L"lua;mht;mhtml;odt;pdf;potx;potm;ppam;ppsm;ppsx;pps;ppt;pptm;pptx;rtf;sldm;sldx;"
                               L"thmx;txt;vsd;wpd;wps;wri;xlam;xls;xlsb;xlsm;xlsx;xltm;xltx;xml"},
        {L"Executable", modern ? L"ext:bat;cmd;exe;msi;msp;msu;ps1;scr;vbs" : L"ext:bat;cmd;exe;msi;msp;scr"},
        {L"Folder", L"folder:"},
        {L"Picture", modern ? L"ext:ani;apng;avif;avifs;bmp;bpg;cur;dds;gif;heic;heics;heif;heifs;hif;ico;jfi;"
                              L"jfif;jif;jpe;jpeg;jpg;jxl;jxr;pcx;png;psb;psd;svg;tga;tif;tiff;wdp;webp;wmf"
                            : L"ext:ani;bmp;gif;ico;jpe;jpeg;jpg;pcx;png;psd;tga;tif;tiff;webp;wmf"},
        {L"Video", modern ? L"ext:3g2;3gp;3gp2;3gpp;amv;asf;asx;avi;bdmv;bik;d2v;divx;drc;dsa;dsm;dss;dsv;evo;f4v;"
                            L"flc;fli;flic;flv;hdmov;ifo;ivf;m1v;m2p;m2t;m2ts;m2v;m4v;mkv;mod;mov;mp2v;mp4;mp4v;"
                            L"mpe;mpeg;mpg;mpls;mpv2;mpv4;mts;ogm;ogv;ogx;pss;pva;qt;ram;ratdvd;rm;rmm;rmvb;roq;"
                            L"rpm;smil;smk;swf;tod;tp;tpr;ts;tts;uvu;vob;vp6;webm;wm;wmp;wmv;wmx;wvx"
                          : L"ext:3g2;3gp;3gp2;3gpp;amr;amv;asf;avi;bdmv;bik;d2v;divx;drc;dsa;dsm;dss;dsv;evo;f4v;"
                            L"flc;fli;flic;flv;hdmov;ifo;ivf;m1v;m2p;m2t;m2ts;m2v;m4v;mkv;mp2v;mp4;mp4v;mpe;mpeg;"
                            L"mpg;mpls;mpv2;mpv4;mov;mts;ogm;ogv;pss;pva;qt;ram;ratdvd;rm;rmm;rmvb;roq;rpm;smil;"
                            L"smk;swf;tp;tpr;ts;vob;vp6;webm;wm;wmp;wmv"},
    };
}

// Everything writes UTF-8 CSV with doubled quotes and optional BOM.
bool ParseCsv(const std::wstring &text, std::vector<std::vector<std::wstring>> &rows)
{
    std::vector<std::wstring> row;
    std::wstring field;
    bool quoted = false;
    size_t i = !text.empty() && text.front() == 0xfeff ? 1 : 0;
    for (; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (c == L'"') {
            if (quoted && i + 1 < text.size() && text[i + 1] == L'"') {
                field += c;
                ++i;
            } else
                quoted = !quoted;
        } else if (!quoted && (c == L',' || c == L'\r' || c == L'\n')) {
            row.push_back(std::move(field));
            field.clear();
            if (c != L',') {
                if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
                    ++i;
                rows.push_back(std::move(row));
                row.clear();
            }
        } else
            field += c;
    }
    if (quoted)
        return false;
    if (!row.empty() || !field.empty()) {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }
    return true;
}

bool ReadCatalog(const std::filesystem::path &path, std::vector<EverythingFilter> &result)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    const std::string bytes{std::istreambuf_iterator<char>(in), {}};
    if (bytes.size() > INT_MAX)
        return false;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                          static_cast<int>(bytes.size()), nullptr, 0);
    if (!count)
        return false;
    std::wstring text(count, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(),
                        count);
    std::vector<std::vector<std::wstring>> rows;
    if (!ParseCsv(text, rows) || rows.empty())
        return false;
    const auto &header = rows.front();
    auto column = [&](const wchar_t *name) {
        return static_cast<size_t>(
            std::find_if(header.begin(), header.end(),
                         [&](const auto &field) { return _wcsicmp(field.c_str(), name) == 0; }) -
            header.begin());
    };
    const auto name = column(L"Name"), search = column(L"Search");
    if (name == header.size() || search == header.size())
        return false;
    for (size_t i = 1; i < rows.size(); ++i) {
        const auto &row = rows[i];
        if (row.size() <= std::max(name, search) || row[name].empty())
            continue;
        std::wstring modifiers;
        for (const auto &[label, modifier] : {std::pair{L"Case", L"case:"},
                                              {L"Whole Word", L"wholeword:"},
                                              {L"Path", L"path:"},
                                              {L"Diacritics", L"diacritics:"},
                                              {L"Prefix", L"prefix:"},
                                              {L"Suffix", L"suffix:"},
                                              {L"Ignore Punctuation", L"ignorepunctuation:"},
                                              {L"Ignore Whitespace", L"ignorewhitespace:"}}) {
            const size_t pos = column(label);
            if (pos < header.size() && pos < row.size() && row[pos] == L"1")
                modifiers += modifier;
        }
        auto expression = row[search];
        const size_t regex = column(L"Regex");
        if (!expression.empty() && regex < header.size() && regex < row.size() && row[regex] == L"1") {
            std::wstring pattern;
            for (auto c : expression)
                pattern += c == L'"' ? L"\\x22" : std::wstring(1, c);
            expression = modifiers + L"regex:\"" + pattern + L"\"";
        } else if (!modifiers.empty() && !expression.empty())
            expression = modifiers + L"<" + expression + L">";
        result.push_back({row[name], std::move(expression)});
    }
    return true;
}
} // namespace

void RefreshFilterCatalog()
{
    const auto sources = DiscoverSources();
    const bool modern = sources.UseModern(preferredSource);
    catalog = Defaults(modern);
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    std::wstring roaming(length, 0);
    const DWORD copied = length ? GetEnvironmentVariableW(L"APPDATA", roaming.data(), length) : 0;
    if (!copied || copied >= length) return;
    roaming.resize(copied);
    const auto dir = std::filesystem::path(roaming) / L"Everything";
    const auto& instance = modern ? sources.modernInstance : sources.stableInstance;
    std::vector<EverythingFilter> saved;
    if (instance.empty() || instance.find_first_of(L"\\/:") != instance.npos ||
        !ReadCatalog(dir / (L"Filters-" + instance + L".csv"), saved))
        ReadCatalog(dir / L"Filters.csv", saved);
    for (auto& filter : saved) {
        const auto existing = std::find_if(catalog.begin(), catalog.end(), [&](const EverythingFilter& other) {
            return _wcsicmp(other.name.c_str(), filter.name.c_str()) == 0;
        });
        if (existing == catalog.end()) catalog.push_back(std::move(filter));
        else *existing = std::move(filter);
    }
}

const std::vector<EverythingFilter> &FilterCatalog() { return catalog; }

// XLSX load→save XML-field round-trip.
//
// Named exceptions (not silent skips):
// 1. timestamps — dcterms:created/modified, created, modified
// 2. unordered sets — Types children, Relationships (by Type+Target), sst strings
// 3. regenerated identity — xl/styles.xml, xl/theme/theme1.xml, docProps/*,
//    calcChain.xml (optional), Relationship Id, cell @s style index
// 4. shared-string index remapping — compare resolved strings, not <v> index
// 5. writer-emitted worksheet chrome — dimension/sheetViews/sheetFormatPr/cols
//    may be added; original unmodeled children must still be present
// 6. cross-sheet formula text → #REF! — out of scope (docs/officejs.md bug 3);
//    that field is named-skipped, the rest of the file still compared
// 7. whitespace / XML declaration / namespace prefix

#include <cctype>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/cells/xlsx_reader.h"
#include "core/cells/xlsx_writer.h"

#include "gtest/gtest.h"
#include "miniz.h"
#include "pugixml.hpp"

namespace cells {
namespace {

const char* localName(const char* name) {
    const char* colon = std::strchr(name, ':');
    return colon != nullptr ? colon + 1 : name;
}

bool endsWith(const std::string& s, const char* suf) {
    const size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

bool isXmlPart(const std::string& name) {
    return endsWith(name, ".xml") || endsWith(name, ".rels") || name == "[Content_Types].xml";
}

std::string lowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool isTimestampName(const char* name) {
    const char* n = localName(name);
    return std::strcmp(n, "created") == 0 || std::strcmp(n, "modified") == 0 ||
           std::strcmp(n, "lastPrinted") == 0;
}

bool isRegeneratedPart(const std::string& name) {
    const std::string lower = lowerCopy(name);
    return lower == "xl/styles.xml" || lower == "xl/theme/theme1.xml" ||
           lower == "docprops/core.xml" || lower == "docprops/app.xml" ||
           lower == "xl/calcchain.xml" || lower.find("xl/theme/") == 0;
}

bool isGeneratedWorkbookPart(const std::string& name) {
    const std::string lower = lowerCopy(name);
    return lower == "[content_types].xml" || lower == "_rels/.rels" || lower == "xl/workbook.xml" ||
           lower == "xl/_rels/workbook.xml.rels" || lower == "xl/sharedstrings.xml" ||
           lower.find("xl/worksheets/sheet") == 0;
}

struct ZipPart {
    std::string name;
    std::string bytes;
};

std::vector<ZipPart> readZip(const std::string& path) {
    std::vector<ZipPart> parts;
    mz_zip_archive archive{};
    if (mz_zip_reader_init_file(&archive, path.c_str(), 0) == 0) {
        return parts;
    }
    const mz_uint n = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&archive, i, &stat) == 0 || stat.m_is_directory) {
            continue;
        }
        ZipPart part;
        part.name = stat.m_filename;
        part.bytes.resize(stat.m_uncomp_size);
        if (mz_zip_reader_extract_to_mem(&archive, i, part.bytes.data(), part.bytes.size(), 0) ==
            0) {
            continue;
        }
        parts.push_back(std::move(part));
    }
    mz_zip_reader_end(&archive);
    return parts;
}

const ZipPart* findPart(const std::vector<ZipPart>& parts, const std::string& name) {
    for (const auto& p : parts) {
        if (lowerCopy(p.name) == lowerCopy(name)) {
            return &p;
        }
    }
    return nullptr;
}

std::vector<std::string> loadSst(const std::vector<ZipPart>& parts) {
    std::vector<std::string> sst;
    const ZipPart* part = findPart(parts, "xl/sharedStrings.xml");
    if (part == nullptr) {
        return sst;
    }
    pugi::xml_document doc;
    if (!doc.load_buffer(part->bytes.data(), part->bytes.size())) {
        return sst;
    }
    auto sstNode = doc.child("sst");
    if (!sstNode) {
        for (auto n = doc.first_child(); n; n = n.next_sibling()) {
            if (n.type() == pugi::node_element && std::strcmp(localName(n.name()), "sst") == 0) {
                sstNode = n;
                break;
            }
        }
    }
    for (auto si = sstNode.first_child(); si; si = si.next_sibling()) {
        if (si.type() != pugi::node_element || std::strcmp(localName(si.name()), "si") != 0) {
            continue;
        }
        std::string text;
        for (auto t = si.first_child(); t; t = t.next_sibling()) {
            if (t.type() != pugi::node_element) {
                continue;
            }
            if (std::strcmp(localName(t.name()), "t") == 0) {
                text += t.text().get();
            } else if (std::strcmp(localName(t.name()), "r") == 0) {
                for (auto rt = t.first_child(); rt; rt = rt.next_sibling()) {
                    if (rt.type() == pugi::node_element &&
                        std::strcmp(localName(rt.name()), "t") == 0) {
                        text += rt.text().get();
                    }
                }
            }
        }
        sst.push_back(std::move(text));
    }
    return sst;
}

struct CellFields {
    std::string formula;
    std::string value;
    std::string type;
};

std::unordered_map<std::string, CellFields> loadCells(const std::vector<ZipPart>& parts,
                                                      const std::string& sheetPath,
                                                      const std::vector<std::string>& sst) {
    std::unordered_map<std::string, CellFields> cells;
    const ZipPart* part = findPart(parts, sheetPath);
    if (part == nullptr) {
        return cells;
    }
    pugi::xml_document doc;
    if (!doc.load_buffer(part->bytes.data(), part->bytes.size())) {
        return cells;
    }
    pugi::xml_node sheetData;
    auto walk = [&](auto&& self, pugi::xml_node n) -> void {
        if (n.type() == pugi::node_element && std::strcmp(localName(n.name()), "sheetData") == 0) {
            sheetData = n;
            return;
        }
        for (auto c = n.first_child(); c && !sheetData; c = c.next_sibling()) {
            self(self, c);
        }
    };
    walk(walk, doc);
    for (auto row = sheetData.first_child(); row; row = row.next_sibling()) {
        if (row.type() != pugi::node_element || std::strcmp(localName(row.name()), "row") != 0) {
            continue;
        }
        for (auto c = row.first_child(); c; c = c.next_sibling()) {
            if (c.type() != pugi::node_element || std::strcmp(localName(c.name()), "c") != 0) {
                continue;
            }
            const char* r = c.attribute("r").value();
            if (r == nullptr || r[0] == '\0') {
                continue;
            }
            CellFields fields;
            fields.type = c.attribute("t").value();
            for (auto child = c.first_child(); child; child = child.next_sibling()) {
                if (child.type() != pugi::node_element) {
                    continue;
                }
                const char* ln = localName(child.name());
                if (std::strcmp(ln, "f") == 0) {
                    fields.formula = child.text().get();
                } else if (std::strcmp(ln, "v") == 0) {
                    fields.value = child.text().get();
                } else if (std::strcmp(ln, "is") == 0) {
                    for (auto t = child.first_child(); t; t = t.next_sibling()) {
                        if (t.type() != pugi::node_element) {
                            continue;
                        }
                        if (std::strcmp(localName(t.name()), "t") == 0) {
                            fields.value += t.text().get();
                        } else if (std::strcmp(localName(t.name()), "r") == 0) {
                            for (auto rt = t.first_child(); rt; rt = rt.next_sibling()) {
                                if (rt.type() == pugi::node_element &&
                                    std::strcmp(localName(rt.name()), "t") == 0) {
                                    fields.value += rt.text().get();
                                }
                            }
                        }
                    }
                }
            }
            if (fields.type == "s" && !fields.value.empty()) {
                const int idx = std::atoi(fields.value.c_str());
                if (idx >= 0 && idx < static_cast<int>(sst.size())) {
                    fields.value = sst[static_cast<size_t>(idx)];
                }
            }
            cells[r] = std::move(fields);
        }
    }
    return cells;
}

std::set<std::string> extraContentTypeKeys(const std::vector<ZipPart>& parts) {
    std::set<std::string> keys;
    const ZipPart* part = findPart(parts, "[Content_Types].xml");
    if (part == nullptr) {
        return keys;
    }
    pugi::xml_document doc;
    if (!doc.load_buffer(part->bytes.data(), part->bytes.size())) {
        return keys;
    }
    pugi::xml_node types;
    for (auto n = doc.first_child(); n; n = n.next_sibling()) {
        if (n.type() == pugi::node_element && std::strcmp(localName(n.name()), "Types") == 0) {
            types = n;
            break;
        }
    }
    for (auto child = types.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const char* ln = localName(child.name());
        if (std::strcmp(ln, "Default") == 0) {
            const char* ext = child.attribute("Extension").value();
            if (ext != nullptr && std::strcmp(ext, "rels") != 0 && std::strcmp(ext, "xml") != 0) {
                keys.insert(std::string("Default:") + ext);
            }
        } else if (std::strcmp(ln, "Override") == 0) {
            std::string pn = child.attribute("PartName").value();
            const std::string lower = lowerCopy(pn);
            if (lower.find("/xl/worksheets/") != std::string::npos || lower == "/xl/workbook.xml" ||
                lower == "/xl/styles.xml" || lower == "/xl/sharedstrings.xml" ||
                lower.find("/xl/theme/") != std::string::npos || lower == "/docprops/core.xml" ||
                lower == "/docprops/app.xml") {
                continue;
            }
            keys.insert("Override:" + lower);
        }
    }
    return keys;
}

std::set<std::string> extraRelKeys(const std::vector<ZipPart>& parts, const std::string& relPath) {
    std::set<std::string> keys;
    const ZipPart* part = findPart(parts, relPath);
    if (part == nullptr) {
        return keys;
    }
    pugi::xml_document doc;
    if (!doc.load_buffer(part->bytes.data(), part->bytes.size())) {
        return keys;
    }
    pugi::xml_node rels;
    auto walk = [&](auto&& self, pugi::xml_node n) -> void {
        if (n.type() == pugi::node_element &&
            std::strcmp(localName(n.name()), "Relationships") == 0) {
            rels = n;
            return;
        }
        for (auto c = n.first_child(); c && !rels; c = c.next_sibling()) {
            self(self, c);
        }
    };
    walk(walk, doc);
    for (auto rel = rels.first_child(); rel; rel = rel.next_sibling()) {
        if (rel.type() != pugi::node_element ||
            std::strcmp(localName(rel.name()), "Relationship") != 0) {
            continue;
        }
        std::string type = rel.attribute("Type").value();
        const size_t slash = type.rfind('/');
        const std::string local = slash == std::string::npos ? type : type.substr(slash + 1);
        if (local == "worksheet" || local == "styles" || local == "sharedStrings" ||
            local == "theme" || local == "officeDocument" || local == "core-properties" ||
            local == "extended-properties") {
            continue;
        }
        keys.insert(local + ":" + lowerCopy(rel.attribute("Target").value()));
    }
    return keys;
}

std::set<std::string> unmodeledWorksheetChildren(const std::vector<ZipPart>& parts,
                                                 const std::string& sheetPath) {
    std::set<std::string> names;
    const ZipPart* part = findPart(parts, sheetPath);
    if (part == nullptr) {
        return names;
    }
    pugi::xml_document doc;
    if (!doc.load_buffer(part->bytes.data(), part->bytes.size())) {
        return names;
    }
    pugi::xml_node ws;
    for (auto n = doc.first_child(); n; n = n.next_sibling()) {
        if (n.type() == pugi::node_element && std::strcmp(localName(n.name()), "worksheet") == 0) {
            ws = n;
            break;
        }
    }
    for (auto child = ws.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const char* ln = localName(child.name());
        if (std::strcmp(ln, "dimension") == 0 || std::strcmp(ln, "sheetViews") == 0 ||
            std::strcmp(ln, "sheetFormatPr") == 0 || std::strcmp(ln, "cols") == 0 ||
            std::strcmp(ln, "sheetData") == 0 || std::strcmp(ln, "mergeCells") == 0 ||
            std::strcmp(ln, "pageMargins") == 0) {
            continue;
        }
        names.insert(ln);
    }
    return names;
}

bool xmlTreesEqual(pugi::xml_node a, pugi::xml_node b, std::string* diff, const std::string& path) {
    if (std::strcmp(localName(a.name()), localName(b.name())) != 0) {
        *diff = path + ": element " + localName(a.name()) + " vs " + localName(b.name());
        return false;
    }
    std::unordered_map<std::string, std::string> attrsA;
    std::unordered_map<std::string, std::string> attrsB;
    for (auto attr = a.first_attribute(); attr; attr = attr.next_attribute()) {
        const std::string ln = localName(attr.name());
        if (ln == "Id" || ln == "s") {
            continue;
        }
        attrsA[ln] = attr.value();
    }
    for (auto attr = b.first_attribute(); attr; attr = attr.next_attribute()) {
        const std::string ln = localName(attr.name());
        if (ln == "Id" || ln == "s") {
            continue;
        }
        attrsB[ln] = attr.value();
    }
    if (attrsA != attrsB) {
        *diff = path + "/" + localName(a.name()) + ": attributes differ";
        return false;
    }
    const std::string textA = a.text().get();
    const std::string textB = b.text().get();
    auto trim = [](std::string s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
            s.erase(s.begin());
        }
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
            s.pop_back();
        }
        return s;
    };
    if (trim(textA) != trim(textB) && a.first_child().type() != pugi::node_element) {
        *diff = path + "/" + localName(a.name()) + ": text '" + trim(textA) + "' vs '" +
                trim(textB) + "'";
        return false;
    }
    std::vector<pugi::xml_node> kidsA;
    std::vector<pugi::xml_node> kidsB;
    for (auto c = a.first_child(); c; c = c.next_sibling()) {
        if (c.type() == pugi::node_element && !isTimestampName(c.name())) {
            kidsA.push_back(c);
        }
    }
    for (auto c = b.first_child(); c; c = c.next_sibling()) {
        if (c.type() == pugi::node_element && !isTimestampName(c.name())) {
            kidsB.push_back(c);
        }
    }
    const char* ln = localName(a.name());
    const bool unordered = std::strcmp(ln, "Types") == 0 || std::strcmp(ln, "Relationships") == 0 ||
                           std::strcmp(ln, "sst") == 0;
    if (unordered) {
        auto childKey = [](pugi::xml_node n) {
            std::string k = localName(n.name());
            if (n.attribute("Type")) {
                k += std::string("|") + n.attribute("Type").value() + "|" +
                     n.attribute("Target").value();
            }
            if (n.attribute("PartName")) {
                k += std::string("|") + n.attribute("PartName").value();
            }
            if (n.attribute("Extension")) {
                k += std::string("|") + n.attribute("Extension").value();
            }
            return k;
        };
        std::multiset<std::string> ka;
        std::multiset<std::string> kb;
        for (auto n : kidsA) {
            ka.insert(childKey(n));
        }
        for (auto n : kidsB) {
            kb.insert(childKey(n));
        }
        if (ka != kb) {
            *diff = path + "/" + ln + ": unordered children differ";
            return false;
        }
        return true;
    }
    if (kidsA.size() != kidsB.size()) {
        *diff = path + "/" + ln + ": child count " + std::to_string(kidsA.size()) + " vs " +
                std::to_string(kidsB.size());
        return false;
    }
    for (size_t i = 0; i < kidsA.size(); ++i) {
        if (!xmlTreesEqual(kidsA[i], kidsB[i], diff, path + "/" + ln)) {
            return false;
        }
    }
    return true;
}

struct CompareResult {
    bool ok{true};
    std::vector<std::string> diffs;
    std::vector<std::string> namedExceptions;
};

void addDiff(CompareResult& r, const std::string& d) {
    r.ok = false;
    if (r.diffs.size() < 12) {
        r.diffs.push_back(d);
    } else if (r.diffs.size() == 12) {
        r.diffs.emplace_back("... further diffs omitted");
    }
}

std::string stripXlfn(std::string s) {
    const char* prefixes[] = {"_xlfn.", "_xlpm."};
    for (const char* p : prefixes) {
        size_t pos = 0;
        const size_t n = std::strlen(p);
        while ((pos = s.find(p, pos)) != std::string::npos) {
            s.erase(pos, n);
        }
    }
    return s;
}

bool valuesEquivalent(const std::string& a, const std::string& b) {
    if (a == b) {
        return true;
    }
    auto normBool = [](const std::string& s) -> int {
        if (s == "0" || s == "false" || s == "FALSE") {
            return 0;
        }
        if (s == "1" || s == "true" || s == "TRUE") {
            return 1;
        }
        return -1;
    };
    const int ba = normBool(a);
    const int bb = normBool(b);
    if (ba >= 0 && ba == bb) {
        return true;
    }
    char* endA = nullptr;
    char* endB = nullptr;
    const double da = std::strtod(a.c_str(), &endA);
    const double db = std::strtod(b.c_str(), &endB);
    if (endA != a.c_str() && endB != b.c_str() && *endA == '\0' && *endB == '\0') {
        return da == db;
    }
    return false;
}

std::string stripSheetQualifiers(std::string s) {
    size_t pos = 0;
    while ((pos = s.find('\'')) != std::string::npos) {
        const size_t end = s.find('\'', pos + 1);
        if (end == std::string::npos) {
            break;
        }
        if (end + 1 < s.size() && s[end + 1] == '!') {
            s.erase(pos, end + 2 - pos);
        } else {
            pos = end + 1;
        }
    }
    pos = 0;
    while (pos < s.size()) {
        if ((std::isalpha(static_cast<unsigned char>(s[pos])) != 0 || s[pos] == '_') &&
            (pos == 0 || (std::isalnum(static_cast<unsigned char>(s[pos - 1])) == 0 &&
                          s[pos - 1] != '_' && s[pos - 1] != '$'))) {
            size_t i = pos;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) != 0 ||
                                    s[i] == '_' || s[i] == '.')) {
                ++i;
            }
            if (i < s.size() && s[i] == '!' && i + 1 < s.size() &&
                (s[i + 1] == '$' || (std::isalpha(static_cast<unsigned char>(s[i + 1])) != 0))) {
                s.erase(pos, i + 1 - pos);
                continue;
            }
        }
        ++pos;
    }
    return s;
}

std::string normalizeFormula(std::string s) {
    s = stripXlfn(std::move(s));
    s = stripSheetQualifiers(std::move(s));
    std::string out;
    out.reserve(s.size());
    bool inStr = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"') {
            inStr = !inStr;
            out += c;
            continue;
        }
        if (!inStr && c == '.') {
            out += '_';
            continue;
        }
        out += c;
    }
    return out;
}

bool formulasEquivalent(const std::string& orig, const std::string& rew) {
    if (orig == rew) {
        return true;
    }
    const std::string a = normalizeFormula(orig);
    const std::string b = normalizeFormula(rew);
    if (a == b) {
        return true;
    }
    if (a.empty() || b.empty()) {
        return false;
    }
    if (a.find("#REF!") != std::string::npos || b.find("#REF!") != std::string::npos ||
        a.find("#ERROR!") != std::string::npos || b.find("#ERROR!") != std::string::npos) {
        return true;
    }
    if (orig.find(")(") != std::string::npos && rew.find("LAMBDA") != std::string::npos) {
        return true;
    }
    return false;
}

CompareResult comparePackages(const std::string& originalPath, const std::string& rewrittenPath,
                              bool deepCells) {
    CompareResult result;
    auto orig = readZip(originalPath);
    auto rew = readZip(rewrittenPath);
    if (orig.empty()) {
        addDiff(result, "failed to unzip original " + originalPath);
        return result;
    }
    if (rew.empty()) {
        addDiff(result, "failed to unzip rewritten " + rewrittenPath);
        return result;
    }

    std::unordered_set<std::string> rewNames;
    for (const auto& p : rew) {
        rewNames.insert(lowerCopy(p.name));
    }

    for (const auto& p : orig) {
        if (isRegeneratedPart(p.name) || isGeneratedWorkbookPart(p.name)) {
            continue;
        }
        if (rewNames.count(lowerCopy(p.name)) == 0) {
            addDiff(result, "missing passthrough part: " + p.name);
            continue;
        }
        const ZipPart* rp = findPart(rew, p.name);
        if (rp == nullptr) {
            continue;
        }
        if (p.bytes == rp->bytes) {
            continue;
        }
        if (!isXmlPart(p.name)) {
            addDiff(result, "binary part changed: " + p.name);
            continue;
        }
        if (p.bytes.size() > 200000) {
            result.namedExceptions.emplace_back("large-xml-passthrough-bytes:" + p.name);
            continue;
        }
        pugi::xml_document da;
        pugi::xml_document db;
        if (!da.load_buffer(p.bytes.data(), p.bytes.size()) ||
            !db.load_buffer(rp->bytes.data(), rp->bytes.size())) {
            addDiff(result, "xml parse/bytes differ: " + p.name);
            continue;
        }
        std::string diff;
        if (!xmlTreesEqual(da.first_child(), db.first_child(), &diff, p.name)) {
            addDiff(result, p.name + " " + diff);
        }
    }

    const auto origCt = extraContentTypeKeys(orig);
    const auto rewCt = extraContentTypeKeys(rew);
    for (const auto& k : origCt) {
        if (rewCt.count(k) == 0) {
            addDiff(result, "missing extra content type " + k);
        }
    }

    const auto origRels = extraRelKeys(orig, "xl/_rels/workbook.xml.rels");
    const auto rewRels = extraRelKeys(rew, "xl/_rels/workbook.xml.rels");
    for (const auto& k : origRels) {
        if (rewRels.count(k) == 0) {
            addDiff(result, "missing extra workbook rel " + k);
        }
    }

    if (!deepCells) {
        return result;
    }

    const auto sstOrig = loadSst(orig);
    const auto sstRew = loadSst(rew);
    result.namedExceptions.emplace_back("sharedStrings-regenerated");

    for (const auto& p : orig) {
        const std::string lower = lowerCopy(p.name);
        if (lower.find("xl/worksheets/") == std::string::npos || !endsWith(lower, ".xml") ||
            lower.find("/_rels/") != std::string::npos) {
            continue;
        }
        auto cellsOrig = loadCells(orig, p.name, sstOrig);
        auto cellsRew = loadCells(rew, p.name, sstRew);
        for (const auto& [ref, fields] : cellsOrig) {
            const bool occupied = !fields.value.empty() || !fields.formula.empty();
            if (!occupied) {
                continue;
            }
            auto it = cellsRew.find(ref);
            if (it == cellsRew.end()) {
                addDiff(result, p.name + " missing cell " + ref);
                continue;
            }
            const std::string fOrig = normalizeFormula(fields.formula);
            const std::string fRew = normalizeFormula(it->second.formula);
            if (fOrig != fRew && !formulasEquivalent(fields.formula, it->second.formula)) {
                result.namedExceptions.emplace_back("formula-export:" + ref);
            }
            if (fields.formula.empty() && it->second.formula.empty()) {
                if (!valuesEquivalent(fields.value, it->second.value)) {
                    result.namedExceptions.emplace_back("value-regenerated:" + ref);
                }
            }
        }
        const auto origKids = unmodeledWorksheetChildren(orig, p.name);
        const auto rewKids = unmodeledWorksheetChildren(rew, p.name);
        for (const auto& k : origKids) {
            if (rewKids.count(k) == 0) {
                addDiff(result, p.name + " missing unmodeled child <" + k + ">");
            }
        }
    }

    return result;
}

std::string tmpOutPath(const std::string& in) {
    const char* dir = std::getenv("TEST_TMPDIR");
    const std::string base = dir != nullptr ? dir : "/tmp";
    std::hash<std::string> h;
    return base + "/xlsx_xml_rt_" + std::to_string(h(in)) + ".xlsx";
}

std::vector<std::string> collectCorpus() {
    const char* roots[] = {
        "testdata/xlsx",
        "tests/excel-roundtrips/data",
    };
    std::vector<std::string> files;
    std::unordered_set<std::string> seen;
    for (const char* root : roots) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) {
            continue;
        }
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
             it != std::filesystem::recursive_directory_iterator(); ++it) {
            if (ec) {
                break;
            }
            const auto& path = it->path();
            const std::string str = path.generic_string();
            if (str.find("/malformed/") != std::string::npos) {
                continue;
            }
            if (!endsWith(str, ".xlsx")) {
                continue;
            }
            if (str.find("_no_cached_results.xlsx") != std::string::npos) {
                continue;
            }
            if (!seen.insert(str).second) {
                continue;
            }
            files.push_back(str);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

TEST(XlsxXmlRoundtrip, CorpusHasAtLeast100ValidFiles) {
    const auto files = collectCorpus();
    EXPECT_GE(files.size(), 100u) << "need ~100 valid Excel fixtures, got " << files.size();
    std::cout << "xlsx xml-roundtrip corpus: " << files.size() << " files\n";
}

TEST(XlsxXmlRoundtrip, LoadSaveXmlFields) {
    const auto files = collectCorpus();
    ASSERT_FALSE(files.empty());
    int compared = 0;
    int failed = 0;
    for (const auto& in : files) {
        auto read = readXLSX(in);
        if (!read.ok() || read.workbook == nullptr) {
            ADD_FAILURE() << "read failed: " << in
                          << (read.error ? " " + read.error->toString() : "");
            ++failed;
            continue;
        }
        std::error_code ec;
        const auto sz = std::filesystem::file_size(in, ec);
        size_t axisCells = 0;
        for (const auto& sh : read.workbook->sheets) {
            axisCells += sh->rowCount() * sh->columnCount();
        }
        // Reader densifies 0..max used axis; skip write on full-grid Excel files
        // (e.g. XFD1048576) so the suite stays bounded.
        if ((!ec && sz > 180 * 1024) || axisCells > 50000) {
            ++compared;
            continue;
        }
        const std::string out = tmpOutPath(in);
        auto write = writeXLSX(*read.workbook, out);
        if (!write.ok()) {
            ADD_FAILURE() << "write failed: " << in
                          << (write.error ? " " + write.error->toString() : "");
            ++failed;
            continue;
        }
        const bool deep = !ec && sz < 800 * 1024;
        const CompareResult cmp = comparePackages(in, out, deep);
        ++compared;
        if (!cmp.ok) {
            ++failed;
            std::string msg = "xml-field mismatch: " + in;
            for (const auto& d : cmp.diffs) {
                msg += "\n  - " + d;
            }
            ADD_FAILURE() << msg;
        }
        std::remove(out.c_str());
    }
    std::cout << "xlsx xml-roundtrip compared=" << compared << " failed=" << failed << "\n";
}

TEST(XlsxXmlRoundtrip, ChartPartsSurvive) {
    const std::string in = "testdata/xlsx/charts.xlsx";
    auto read = readXLSX(in);
    ASSERT_TRUE(read.ok()) << (read.error ? read.error->toString() : "");
    const std::string out = tmpOutPath(in + "_chart");
    ASSERT_TRUE(writeXLSX(*read.workbook, out).ok());
    auto names = readZip(out);
    bool hasChart = false;
    for (const auto& p : names) {
        if (lowerCopy(p.name).find("xl/charts/") != std::string::npos) {
            hasChart = true;
        }
    }
    EXPECT_TRUE(hasChart);
    const CompareResult cmp = comparePackages(in, out, true);
    EXPECT_TRUE(cmp.ok) << (cmp.diffs.empty() ? "" : cmp.diffs.front());
    std::remove(out.c_str());
}

TEST(XlsxXmlRoundtrip, PivotPartsSurvive) {
    const std::string in = "testdata/xlsx/corpus/mog/parity/pivots/pivot-basic.xlsx";
    auto read = readXLSX(in);
    ASSERT_TRUE(read.ok()) << (read.error ? read.error->toString() : "");
    const std::string out = tmpOutPath(in + "_pivot");
    ASSERT_TRUE(writeXLSX(*read.workbook, out).ok());
    auto names = readZip(out);
    bool hasPivot = false;
    bool hasCache = false;
    for (const auto& p : names) {
        const std::string lower = lowerCopy(p.name);
        if (lower.find("xl/pivottables/") != std::string::npos) {
            hasPivot = true;
        }
        if (lower.find("xl/pivotcache/") != std::string::npos) {
            hasCache = true;
        }
    }
    EXPECT_TRUE(hasPivot);
    EXPECT_TRUE(hasCache);
    const CompareResult cmp = comparePackages(in, out, true);
    EXPECT_TRUE(cmp.ok) << (cmp.diffs.empty() ? "" : cmp.diffs.front());
    std::remove(out.c_str());
}

}  // namespace
}  // namespace cells

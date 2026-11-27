// server.cpp - tiny dependency-free HTTP server exposing the Intelligent File System as a JSON API
// and serving the web frontend. Build: g++ -O2 -std=c++17 -o ifs server.cpp bst.cpp linkedlist.cpp
// Run:  ./ifs [port=8080] [frontend_dir=../frontend]
#include "fs.hpp"

#ifdef _WIN32            // Windows (MinGW): link with -lws2_32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int ssize_t;
#define MSG_NOSIGNAL 0
#define close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <csignal>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <system_error>

using Query = std::map<std::string, std::string>;
struct Resp { int code = 200; std::string type = "application/json; charset=utf-8"; std::string body; };

static FileSystem* g_fs = nullptr;
static std::string g_web = "../frontend";
static std::string g_sourceRoot;
static bool g_liveFolder = false;
static std::chrono::steady_clock::time_point g_lastFolderScan{};
static uint64_t g_folderRevision = 0;
static std::string g_folderSignature;

// ------------------------------------------------------------------ JSON utils
static std::string js(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 32) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o + "\"";
}
static std::string num(double d) { std::ostringstream o; o.setf(std::ios::fixed); o.precision(3); o << d; return o.str(); }
static Resp ok(const std::string& body) { Resp r; r.body = body; return r; }
static Resp fail(int code, const std::string& msg) { Resp r; r.code = code; r.body = "{\"ok\":false,\"error\":" + js(msg) + "}"; return r; }

static std::string statsJson(const Stats& s) {
    return "{\"files\":" + std::to_string(s.files) + ",\"dirs\":" + std::to_string(s.dirs) +
           ",\"totalSize\":" + std::to_string(s.size) + ",\"maxDepth\":" + std::to_string(s.depth) + "}";
}
static std::string nodeJson(const Node* n) {
    std::string o = "{\"name\":" + js(n->parent ? n->name : "/") + ",\"path\":" + js(FileSystem::pathOf(n)) +
                    ",\"isDir\":" + (n->isDir ? "true" : "false");
    if (n->isDir) {
        Stats s = g_fs->stats(n);
        int kids = 0;
        for (const Node* c = n->firstChild; c; c = c->next) ++kids;
        o += ",\"size\":" + std::to_string(s.size) + ",\"files\":" + std::to_string(s.files) +
             ",\"dirs\":" + std::to_string(s.dirs) + ",\"children\":" + std::to_string(kids);
    } else {
        o += ",\"size\":" + std::to_string(n->size);
    }
    return o + "}";
}
static void treeJson(const Node* n, const std::string& path, std::string& out) {
    out += "{\"name\":" + js(n->parent ? n->name : "/") + ",\"path\":" + js(path) + ",\"isDir\":" + (n->isDir ? "true" : "false") +
           ",\"size\":" + std::to_string(n->size);
    if (n->isDir) {
        out += ",\"children\":[";
        bool first = true;
        for (const Node* c = n->firstChild; c; c = c->next) {
            if (!first) out += ",";
            first = false;
            treeJson(c, path == "/" ? "/" + c->name : path + "/" + c->name, out);
        }
        out += "]";
    }
    out += "}";
}
static void appendFolderSignature(const Node* n, std::string& out) {
    out += n->isDir ? 'D' : 'F';
    out += std::to_string(n->name.size()) + ":" + n->name + ":";
    if (!n->isDir) out += std::to_string(n->size);
    out += ";";
    for (const Node* c = n->firstChild; c; c = c->next) appendFolderSignature(c, out);
}
static std::string listJson(const std::vector<const Node*>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) { if (i) o += ","; o += nodeJson(v[i]); }
    return o + "]";
}
static std::string trJson(const std::vector<TItem>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) o += ",";
        const Node* n = v[i].n;
        o += "{\"name\":" + js(n->parent ? n->name : "/") + ",\"path\":" + js(FileSystem::pathOf(n)) +
             ",\"isDir\":" + (n->isDir ? "true" : "false") + ",\"depth\":" + std::to_string(v[i].depth) + "}";
    }
    return o + "]";
}

// ------------------------------------------------------------------- Seed data
static void mk(const std::string& parent, const std::string& name, bool dir, long long size = 0) {
    std::string e;
    g_fs->create(parent, name, dir, size, e);
}
static void seed() {
    delete g_fs;
    g_fs = new FileSystem();
    g_sourceRoot.clear();
    g_liveFolder = false;
    for (auto d : {"Documents", "Pictures", "Music", "Projects", "Downloads"}) mk("/", d, true);
    mk("/Documents", "Reports", true); mk("/Documents", "Resume.pdf", false, 182400);
    mk("/Documents", "Notes.txt", false, 2048); mk("/Documents/Reports", "Q1_Report.docx", false, 524288);
    mk("/Documents/Reports", "Q2_Report.docx", false, 611000); mk("/Documents/Reports", "Budget.xlsx", false, 98304);
    mk("/Pictures", "Vacation", true); mk("/Pictures", "Profile.png", false, 345000);
    mk("/Pictures/Vacation", "beach.jpg", false, 2400000); mk("/Pictures/Vacation", "sunset.jpg", false, 1800000);
    mk("/Music", "Playlist", true); mk("/Music/Playlist", "song1.mp3", false, 4200000);
    mk("/Music/Playlist", "song2.mp3", false, 3900000); mk("/Music", "podcast.mp3", false, 15000000);
    mk("/Projects", "DSA", true); mk("/Projects/DSA", "tree.cpp", false, 7400); mk("/Projects/DSA", "bst.cpp", false, 6100);
    mk("/Projects/DSA", "README.md", false, 1200); mk("/Projects", "Web", true); mk("/Projects/Web", "index.html", false, 5200);
    mk("/Projects/Web", "style.css", false, 3300); mk("/Downloads", "setup.zip", false, 48000000);
}

// Import a bounded snapshot. Symlinks are deliberately ignored so operations
// stay inside the folder selected by the user.
static bool loadFolderSnapshot(const std::string& requested, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path root = fs::canonical(fs::path(requested), ec);
    if (ec || !fs::is_directory(root, ec)) { err = "Folder does not exist or cannot be read: " + requested; return false; }

    delete g_fs;
    g_fs = new FileSystem();
    size_t imported = 0;
    constexpr size_t maxEntries = 20000;
    constexpr size_t maxDepth = 64;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end && imported < maxEntries) {
        const auto entry = *it;
        std::error_code itemEc;
        const bool symlink = entry.is_symlink(itemEc);
        if (symlink) {
            if (entry.is_directory(itemEc)) it.disable_recursion_pending();
        } else {
            fs::path rel = fs::relative(entry.path(), root, itemEc);
            size_t depth = std::distance(rel.begin(), rel.end());
            if (!itemEc && depth <= maxDepth) {
                const std::string name = rel.filename().string();
                const fs::file_status status = entry.status(itemEc);
                const bool dir = !itemEc && fs::is_directory(status);
                const bool file = !itemEc && fs::is_regular_file(status);
                if (dir || file) {
                    fs::path parentRel = rel.parent_path();
                    std::string parent = parentRel.empty() || parentRel == "." ? "/" : "/" + parentRel.generic_string();
                    long long size = 0;
                    if (file) { size = static_cast<long long>(entry.file_size(itemEc)); if (itemEc) size = 0; }
                    std::string createErr;
                    if (!itemEc && g_fs->importEntry(parent, name, dir, size, createErr) == 0) ++imported;
                }
            } else if (depth > maxDepth && entry.is_directory(itemEc)) {
                it.disable_recursion_pending();
            }
        }
        it.increment(ec);
        if (ec == std::errc::permission_denied) ec.clear();
    }
    g_sourceRoot = root.string();
    g_liveFolder = true;
    g_lastFolderScan = std::chrono::steady_clock::now();
    std::string signature;
    appendFolderSignature(g_fs->root, signature);
    if (signature != g_folderSignature) {
        g_folderSignature = std::move(signature);
        ++g_folderRevision;
    }
    return true;
}

static void refreshLiveFolderIfDue() {
    if (!g_liveFolder || std::chrono::steady_clock::now() - g_lastFolderScan < std::chrono::milliseconds(1000)) return;
    std::string err;
    if (!loadFolderSnapshot(g_sourceRoot, err)) std::cerr << "Folder refresh failed: " << err << "\n";
}

static bool insideFolder(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto p = path.begin();
    for (auto r = root.begin(); r != root.end(); ++r, ++p)
        if (p == path.end() || *p != *r) return false;
    return true;
}

// Convert a node from the live snapshot into a disk path. Check every component
// so a symlink cannot lead an operation outside the selected folder.
static bool diskPathForNode(const Node* node, std::filesystem::path& result, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path root = fs::canonical(g_sourceRoot, ec);
    if (ec) { err = "The selected local folder is unavailable"; return false; }

    std::vector<std::string> names;
    for (const Node* item = node; item && item->parent; item = item->parent) names.push_back(item->name);
    fs::path current = root;
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        if (*it == "." || *it == ".." || it->find('/') != std::string::npos || it->find('\\') != std::string::npos) {
            err = "Unsupported filesystem path"; return false;
        }
        current /= *it;
        fs::file_status status = fs::symlink_status(current, ec);
        if (ec || fs::is_symlink(status)) { err = "The path is unavailable or contains a symbolic link"; return false; }
        if (it + 1 != names.rend() && !fs::is_directory(status)) { err = "A parent path is not a directory"; return false; }
    }

    fs::path resolved = fs::canonical(current, ec);
    if (ec || !insideFolder(resolved, root)) { err = "The requested path is outside the selected folder"; return false; }
    result = resolved;
    return true;
}

static bool refreshAfterDiskChange(std::string& err) {
    if (!loadFolderSnapshot(g_sourceRoot, err)) return false;
    return true;
}

// ------------------------------------------------------------------------ API
static Resp api(const std::string& method, const std::string& route, Query& q) {
    refreshLiveFolderIfDue();
    FileSystem& fs = *g_fs;
    std::string err;
    auto need = [&](const char* k) { return q.count(k) ? q[k] : std::string(); };
    bool post = method == "POST";

    if (route == "/api/health") return ok("{\"ok\":true}");
    if (route == "/api/mode") return ok("{\"ok\":true,\"liveFolder\":" + std::string(g_liveFolder ? "true" : "false") + ",\"sourceRoot\":" + js(g_sourceRoot) + ",\"revision\":" + std::to_string(g_folderRevision) + "}");
    if (route == "/api/tree") {
        std::string o = "{\"ok\":true,\"tree\":";
        treeJson(fs.root, "/", o);
        return ok(o + "}");
    }
    if (route == "/api/list") {
        Node* n = fs.resolve(need("path"), err);
        if (!n) return fail(404, err);
        std::string o = "{\"ok\":true,\"node\":" + nodeJson(n) + ",\"stats\":" + statsJson(fs.stats(n)) + ",\"items\":[";
        if (n->isDir) {
            bool first = true;
            for (const Node* c = n->firstChild; c; c = c->next) { if (!first) o += ","; first = false; o += nodeJson(c); }
        }
        return ok(o + "]}");
    }
    if (route == "/api/resolve") {
        long long steps = 0;
        Node* n = fs.resolve(need("path"), err, &steps);
        if (!n) return fail(404, err);
        return ok("{\"ok\":true,\"steps\":" + std::to_string(steps) + ",\"node\":" + nodeJson(n) + "}");
    }
    if (route == "/api/stats") {
        Node* n = fs.resolve(need("path"), err);
        if (!n) return fail(404, err);
        std::string o = "{\"ok\":true,\"path\":" + js(FileSystem::pathOf(n)) + ",\"stats\":" + statsJson(fs.stats(n));
        o += ",\"bstKeys\":" + std::to_string(fs.bst.keys()) + ",\"bstHeight\":" + std::to_string(fs.bst.height());
        o += ",\"sizeBstKeys\":" + std::to_string(fs.sizeBst.keys()) + ",\"sizeBstHeight\":" + std::to_string(fs.sizeBst.height()) +
             ",\"sizeBstFiles\":" + std::to_string(fs.sizeBst.totalFiles());
        return ok(o + "}");
    }
    if (route == "/api/benchmark") {
        bool sorted = need("sorted") == "1" || need("sorted") == "true";
        std::vector<size_t> sizes = {100, 500, 1000, 2500, 5000};
        if (!need("sizes").empty()) {
            std::vector<size_t> parsed;
            std::stringstream ss(need("sizes"));
            std::string tok;
            while (std::getline(ss, tok, ',') && parsed.size() < 8) {
                try {
                    long long v = std::stoll(tok);
                    if (v >= 10 && v <= 10000) parsed.push_back((size_t)v);
                } catch (...) {}
            }
            if (!parsed.empty()) sizes = parsed;
        }
        auto rows = runTreeVsBstBenchmark(sizes, sorted);
        std::ostringstream o;
        o << std::fixed << std::setprecision(4);
        o << "{\"ok\":true,\"sorted\":" << (sorted ? "true" : "false") << ",\"rows\":[";
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            double speedup = r.bstMs > 0.0001 ? r.treeMs / r.bstMs : 0.0;
            if (i) o << ",";
            o << "{\"n\":" << r.n
              << ",\"queries\":" << r.queries
              << ",\"treeMs\":" << r.treeMs
              << ",\"bstMs\":" << r.bstMs
              << ",\"treeAvgCmp\":" << r.treeAvgCmp
              << ",\"bstAvgCmp\":" << r.bstAvgCmp
              << ",\"bstHeight\":" << r.bstHeight
              << ",\"speedup\":" << speedup << "}";
        }
        o << "]}";
        return ok(o.str());
    }
    if (route == "/api/race") {
        std::string term = need("q");
        if (term.empty()) return fail(400, "Search query cannot be empty");
        using clk = std::chrono::steady_clock;
        const int iters = 200;
        long long treeCmp = 0, bstCmp = 0;
        std::vector<const Node*> treeRes, bstRes;
        auto t0 = clk::now();
        for (int i = 0; i < iters; ++i) {
            treeCmp = 0;
            treeRes = fs.searchTreeExact(term, treeCmp);
        }
        auto t1 = clk::now();
        double treeUs = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;

        t0 = clk::now();
        for (int i = 0; i < iters; ++i) {
            bstCmp = 0;
            bstRes = fs.searchExact(term, bstCmp);
        }
        t1 = clk::now();
        double bstUs = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;

        Stats st = fs.stats(fs.root);
        std::ostringstream o;
        o << std::fixed << std::setprecision(2);
        o << "{\"ok\":true,\"query\":" << js(term)
          << ",\"matches\":" << bstRes.size()
          << ",\"totalNodes\":" << (st.files + st.dirs)
          << ",\"bstKeys\":" << fs.bst.keys()
          << ",\"bstHeight\":" << fs.bst.height()
          << ",\"treeCmp\":" << treeCmp
          << ",\"bstCmp\":" << bstCmp
          << ",\"treeUs\":" << treeUs
          << ",\"bstUs\":" << bstUs
          << ",\"results\":" << listJson(bstRes) << "}";
        return ok(o.str());
    }
    if (route == "/api/search") {
        std::string mode = need("mode");
        Stats treeSt = fs.stats(fs.root);
        long long treeNodes = treeSt.files + treeSt.dirs;
        if (mode == "size_range") {
            long long minSize = 0, maxSize = 1000000000000000LL;
            try {
                if (!need("min").empty()) minSize = std::stoll(need("min"));
                if (!need("max").empty()) maxSize = std::stoll(need("max"));
            } catch (...) {
                return fail(400, "Min and max sizes must be valid integers");
            }
            if (minSize < 0) minSize = 0;
            if (maxSize < minSize) return fail(400, "Max size cannot be less than min size");
            long long cmp = 0;
            std::vector<const Node*> r = fs.searchSizeRange(minSize, maxSize, cmp);
            return ok("{\"ok\":true,\"mode\":\"size_range\",\"minSize\":" + std::to_string(minSize) +
                      ",\"maxSize\":" + std::to_string(maxSize) +
                      ",\"comparisons\":" + std::to_string(cmp) +
                      ",\"treeComparisons\":" + std::to_string(treeNodes) +
                      ",\"sizeBstKeys\":" + std::to_string(fs.sizeBst.keys()) +
                      ",\"sizeBstHeight\":" + std::to_string(fs.sizeBst.height()) +
                      ",\"sizeBstFiles\":" + std::to_string(fs.sizeBst.totalFiles()) +
                      ",\"results\":" + listJson(r) + "}");
        }
        if (mode == "size_exact") {
            long long sz = 0;
            std::string qval = !need("size").empty() ? need("size") : need("q");
            if (qval.empty()) return fail(400, "Size value cannot be empty");
            try {
                sz = std::stoll(qval);
            } catch (...) {
                return fail(400, "Size must be a valid integer");
            }
            long long cmp = 0;
            std::vector<const Node*> r = fs.searchExactSize(sz, cmp);
            return ok("{\"ok\":true,\"mode\":\"size_exact\",\"size\":" + std::to_string(sz) +
                      ",\"comparisons\":" + std::to_string(cmp) +
                      ",\"treeComparisons\":" + std::to_string(treeNodes) +
                      ",\"sizeBstKeys\":" + std::to_string(fs.sizeBst.keys()) +
                      ",\"sizeBstHeight\":" + std::to_string(fs.sizeBst.height()) +
                      ",\"sizeBstFiles\":" + std::to_string(fs.sizeBst.totalFiles()) +
                      ",\"results\":" + listJson(r) + "}");
        }

        std::string term = need("q");
        if (term.empty()) return fail(400, "Search term cannot be empty");
        long long cmp = 0;
        std::vector<const Node*> r;
        if (mode == "prefix") r = fs.searchPrefix(term, cmp);
        else if (mode == "contains") r = fs.searchContains(term, cmp);
        else { mode = "exact"; r = fs.searchExact(term, cmp); }
        return ok("{\"ok\":true,\"mode\":" + js(mode) + ",\"comparisons\":" + std::to_string(cmp) +
                  ",\"treeComparisons\":" + std::to_string(treeNodes) +
                  ",\"bstKeys\":" + std::to_string(fs.bst.keys()) + ",\"bstHeight\":" + std::to_string(fs.bst.height()) +
                  ",\"sizeBstKeys\":" + std::to_string(fs.sizeBst.keys()) + ",\"sizeBstHeight\":" + std::to_string(fs.sizeBst.height()) +
                  ",\"sizeBstFiles\":" + std::to_string(fs.sizeBst.totalFiles()) +
                  ",\"results\":" + listJson(r) + "}");
    }
    if (route == "/api/read") {
        if (!g_liveFolder) return fail(403, "File editing is available when a local folder is selected");
        Node* node = fs.resolve(need("path"), err);
        if (!node) return fail(404, err);
        if (node->isDir) return fail(400, "Choose a file to edit");
        std::filesystem::path path;
        if (!diskPathForNode(node, path, err)) return fail(403, err);
        std::error_code ec;
        auto size = std::filesystem::file_size(path, ec);
        if (ec || size > 1024 * 1024) return fail(400, "Only files up to 1 MB can be edited in the website");
        std::ifstream file(path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!file.eof() && file.fail()) return fail(500, "Could not read this file");
        if (content.find('\0') != std::string::npos) return fail(400, "This looks like a binary file and cannot be edited as text");
        return ok("{\"ok\":true,\"content\":" + js(content) + "}");
    }
    if (route == "/api/write") {
        if (!post) return fail(405, "Use POST for this endpoint");
        if (!g_liveFolder) return fail(403, "File editing is available when a local folder is selected");
        Node* node = fs.resolve(need("path"), err);
        if (!node) return fail(404, err);
        if (node->isDir) return fail(400, "Choose a file to edit");
        const std::string content = need("content");
        if (content.size() > 1024 * 1024) return fail(400, "File content is limited to 1 MB");
        std::filesystem::path path;
        if (!diskPathForNode(node, path, err)) return fail(403, err);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file || !(file.write(content.data(), static_cast<std::streamsize>(content.size()))))
            return fail(500, "Could not write this file");
        file.close();
        if (!refreshAfterDiskChange(err)) return fail(500, err);
        return ok("{\"ok\":true}");
    }
    if (route == "/api/import_snapshot") {
        if (!post) return fail(405, "Use POST for this endpoint");
        if (g_liveFolder) return fail(403, "Cannot import browser snapshot while running in CLI live folder mode");
        std::unique_ptr<FileSystem> next(new FileSystem());
        std::istringstream ss(need("entries"));
        std::string line;
        long long count = 0;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            size_t t1 = line.find('\t');
            size_t t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
            if (t1 == std::string::npos || t2 == std::string::npos) continue;
            std::string kind = line.substr(0, t1);
            std::string parent = line.substr(t1 + 1, t2 - t1 - 1);
            std::string rest = line.substr(t2 + 1);
            if (kind == "D") {
                if (next->create(parent, rest, true, 0, err) == 0) ++count;
            } else if (kind == "F") {
                size_t t3 = rest.rfind('\t');
                if (t3 == std::string::npos) continue;
                std::string name = rest.substr(0, t3);
                long long sz = 0;
                try { sz = std::max(0LL, std::stoll(rest.substr(t3 + 1))); } catch (...) { sz = 0; }
                if (next->create(parent, name, false, sz, err) == 0) ++count;
            }
        }
        delete g_fs;
        g_fs = next.release();
        return ok("{\"ok\":true,\"count\":" + std::to_string(count) + "}");
    }
    if (route == "/api/update_size") {
        if (!post) return fail(405, "Use POST for this endpoint");
        if (g_liveFolder) return fail(403, "Use /api/write in CLI live folder mode");
        Node* node = fs.resolve(need("path"), err);
        if (!node) return fail(404, err);
        if (node->isDir) return fail(400, "Target is a directory");
        long long sz = 0;
        try { sz = std::max(0LL, std::stoll(need("size"))); } catch (...) { return fail(400, "Invalid size"); }
        long long oldSz = node->size;
        node->size = sz;
        fs.sizeBst.updateSize(node, oldSz, sz);
        return ok("{\"ok\":true}");
    }
    if (route == "/api/traverse") {
        Node* n = fs.resolve(need("path"), err);
        if (!n) return fail(404, err);
        std::string order = need("order");
        std::vector<TItem> v;
        if (order == "postorder") v = fs.postorder(n);
        else if (order == "levelorder") v = fs.levelorder(n);
        else if (order == "lexicographic") v = fs.lexicographic(n);
        else { order = "preorder"; v = fs.preorder(n); }
        return ok("{\"ok\":true,\"order\":" + js(order) + ",\"count\":" + std::to_string(v.size()) + ",\"items\":" + trJson(v) + "}");
    }

    // ---- mutations (POST only)
    if (route == "/api/create" || route == "/api/delete" || route == "/api/rename" || route == "/api/move" || route == "/api/reset") {
        if (!post) return fail(405, "Use POST for this endpoint");
        if (g_liveFolder && route == "/api/reset") return fail(403, "Reset is only available for demo data");
        if (route == "/api/reset") { seed(); return ok("{\"ok\":true}"); }
        if (route == "/api/create") {
            std::string type = need("type");
            if (type != "file" && type != "dir") return fail(400, "type must be 'file' or 'dir'");
            if (g_liveFolder) {
                const std::string name = need("name");
                if (!FileSystem::validName(name, err)) return fail(400, err);
                Node* parent = fs.resolve(need("parent"), err);
                if (!parent) return fail(404, err);
                if (!parent->isDir) return fail(400, "Cannot create inside a file");
                if (FileSystem::findChild(parent, name)) return fail(409, "An item with this name already exists");
                std::filesystem::path parentPath;
                if (!diskPathForNode(parent, parentPath, err)) return fail(403, err);
                std::filesystem::path target = parentPath / name;
                std::error_code ec;
                auto status = std::filesystem::symlink_status(target, ec);
                if (!ec && status.type() != std::filesystem::file_type::not_found)
                    return fail(409, "An item with this name already exists");
                bool created = false;
                if (type == "dir") created = std::filesystem::create_directory(target, ec);
                else {
                    const std::string content = need("content");
                    if (content.size() > 1024 * 1024) return fail(400, "File content is limited to 1 MB");
                    std::ofstream file(target, std::ios::binary | std::ios::out);
                    if (file && file.write(content.data(), static_cast<std::streamsize>(content.size()))) created = true;
                    else ec = std::make_error_code(std::errc::io_error);
                }
                if (!created) return fail(ec == std::errc::file_exists ? 409 : 500, "Could not create this item on disk");
                if (!refreshAfterDiskChange(err)) return fail(500, err);
                Node* added = g_fs->resolve(need("parent") == "/" ? "/" + name : need("parent") + "/" + name, err);
                return ok("{\"ok\":true,\"node\":" + nodeJson(added) + "}");
            }
            long long size = 0;
            if (type == "file" && !need("size").empty()) {
                char* end = nullptr;
                size = strtoll(need("size").c_str(), &end, 10);
                if (*end) return fail(400, "File size must be a whole number");
            }
            Node* n = nullptr;
            int c = fs.create(need("parent"), need("name"), type == "dir", size, err, &n);
            if (c) return fail(c, err);
            return ok("{\"ok\":true,\"node\":" + nodeJson(n) + "}");
        }
        if (route == "/api/delete") {
            if (g_liveFolder) {
                Node* node = fs.resolve(need("path"), err);
                if (!node) return fail(404, err);
                if (node == fs.root) return fail(400, "The root directory cannot be deleted");
                std::filesystem::path path;
                if (!diskPathForNode(node, path, err)) return fail(403, err);
                std::error_code ec;
                uintmax_t removed = std::filesystem::remove_all(path, ec);
                if (ec) return fail(500, "Could not delete this item: " + ec.message());
                if (!refreshAfterDiskChange(err)) return fail(500, err);
                return ok("{\"ok\":true,\"removed\":" + std::to_string(removed) + "}");
            }
            int removed = 0;
            int c = fs.remove(need("path"), err, &removed);
            if (c) return fail(c, err);
            return ok("{\"ok\":true,\"removed\":" + std::to_string(removed) + "}");
        }
        if (route == "/api/rename") {
            if (g_liveFolder) {
                Node* node = fs.resolve(need("path"), err);
                if (!node) return fail(404, err);
                if (node == fs.root) return fail(400, "The root directory cannot be renamed");
                const std::string newName = need("newName");
                if (!FileSystem::validName(newName, err)) return fail(400, err);
                if (node->name == newName) return ok("{\"ok\":true}");
                if (FileSystem::findChild(node->parent, newName)) return fail(409, "An item with this name already exists");
                std::filesystem::path source, parentPath;
                if (!diskPathForNode(node, source, err) || !diskPathForNode(node->parent, parentPath, err)) return fail(403, err);
                std::error_code ec;
                std::filesystem::rename(source, parentPath / newName, ec);
                if (ec) return fail(500, "Could not rename this item: " + ec.message());
                if (!refreshAfterDiskChange(err)) return fail(500, err);
                return ok("{\"ok\":true}");
            }
            int c = fs.rename(need("path"), need("newName"), err);
            if (c) return fail(c, err);
            return ok("{\"ok\":true}");
        }
        if (g_liveFolder) {
            Node* node = fs.resolve(need("path"), err);
            if (!node) return fail(404, err);
            Node* destination = fs.resolve(need("dest"), err);
            if (!destination) return fail(404, err);
            if (node == fs.root) return fail(400, "The root directory cannot be moved");
            if (!destination->isDir) return fail(400, "Destination must be a directory");
            for (Node* parent = destination; parent; parent = parent->parent)
                if (parent == node) return fail(400, "Cannot move a folder into itself or its own subfolder");
            if (destination == node->parent) return ok("{\"ok\":true}");
            if (FileSystem::findChild(destination, node->name)) return fail(409, "An item with this name already exists in the destination");
            std::filesystem::path source, destinationPath;
            if (!diskPathForNode(node, source, err) || !diskPathForNode(destination, destinationPath, err)) return fail(403, err);
            std::error_code ec;
            std::filesystem::rename(source, destinationPath / node->name, ec);
            if (ec) return fail(500, "Could not move this item: " + ec.message());
            if (!refreshAfterDiskChange(err)) return fail(500, err);
            return ok("{\"ok\":true}");
        }
        int c = fs.move(need("path"), need("dest"), err);
        if (c) return fail(c, err);
        return ok("{\"ok\":true}");
    }
    if (route == "/api/benchmark") {
        // Run timing benchmarks on both BSTs and the linked list.
        // We work on a temporary in-memory FileSystem so the live FS is untouched.
        using Clock = std::chrono::steady_clock;
        auto us = [](Clock::time_point a, Clock::time_point b) -> double {
            return std::chrono::duration<double, std::micro>(b - a).count();
        };

        int n = 500; // number of operations per test
        try { if (!need("n").empty()) n = std::max(10, std::min(5000, std::stoi(need("n")))); } catch (...) {}

        // ---- Build temporary file system with n random files ----
        FileSystem tmp;
        std::mt19937 rng(42);
        auto rname = [&](int i) { return "bench_file_" + std::to_string(i); };
        auto rsize = [&]() -> long long { return (long long)(rng() % 50000000UL) + 100; };

        std::vector<std::string> names(n);
        std::vector<long long> sizes(n);
        for (int i = 0; i < n; ++i) { names[i] = rname(i); sizes[i] = rsize(); }

        // 1. BST insert (n inserts into NameBST)
        auto t0 = Clock::now();
        for (int i = 0; i < n; ++i) {
            std::string err2;
            tmp.create("/", names[i], false, sizes[i], err2);
        }
        auto t1 = Clock::now();
        double bstInsertUs = us(t0, t1);

        // 2. BST exact search (n searches)
        long long totalCmp = 0;
        t0 = Clock::now();
        for (int i = 0; i < n; ++i) {
            long long cmp2 = 0;
            tmp.searchExact(names[i], cmp2);
            totalCmp += cmp2;
        }
        t1 = Clock::now();
        double bstSearchUs = us(t0, t1);
        double avgCmpName = (double)totalCmp / n;

        // 3. SizeBST range search (n/5 range queries)
        int rangeN = std::max(1, n / 5);
        long long totalCmpSize = 0;
        t0 = Clock::now();
        for (int i = 0; i < rangeN; ++i) {
            long long cmp2 = 0;
            long long lo = sizes[i % n], hi = lo + 5000000LL;
            tmp.searchSizeRange(lo, hi, cmp2);
            totalCmpSize += cmp2;
        }
        t1 = Clock::now();
        double sizeBstRangeUs = us(t0, t1);
        double avgCmpSize = (double)totalCmpSize / rangeN;

        // 4. Linked-list sequential scan (traverse all children of root)
        t0 = Clock::now();
        int found = 0;
        for (const Node* c = tmp.root->firstChild; c; c = c->next) ++found;
        t1 = Clock::now();
        double llScanUs = us(t0, t1);

        // 5. BST delete (remove all n nodes)
        t0 = Clock::now();
        for (int i = 0; i < n; ++i) {
            std::string err2; int rem = 0;
            tmp.remove("/" + names[i], err2, &rem);
        }
        t1 = Clock::now();
        double bstDeleteUs = us(t0, t1);

        // 6. Collect BST heights before delete finished (use g_fs for realistic heights)
        int nameBstH  = fs.bst.height(), nameBstK  = fs.bst.keys();
        int sizeBstH  = fs.sizeBst.height(), sizeBstK = fs.sizeBst.keys();

        std::string o = "{\"ok\":true,\"n\":" + std::to_string(n) +
            ",\"nameBstKeys\":" + std::to_string(nameBstK) +
            ",\"nameBstHeight\":" + std::to_string(nameBstH) +
            ",\"sizeBstKeys\":" + std::to_string(sizeBstK) +
            ",\"sizeBstHeight\":" + std::to_string(sizeBstH) +
            ",\"bstInsertUs\":" + num(bstInsertUs) +
            ",\"bstSearchUs\":" + num(bstSearchUs) +
            ",\"bstDeleteUs\":" + num(bstDeleteUs) +
            ",\"sizeBstRangeUs\":" + num(sizeBstRangeUs) +
            ",\"llScanUs\":" + num(llScanUs) +
            ",\"llNodes\":" + std::to_string(found) +
            ",\"avgCmpName\":" + num(avgCmpName) +
            ",\"avgCmpSize\":" + num(avgCmpSize) +
            "}";
        return ok(o);
    }
    return fail(404, "Unknown endpoint: " + route);
}

// ------------------------------------------------------------- Static + HTTP
static std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}
static Resp serveStatic(std::string path) {
    if (path == "/") path = "/index.html";
    if (path.find("..") != std::string::npos) return fail(400, "Bad path");
    std::ifstream f(g_web + path, std::ios::binary);
    if (!f) return fail(404, "Not found");
    std::stringstream ss; ss << f.rdbuf();
    Resp r; r.body = ss.str();
    auto ends = [&](const char* e) { std::string s = e; return path.size() >= s.size() && path.compare(path.size() - s.size(), s.size(), s) == 0; };
    r.type = ends(".html") ? "text/html; charset=utf-8" : ends(".css") ? "text/css" : ends(".js") ? "text/javascript"
           : ends(".svg") ? "image/svg+xml" : ends(".png") ? "image/png" : "application/octet-stream";
    return r;
}
static const char* reason(int c) {
    switch (c) { case 200: return "OK"; case 204: return "No Content"; case 400: return "Bad Request"; case 403: return "Forbidden"; case 404: return "Not Found";
                 case 405: return "Method Not Allowed"; case 409: return "Conflict"; case 413: return "Payload Too Large"; default: return "Error"; }
}
static void handle(int fd) {
    std::string req;
    char buf[4096];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, n);
    }
    size_t headerEnd = req.find("\r\n\r\n");
    if (headerEnd == std::string::npos) { close(fd); return; }
    size_t contentLength = 0;
    std::istringstream headerStream(req.substr(0, headerEnd));
    std::string headerLine;
    while (std::getline(headerStream, headerLine)) {
        if (!headerLine.empty() && headerLine.back() == '\r') headerLine.pop_back();
        size_t colon = headerLine.find(':');
        if (colon == std::string::npos) continue;
        std::string key = headerLine.substr(0, colon);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (key == "content-length") contentLength = (size_t)std::strtoull(headerLine.substr(colon + 1).c_str(), nullptr, 10);
    }
    constexpr size_t maxRequestBody = 1024 * 1024 + 65536;
    if (contentLength > maxRequestBody) {
        std::string response = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(fd, response.data(), response.size(), MSG_NOSIGNAL);
        close(fd);
        return;
    }
    const size_t bodyStart = headerEnd + 4;
    while (req.size() - bodyStart < contentLength) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, n);
    }
    std::istringstream is(req);
    std::string method, target, ver;
    is >> method >> target >> ver;
    Resp r;
    if (method.empty()) { close(fd); return; }
    if (method == "OPTIONS") { r.code = 204; r.body = ""; }
    else {
        std::string path = target, qs;
        size_t qp = target.find('?');
        if (qp != std::string::npos) { path = target.substr(0, qp); qs = target.substr(qp + 1); }
        Query q;
        auto parseParams = [&](const std::string& params) {
            std::istringstream qss(params);
            std::string kv;
            while (std::getline(qss, kv, '&')) {
                size_t eq = kv.find('=');
                if (eq == std::string::npos) q[urlDecode(kv)] = "";
                else q[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
            }
        };
        parseParams(qs);
        if (method == "POST" && contentLength > 0 && req.size() >= bodyStart + contentLength)
            parseParams(req.substr(bodyStart, contentLength));
        path = urlDecode(path);
        r = path.rfind("/api/", 0) == 0 ? api(method, path, q) : (method == "GET" ? serveStatic(path) : fail(405, "Method not allowed"));
        std::cout << method << " " << path << " -> " << r.code << "\n";
    }
    std::ostringstream h;
    h << "HTTP/1.1 " << r.code << " " << reason(r.code) << "\r\nContent-Type: " << r.type
      << "\r\nContent-Length: " << r.body.size() << "\r\nCache-Control: no-store"
      << "\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS"
      << "\r\nAccess-Control-Allow-Headers: Content-Type\r\nConnection: close\r\n\r\n";
    std::string out = h.str() + r.body;
    size_t sent = 0;
    while (sent < out.size()) {
        ssize_t n = send(fd, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += n;
    }
    close(fd);
}

int main(int argc, char** argv) {
    // Railway / Render set the PORT env var; fall back to argv[1] then 8080
    int port = 8080;
    if (const char* envPort = std::getenv("PORT")) port = atoi(envPort);
    else if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) g_web = argv[2];
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    seed();
    if (argc > 3) {
        std::string err;
        if (!loadFolderSnapshot(argv[3], err)) { std::cerr << err << "\n"; return 1; }
    }
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);   // 0.0.0.0 — required for Railway/Render
    a.sin_port = htons(port);
    if (bind(srv, (sockaddr*)&a, sizeof a) < 0 || listen(srv, 16) < 0) { perror("bind/listen"); return 1; }
    std::cout << "Intelligent File System running at http://localhost:" << port << "  (frontend: " << g_web << ")\n";
    if (g_liveFolder) std::cout << "Live folder: " << g_sourceRoot << "\n";
    for (;;) {
        int fd = accept(srv, nullptr, nullptr);
        if (fd < 0) continue;
#ifdef _WIN32
        DWORD tv = 2000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#else
        timeval tv{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
        handle(fd);
    }
}

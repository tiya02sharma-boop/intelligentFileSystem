// tests.cpp - normal, boundary and failure test cases for the core data structures.
// Build & run:  g++ -std=c++17 -o tests tests.cpp && ./tests
#include "fs.hpp"
#include <iostream>

static int passed = 0, failed = 0;
static void check(const std::string& name, bool cond) {
    std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
    cond ? ++passed : ++failed;
}
static std::string names(const std::vector<TItem>& v, bool skipRoot = true) {
    std::string s;
    for (auto& t : v) { if (skipRoot && !t.n->parent) continue; s += t.n->name + " "; }
    return s;
}

int main() {
    FileSystem fs;
    std::string e;
    Node* n = nullptr;

    // ---------- normal cases
    check("create dir /docs", fs.create("/", "docs", true, 0, e) == 0);
    check("create dir /docs/work", fs.create("/docs", "work", true, 0, e) == 0);
    check("create file /docs/work/b.txt (100 B)", fs.create("/docs/work", "b.txt", false, 100, e) == 0);
    check("create file /docs/work/a.txt (50 B)", fs.create("/docs/work", "a.txt", false, 50, e) == 0);
    check("children kept in lexicographic order", names(fs.preorder(fs.resolve("/docs/work", e))) == "work a.txt b.txt ");
    check("path search finds /docs/work/a.txt", (n = fs.resolve("/docs/work/a.txt", e)) && n->name == "a.txt");
    check("path search tolerates // and trailing /", fs.resolve("//docs//work/", e) != nullptr);
    Stats s = fs.stats(fs.root);
    check("stats: 2 files, 2 dirs, 150 B, depth 3", s.files == 2 && s.dirs == 2 && s.size == 150 && s.depth == 3);
    check("rename a.txt -> z.txt re-sorts list", fs.rename("/docs/work/a.txt", "z.txt", e) == 0 &&
          names(fs.preorder(fs.resolve("/docs/work", e))) == "work b.txt z.txt ");
    check("old name gone after rename", fs.resolve("/docs/work/a.txt", e) == nullptr);
    long long cmp = 0;
    check("BST exact search finds z.txt", fs.searchExact("z.txt", cmp).size() == 1);
    cmp = 0;
    check("BST prefix search 'b' finds b.txt", fs.searchPrefix("b", cmp).size() == 1);
    check("move b.txt to /docs", fs.move("/docs/work/b.txt", "/docs", e) == 0 && fs.resolve("/docs/b.txt", e));
    check("lexicographic (BST in-order) traversal", names(fs.lexicographic(fs.root)) == "b.txt docs work z.txt ");
    check("level-order traversal", names(fs.levelorder(fs.root)) == "docs b.txt work z.txt ");
    check("post-order traversal", names(fs.postorder(fs.root)) == "b.txt z.txt work docs ");
    int removed = 0;
    check("delete /docs removes whole subtree (4 nodes)", fs.remove("/docs", e, &removed) == 0 && removed == 4);
    check("BST index emptied after delete", fs.bst.keys() == 0);

    // ---------- boundary cases
    FileSystem g;
    check("empty FS stats are all zero", g.stats(g.root).files == 0 && g.stats(g.root).depth == 0);
    check("resolve('') and resolve('/') return root", g.resolve("", e) == g.root && g.resolve("/", e) == g.root);
    check("empty FS traversal has only root", g.preorder(g.root).size() == 1);
    check("100-char name accepted", g.create("/", std::string(100, 'x'), false, 0, e) == 0);
    check("0-byte file accepted", g.create("/", "empty", false, 0, e) == 0);
    std::string deep;
    bool deepOk = true;
    for (int i = 0; i < 200; ++i) { deepOk &= g.create(deep.empty() ? "/" : deep, "d" + std::to_string(i), true, 0, e) == 0; deep += "/d" + std::to_string(i); }
    check("200-level deep hierarchy built", deepOk && g.stats(g.root).depth == 200);
    check("same name in two folders shares one BST key", g.create("/", "A", true, 0, e) == 0 && g.create("/", "B", true, 0, e) == 0 &&
          g.create("/A", "same", false, 1, e) == 0 && g.create("/B", "same", false, 1, e) == 0 &&
          [&] { long long c = 0; return g.searchExact("same", c).size() == 2; }());
    check("deleting one 'same' keeps the other indexed", [&] { int r; g.remove("/A/same", e, &r); long long c = 0; return g.searchExact("same", c).size() == 1; }());

    // ---------- failure cases
    check("duplicate name rejected (409)", g.create("/", "A", true, 0, e) == 409);
    check("empty name rejected (400)", g.create("/", "", true, 0, e) == 400);
    check("name with '/' rejected", g.create("/", "a/b", true, 0, e) == 400);
    check("name '..' rejected", g.create("/", "..", true, 0, e) == 400);
    check("101-char name rejected", g.create("/", std::string(101, 'y'), true, 0, e) == 400);
    check("negative size rejected", g.create("/", "neg", false, -1, e) == 400);
    check("missing parent -> 404", g.create("/nope", "x", true, 0, e) == 404);
    check("create inside a file rejected", g.create("/empty", "x", true, 0, e) == 400);
    check("path through a file fails", g.resolve("/empty/x", e) == nullptr);
    check("delete missing path -> 404", g.remove("/ghost", e, &removed) == 404);
    check("delete root rejected", g.remove("/", e, &removed) == 400);
    check("rename root rejected", g.rename("/", "x", e) == 400);
    check("rename to existing sibling rejected", g.rename("/A", "B", e) == 409);
    check("rename to same name is a no-op", g.rename("/A", "A", e) == 0);
    check("move folder into its own subfolder rejected", g.move("/d0", "/d0/d1/d2", e) == 400);
    check("move onto a file rejected", g.move("/A", "/empty", e) == 400);
    check("move with name clash rejected", g.create("/B", "A", true, 0, e) == 0 && g.move("/A", "/B", e) == 409);
    check("search for absent name returns nothing", [&] { long long c = 0; return g.searchExact("nothing", c).empty(); }());

    // ---------- Second BST (Size Index & Range Pruning)
    FileSystem sfs;
    check("size BST starts empty", sfs.sizeBst.keys() == 0 && sfs.sizeBst.totalFiles() == 0);
    sfs.create("/", "small.txt", false, 100, e);
    sfs.create("/", "med1.bin", false, 500, e);
    sfs.create("/", "med2.bin", false, 500, e); // Duplicate size: linked list collision
    sfs.create("/", "big.iso", false, 2000, e);
    sfs.create("/", "huge.dat", false, 10000, e);

    check("size BST has 4 distinct keys (100, 500, 2000, 10000)", sfs.sizeBst.keys() == 4);
    check("size BST has 5 total files indexed", sfs.sizeBst.totalFiles() == 5);

    // Exact size search
    long long scmp = 0;
    auto exact500 = sfs.searchExactSize(500, scmp);
    check("size BST exact search for 500 finds 2 duplicate-size files via linked list", exact500.size() == 2);

    // Range search: [200, 3000] should find med1 (500), med2 (500), and big (2000)
    scmp = 0;
    auto rangeRes = sfs.searchSizeRange(200, 3000, scmp);
    check("range search [200, 3000] returns 3 files", rangeRes.size() == 3);
    check("range search pruned subtrees (comparisons < total keys)", scmp <= 4);

    // Range search out of bounds
    scmp = 0;
    auto emptyRange = sfs.searchSizeRange(50000, 99999, scmp);
    check("range search outside bounds returns 0 matches", emptyRange.empty());

    // Update size (resize)
    Node* smNode = sfs.resolve("/small.txt", e);
    sfs.updateFileSize(smNode, 1500);
    scmp = 0;
    check("old size 100 has 0 files after update", sfs.searchExactSize(100, scmp).empty());
    scmp = 0;
    check("new size 1500 has small.txt", sfs.searchExactSize(1500, scmp).size() == 1 && smNode->size == 1500);

    // Remove one duplicate file: other duplicate remains in linked list
    int rcnt = 0;
    sfs.remove("/med1.bin", e, &rcnt);
    scmp = 0;
    auto remaining500 = sfs.searchExactSize(500, scmp);
    check("removing one duplicate keeps remaining file at size 500", remaining500.size() == 1 && remaining500[0]->name == "med2.bin");

    // Remove last file of size 500: key is removed from BST
    sfs.remove("/med2.bin", e, &rcnt);
    scmp = 0;
    check("removing last duplicate completely deletes key 500 from size BST", sfs.searchExactSize(500, scmp).empty());

    // ---------- skewed BST sanity (sorted inserts degrade to a list)
    NameBST degBst;
    Node dummyNode("x", false, 0, nullptr);
    for (int i = 0; i < 500; ++i) {
        std::string num = std::to_string(i);
        degBst.insert("item_" + std::string(4 - std::min<size_t>(4, num.size()), '0') + num, &dummyNode);
    }
    check("sorted input makes BST height == n (degenerate)", degBst.height() == 500);

    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}

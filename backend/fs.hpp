// Filesystem operations and benchmarks, composed from the small data structure modules.
#pragma once
#include "bst.h"
#include "fs_types.hpp"
#include "linkedlist.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <queue>
#include <random>
#include <string>
#include <vector>

// ---------------------------------------------------------------- File system
class FileSystem {
public:
  Node *root;
  NameBST bst;
  SizeBST sizeBst;

  FileSystem() : root(new Node("", true, 0, nullptr)) {}
  ~FileSystem() { destroy(root); }

  FileSystem(const FileSystem &) = delete;
  FileSystem &operator=(const FileSystem &) = delete;

  // ---- helpers
  static bool validName(const std::string &s, std::string &err) {
    if (s.empty()) {
      err = "Name cannot be empty";
      return false;
    }
    if (s.size() > 100) {
      err = "Name is too long (max 100 characters)";
      return false;
    }
    if (s == "." || s == "..") {
      err = "Name cannot be '.' or '..'";
      return false;
    }
    if (s.front() == ' ' || s.back() == ' ') {
      err = "Name cannot start or end with a space";
      return false;
    }
    for (unsigned char ch : s)
      if (ch == '/' || ch == '\\' || ch < 32) {
        err = "Name cannot contain / \\ or control characters";
        return false;
      }
    return true;
  }

  static std::vector<std::string> split(const std::string &path) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : path) {
      if (ch == '/') {
        if (!cur.empty())
          out.push_back(cur);
        cur.clear();
      } else
        cur += ch;
    }
    if (!cur.empty())
      out.push_back(cur);
    return out;
  }

  static std::string pathOf(const Node *n) {
    if (!n->parent)
      return "/";
    std::vector<const Node *> up;
    for (; n->parent; n = n->parent)
      up.push_back(n);
    std::string p;
    for (auto it = up.rbegin(); it != up.rend(); ++it)
      p += "/" + (*it)->name;
    return p;
  }

  static Node *findChild(const Node *dir, const std::string &name,
                         long long *steps = nullptr) {
    return LinkedList::find(const_cast<Node *>(dir), name, steps);
  }

  // ---- path-based search: walks root -> ... -> target
  Node *resolve(const std::string &path, std::string &err,
                long long *steps = nullptr) const {
    Node *current = root;
    std::vector<std::string> parts = split(path);

    for (const std::string &name : parts) {
      if (!current->isDir) {
        err = "This is a file, not a folder";
        return nullptr;
      }

      Node *child = findChild(current, name, steps);

      if (child == nullptr) {
        err = "Path not found";
        return nullptr;
      }

      current = child;
    }

    return current;
  }

  // ---- create. returns 0 on success, else an HTTP-like status code
  int create(const std::string &parentPath, const std::string &name, bool isDir,
             long long size, std::string &err, Node **out = nullptr) {
    if (!validName(name, err)) {
      return 400;
    }

    if (!isDir && (size < 0 || size > 1000000000000LL)) {
      err = "File size must be between 0 and 10^12 bytes";
      return 400;
    }

    Node *parent = resolve(parentPath, err);
    if (parent == nullptr)
      return 404;

    if (!parent->isDir) {
      err = "Cannot create inside a file";
      return 400;
    }

    if (findChild(parent, name)) {
      err = "'" + name + "' already exists in " + pathOf(parent);
      return 409;
    }

    long long nodeSize;
    if (isDir == true) {
      nodeSize = 0;
    } else {
      nodeSize = size;
    }

    Node *n = new Node(name, isDir, nodeSize, nullptr);

    link(parent, n);
    bst.insert(name, n);
    if (!isDir)
      sizeBst.insert(size, n);

    if (out != nullptr) {
      *out = n;
    }

    return 0;
  }

  // Imported disk names can legally contain leading or trailing spaces,
  // unlike names created through the demo UI. Keep path delimiters and
  // control characters out of the in-memory index.
  int importEntry(const std::string &parentPath, const std::string &name,
                  bool isDir, long long size, std::string &err) {
    if (name.empty() || name.size() > 255 || name == "." || name == ".." ||
        name.find('/') != std::string::npos) {
      err = "Unsupported filesystem entry name";
      return 400;
    }
    for (unsigned char ch : name)
      if (ch < 32 || ch == 127) {
        err = "Unsupported control character in filesystem name";
        return 400;
      }
    if (!isDir && size < 0) {
      err = "Invalid file size";
      return 400;
    }
    Node *parent = resolve(parentPath, err);
    if (!parent)
      return 404;
    if (!parent->isDir) {
      err = "Cannot import inside a file";
      return 400;
    }
    if (findChild(parent, name)) {
      err = "Duplicate filesystem entry";
      return 409;
    }
    Node *n = new Node(name, isDir, isDir ? 0 : size, nullptr);
    link(parent, n);
    bst.insert(name, n);
    if (!isDir)
      sizeBst.insert(size, n);
    return 0;
  }

  // ---- delete (whole subtree). returns number of nodes removed or -status
  int remove(const std::string &path, std::string &err, int *removed) {
    Node *n = resolve(path, err);
    if (!n)
      return 404;
    if (n == root) {
      err = "The root directory cannot be deleted";
      return 400;
    }
    unlink(n);
    *removed = purge(n);
    return 0;
  }

  int rename(const std::string &path, const std::string &newName,
             std::string &err) {
    if (!validName(newName, err))
      return 400;
    Node *n = resolve(path, err);
    if (!n)
      return 404;
    if (n == root) {
      err = "The root directory cannot be renamed";
      return 400;
    }
    if (n->name == newName)
      return 0;

    if (findChild(n->parent, newName)) {
      err = "'" + newName + "' already exists in " + pathOf(n->parent);
      return 409;
    }
    Node *p = n->parent;

    unlink(n);
    bst.remove(n->name, n);
    n->name = newName;

    link(p, n); // re-insert at the correct sorted position
    bst.insert(newName, n);
    return 0;
  }

  int move(const std::string &path, const std::string &destPath,
           std::string &err) {
    Node *n = resolve(path, err);

    if (!n)
      return 404;
    Node *d = resolve(destPath, err);
    if (!d)
      return 404;
    if (n == root) {
      err = "The root directory cannot be moved";
      return 400;
    }
    if (!d->isDir) {
      err = "Destination must be a directory";
      return 400;
    }

    Node *a = d;

    while (a != nullptr) {
      if (a == n) {
        err = "Cannot move a folder into itself or its own subfolder";
        return 400;
      }
      a = a->parent;
    }

    if (d == n->parent)
      return 0;

    if (findChild(d, n->name)) {
      err = "'" + n->name + "' already exists in " + pathOf(d);
      return 409;
    }
    unlink(n);
    link(d, n);
    return 0;
  }

  int sortDirectory(const std::string &path, std::string &err) {
    Node *n = resolve(path, err);
    if (!n)
      return 404;
    if (!n->isDir) {
      err = "Not a directory";
      return 400;
    }
    LinkedList::sort(n);
    return 0;
  }

  // ---- recursive subtree statistics (descendants only)
  Stats stats(const Node *n) const {
    Stats s;
    for (const Node *c = n->firstChild; c; c = c->next) {
      if (c->isDir) {
        Stats cs = stats(c);
        s.files += cs.files;
        s.dirs += cs.dirs + 1;
        s.size += cs.size;
        s.depth = std::max(s.depth, cs.depth + 1);
      } else {
        s.files += 1;
        s.size += c->size;
        s.depth = std::max(s.depth, 1);
      }
    }
    return s;
  }

  // ---- traversals
  std::vector<TItem> preorder(const Node *s) const {
    std::vector<TItem> o;
    pre(s, 0, o);
    return o;
  }

  std::vector<TItem> postorder(const Node *s) const {
    std::vector<TItem> o;
    post(s, 0, o);
    return o;
  }

  std::vector<TItem> levelorder(const Node *s) const {
    std::vector<TItem> o;
    std::queue<TItem> q;
    q.push({s, 0});
    while (!q.empty()) {
      TItem it = q.front();
      q.pop();
      o.push_back(it);
      for (const Node *c = it.n->firstChild; c; c = c->next)
        q.push({c, it.depth + 1});
    }
    return o;
  }

  // lexicographic: BST in-order over every name beneath `s`
  std::vector<TItem> lexicographic(const Node *s) const {
    std::vector<TItem> o;
    bst.inorder([&](const BSTNode *b) {
      for (const Node *n : b->items)
        if (isUnder(n, s))
          o.push_back({n, depthBelow(n, s)});
    });
    return o;
  }

  // ---- name search through the BST. comparisons = BST nodes examined
  std::vector<const Node *> searchExact(const std::string &q,
                                        long long &cmp) const {
    std::vector<const Node *> out;
    const BSTNode *b = bst.find(q, &cmp);
    if (b)
      out.assign(b->items.begin(), b->items.end());
    return out;
  }

  // ---- name search by DFS over the general tree (O(n) scan)
  std::vector<const Node *> searchTreeExact(const std::string &q,
                                            long long &cmp) const {
    std::vector<const Node *> out;
    std::function<void(const Node *)> dfs = [&](const Node *n) {
      for (const Node *c = n->firstChild; c; c = c->next) {
        ++cmp;
        if (c->name == q)
          out.push_back(c);
        if (c->isDir)
          dfs(c);
      }
    };
    dfs(root);
    return out;
  }

  std::vector<const Node *> searchPrefix(const std::string &q,
                                         long long &cmp) const {
    auto v = bst.prefix(q, cmp);
    return std::vector<const Node *>(v.begin(), v.end());
  }

  std::vector<const Node *> searchContains(const std::string &q,
                                           long long &cmp) const {
    std::vector<const Node *> out;
    auto low = [](std::string s) {
      for (auto &c : s)
        c = (char)tolower((unsigned char)c);
      return s;
    };
    std::string lq = low(q);
    bst.inorder([&](const BSTNode *b) {
      ++cmp;
      if (low(b->key).find(lq) != std::string::npos)
        out.insert(out.end(), b->items.begin(), b->items.end());
    });
    return out;
  }

  // ---- size search through the second BST (with range pruning)
  std::vector<const Node *> searchSizeRange(long long minSize, long long maxSize,
                                            long long &cmp) const {
    auto v = sizeBst.range(minSize, maxSize, cmp);
    return std::vector<const Node *>(v.begin(), v.end());
  }

  std::vector<const Node *> searchExactSize(long long size,
                                            long long &cmp) const {
    auto v = sizeBst.exact(size, cmp);
    return std::vector<const Node *>(v.begin(), v.end());
  }

  void updateFileSize(Node *file, long long newSize) {
    if (!file || file->isDir)
      return;
    sizeBst.updateSize(file, file->size, newSize);
    file->size = newSize;
  }

  static bool isUnder(const Node *n, const Node *anc) {
    for (const Node *a = n->parent; a; a = a->parent)
      if (a == anc)
        return true;
    return false;
  }

  static int depthBelow(const Node *n, const Node *anc) {
    int d = 0;
    for (const Node *a = n; a && a != anc; a = a->parent)
      ++d;
    return d;
  }

private:
  // insert into the parent's sorted singly linked list
  static void link(Node *dir, Node *n) {
    LinkedList::insert(dir, n);
  }

  static void unlink(Node *n) {
    LinkedList::remove(n);
  }

  int purge(Node *n) {
    int cnt = 1;
    for (Node *c = n->firstChild; c;) {
      Node *nx = c->next;
      cnt += purge(c);
      c = nx;
    }
    bst.remove(n->name, n);
    if (!n->isDir)
      sizeBst.remove(n->size, n);
    delete n;
    return cnt;
  }

  static void destroy(Node *n) {
    for (Node *c = n->firstChild; c;) {
      Node *nx = c->next;
      destroy(c);
      c = nx;
    }
    delete n;
  }

  void pre(const Node *n, int d, std::vector<TItem> &o) const {
    o.push_back({n, d});
    for (const Node *c = n->firstChild; c; c = c->next)
      pre(c, d + 1, o);
  }

  void post(const Node *n, int d, std::vector<TItem> &o) const {
    for (const Node *c = n->firstChild; c; c = c->next)
      post(c, d + 1, o);
    o.push_back({n, d});
  }
};

struct BenchRow {
  size_t n = 0;
  double listInsert = 0, bstInsert = 0, listSearch = 0, bstSearch = 0,
         listDelete = 0, bstDelete = 0;
  int bstHeight = 0;
};

inline std::vector<BenchRow> runBenchmark(const std::vector<size_t> &sizes,
                                          bool sortedInput) {
  using clk = std::chrono::steady_clock;
  auto ms = [](clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  std::vector<BenchRow> rows;
  for (size_t n : sizes) {
    std::vector<std::string> names;
    names.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      std::string num = std::to_string(i);
      names.push_back("file_" +
                      std::string(8 - std::min<size_t>(8, num.size()), '0') +
                      num);
    }
    std::mt19937 rng(12345);
    if (!sortedInput)
      std::shuffle(names.begin(), names.end(), rng);
    std::vector<std::string> probe;
    size_t q = std::min<size_t>(n, 1000);
    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; ++i)
      idx[i] = i;
    std::shuffle(idx.begin(), idx.end(), rng);
    for (size_t i = 0; i < q; ++i)
      probe.push_back(names[idx[i]]);

    BenchRow r;
    r.n = n;

    SortedStringList list;
    auto t0 = clk::now();
    for (auto &s : names)
      list.insert(s);
    auto t1 = clk::now();
    r.listInsert = ms(t0, t1);
    volatile size_t hits = 0;
    t0 = clk::now();
    for (auto &s : probe)
      if (list.contains(s))
        ++hits;
    t1 = clk::now();
    r.listSearch = ms(t0, t1);
    t0 = clk::now();
    for (auto &s : probe)
      list.remove(s);
    t1 = clk::now();
    r.listDelete = ms(t0, t1);

    NameBST b;
    Node dummy("x", false, 0, nullptr);
    t0 = clk::now();
    for (auto &s : names)
      b.insert(s, &dummy);
    t1 = clk::now();
    r.bstInsert = ms(t0, t1);
    r.bstHeight = b.height();
    t0 = clk::now();
    for (auto &s : probe)
      if (b.find(s))
        ++hits;
    t1 = clk::now();
    r.bstSearch = ms(t0, t1);
    t0 = clk::now();
    for (auto &s : probe)
      b.remove(s, &dummy);
    t1 = clk::now();
    r.bstDelete = ms(t0, t1);
    rows.push_back(r);
  }
  return rows;
}

struct TreeBenchRow {
  size_t n = 0;
  double treeMs = 0, bstMs = 0;
  double treeAvgCmp = 0, bstAvgCmp = 0;
  int bstHeight = 0;
  size_t queries = 0;
};

inline std::vector<TreeBenchRow>
runTreeVsBstBenchmark(const std::vector<size_t> &sizes, bool sortedInput) {
  using clk = std::chrono::steady_clock;
  auto ms = [](clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  std::vector<TreeBenchRow> rows;
  for (size_t n : sizes) {
    std::vector<std::string> names;
    names.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      std::string num = std::to_string(i);
      names.push_back("file_" +
                      std::string(8 - std::min<size_t>(8, num.size()), '0') +
                      num);
    }
    std::mt19937 rng(12345);
    if (!sortedInput)
      std::shuffle(names.begin(), names.end(), rng);

    // Build a multi-folder general tree with ~sqrt(n) directories
    Node *treeRoot = new Node("", true, 0, nullptr);
    size_t dirCount = std::max<size_t>(1, (size_t)std::sqrt((double)n));
    std::vector<Node *> dirs;
    dirs.reserve(dirCount);
    Node *lastDir = nullptr;
    for (size_t d = 0; d < dirCount; ++d) {
      Node *dir = new Node("dir_" + std::to_string(d), true, 0, treeRoot);
      if (!lastDir)
        treeRoot->firstChild = dir;
      else
        lastDir->next = dir;
      lastDir = dir;
      dirs.push_back(dir);
    }
    std::vector<Node *> dirTails(dirCount, nullptr);
    for (size_t i = 0; i < n; ++i) {
      size_t d = i % dirCount;
      Node *f = new Node(names[i], false, 128, dirs[d]);
      if (!dirTails[d])
        dirs[d]->firstChild = f;
      else
        dirTails[d]->next = f;
      dirTails[d] = f;
    }

    // Build NameBST with the same n names
    NameBST b;
    Node dummy("x", false, 0, nullptr);
    for (const auto &s : names)
      b.insert(s, &dummy);

    size_t q = std::min<size_t>(n, 300);
    std::vector<std::string> probe;
    probe.reserve(q);
    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; ++i)
      idx[i] = i;
    std::shuffle(idx.begin(), idx.end(), rng);
    for (size_t i = 0; i < q; ++i)
      probe.push_back(names[idx[i]]);

    TreeBenchRow r;
    r.n = n;
    r.queries = q;
    r.bstHeight = b.height();

    // Benchmark General Tree DFS search
    long long totalTreeCmp = 0;
    volatile size_t hits = 0;
    auto treeFind = [&](const std::string &target, long long &cmp) -> bool {
      std::function<bool(const Node *)> dfs = [&](const Node *node) -> bool {
        for (const Node *c = node->firstChild; c; c = c->next) {
          ++cmp;
          if (c->name == target)
            return true;
          if (c->isDir && dfs(c))
            return true;
        }
        return false;
      };
      return dfs(treeRoot);
    };
    auto t0 = clk::now();
    for (const auto &s : probe) {
      if (treeFind(s, totalTreeCmp))
        ++hits;
    }
    auto t1 = clk::now();
    r.treeMs = ms(t0, t1);
    r.treeAvgCmp = q ? (double)totalTreeCmp / (double)q : 0.0;

    // Benchmark NameBST search
    long long totalBstCmp = 0;
    t0 = clk::now();
    for (const auto &s : probe) {
      long long c = 0;
      if (b.find(s, &c))
        ++hits;
      totalBstCmp += c;
    }
    t1 = clk::now();
    r.bstMs = ms(t0, t1);
    r.bstAvgCmp = q ? (double)totalBstCmp / (double)q : 0.0;

    // Clean up general tree
    std::function<void(Node *)> destroyTree = [&](Node *node) {
      for (Node *c = node->firstChild; c;) {
        Node *nx = c->next;
        destroyTree(c);
        c = nx;
      }
      delete node;
    };
    destroyTree(treeRoot);
    rows.push_back(r);
  }
  return rows;
}

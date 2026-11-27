# Intelligent File System

Web file manager (HTML/CSS/JS) backed by a dependency-free C++ server.

## Data structures
| Structure | Role |
|---|---|
| General tree | Directory hierarchy (directories have children, files are leaves) |
| Sorted singly linked list | Each directory's children, always in lexicographic order |
| BST 1 (Name index) | Global name index: exact / prefix / contains search and in-order alphabetical traversal |
| BST 2 (Size index) | Global file size index: exact and range queries `[min, max]` with branch pruning; duplicate sizes chained in a singly linked list at each node |

### Backend code map
The C++ logic is split into small headers in `backend/`:

- `fs_types.hpp` defines the shared file and directory nodes.
- `linkedlist.h` declares the linked-list operations; `linkedlist.cpp` implements them.
- `bst.h` declares `NameBST` and `SizeBST` (with `FileListNode`); `bst.cpp` implements exact, prefix, in-order, and range pruning operations.
- `fs.hpp` connects those structures to filesystem operations and benchmarks.
- `server.cpp` exposes the filesystem logic to the website over HTTP.

## Run
**Linux / macOS**
    cd backend
    g++ -O2 -std=c++17 -o ifs server.cpp bst.cpp linkedlist.cpp
    ./ifs 8080 ../frontend
**Windows (MinGW)**
    cd backend
    g++ -O2 -std=c++17 -o ifs.exe server.cpp bst.cpp linkedlist.cpp -lws2_32
    ifs.exe 8080 ../frontend
Open http://localhost:8080

### Work with a real folder
Pass a folder path as the third argument. The app checks the folder every 1.5 seconds while the page is open and redraws the explorer only when its visible file listing changes. Website create, edit, rename, move, and delete actions are written to that folder by the C++ server. Changes made with Finder, File Explorer, or a terminal also appear in the website. Text editing and new-file contents are limited to 1 MB. Symlinks are skipped, and writes stay inside the selected folder. The scan is capped at 20,000 entries and 64 levels deep.

**macOS / Linux**
    ./ifs 8080 ../frontend "/path/to/folder"

**Windows (MinGW)**
    ifs.exe 8080 ../frontend "C:\\path\\to\\folder"

Restart without the folder argument to return to the editable in-memory demo dataset.

## Tests (57 normal / boundary / failure / second BST cases)
    g++ -std=c++17 -o tests tests.cpp bst.cpp linkedlist.cpp && ./tests

## API
GET  /api/tree · /api/list?path= · /api/resolve?path= · /api/stats?path=
GET  /api/search?q=&mode=exact|prefix|contains
GET  /api/search?mode=size_range&min=&max= · /api/search?mode=size_exact&size=
GET  /api/traverse?order=preorder|postorder|levelorder|lexicographic&path=
GET  /api/read?path=
POST /api/create?parent=&name=&type=file|dir&size= · /api/delete?path= · /api/rename?path=&newName= · /api/move?path=&dest= · /api/write?path= (content in form body) · /api/reset

## Complexity (n = nodes, k = children in one directory, d = depth, h = BST height, r = range matches)
| Operation | Time |
|---|---|
| Insert child | O(k) list position + O(h) BST |
| Path search | O(d·k) |
| Exact name search (BST 1) | O(h) average O(log n), worst O(n) |
| Prefix search (BST 1) | O(h + matches) |
| Size range search (BST 2) | O(h + r) with branch pruning |
| Exact size search (BST 2) | O(h + duplicate files in linked list) |
| Delete subtree of m nodes | O(m·h) |
| Rename | O(k + h) |
| Subtree stats / any traversal | O(subtree size) |
Space: O(n). The BST is unbalanced, so sorted input degrades it to O(n).

# MOC — Middle-Out Compressor

### A compression algorithm that starts in the middle, takes the long way around, and somehow gets the job done.

> **MOC V10.0 — A bounded-memory LZ-style compression prototype written in C.**

MOC (Middle-Out Compressor) is a custom **lossless compression project written from scratch in C**.

It started as an experiment around **middle-out data traversal and pattern detection**.

It eventually became something considerably more useful:

**a working, memory-conscious LZ-style compression engine with its own binary file format, hash-based match indexing, corruption detection, overlapping references, and a decoder that can reconstruct the original data exactly.**

And yes, it has been tested on **500 MB** of data.

Because apparently testing it on 1 MB wasn't dramatic enough.

---

## Why does this exist?

Most compression algorithms are built around a familiar idea:

> Find repeated data → replace it with a reference → save space.

MOC follows the same fundamental principle, but implements its own lightweight version of the machinery behind LZ-style compression.

The interesting part isn't just getting compression to work.

The interesting part is making it:

* **lossless**
* **memory-conscious**
* **deterministic**
* **corruption-aware**
* **portable**
* **simple enough to understand**
* and actually capable of handling large inputs

The project evolved through multiple versions, with each version solving a different engineering problem.

The final result is **MOC V10.0**.

---

# Features

### Core compression

* Lossless LZ-style compression
* Repeated-pattern detection
* Literal blocks
* Back-reference tokens
* Overlapping matches
* Variable-length integer encoding
* Cost-aware reference selection
* Custom `.moc` file format
* Command-line interface

### Memory-conscious design

MOC originally experimented with data structures whose memory consumption grew with the input.

That was useful for learning.

It was not particularly useful for compressing large files.

So the final implementation uses a **bounded hash index**.

Instead of storing an ever-growing list of positions, each hash bucket keeps only a limited number of recent candidates.

Current configuration:

```text
HASH_SIZE       = 65,536 buckets
CHAIN_LIMIT     = 8 positions per bucket
MAX_CANDIDATES  = 64
MIN_MATCH       = 3 bytes
MAX_MATCH       = 256 bytes
```

The hash index therefore has a fixed upper memory requirement rather than growing linearly with the input size.

In other words:

> The file can get bigger.
> The index doesn't get emotionally attached to it.

---

# How MOC works

At a high level:

```text
                    INPUT DATA
                        │
                        ▼
               ┌─────────────────┐
               │  Scan position  │
               └────────┬────────┘
                        │
                        ▼
               ┌─────────────────┐
               │  Hash next 3    │
               │     bytes       │
               └────────┬────────┘
                        │
                        ▼
               ┌─────────────────┐
               │ Search recent   │
               │ hash candidates │
               └────────┬────────┘
                        │
                 ┌──────┴──────┐
                 │             │
              Match?          No
                 │             │
                Yes            ▼
                 │       Store literal
                 ▼             │
          Calculate match      │
               cost            │
                 │             │
                 ▼             │
          ┌──────────────┐     │
          │ Profitable?  │─────┘
          └──────┬───────┘
                 │
                Yes
                 │
                 ▼
          Store reference
                 │
                 ▼
           Advance position
                 │
                 ▼
                DONE
```

---

# The basic compression idea

Suppose the input contains:

```text
ABCABCABCABCABC
```

Instead of storing every repeated `ABC`, the compressor can store the first occurrence as literals and later occurrences as references.

Conceptually:

```text
LITERAL:    ABC
REFERENCE:  offset=3, length=12
```

The decoder then uses the reference to reconstruct the repeated data.

This is the fundamental idea behind the LZ-style portion of MOC.

---

# Why the hash index matters

A naive compressor could search backwards through the entire input every time it wants to find a match.

That works.

It also has a strong philosophical relationship with making your computer suffer.

MOC instead hashes every **3-byte sequence**:

```text
ABC → hash
BCD → hash
CDE → hash
...
```

The hash points to a small chain of recently seen positions.

When the compressor reaches a new position, it:

1. calculates the 3-byte hash
2. finds the corresponding bucket
3. checks recent candidate positions
4. calculates possible match lengths
5. evaluates the cost
6. chooses the best profitable reference

This reduces unnecessary searching while keeping the match index bounded.

---

# Cost-aware matching

Finding a long match isn't automatically useful.

A reference itself consumes space.

MOC therefore estimates the cost of storing a reference:

```text
reference cost =
    1 byte
    + encoded offset
    + encoded length
```

The compressor then calculates:

```text
score = match_length - reference_cost
```

A reference is emitted only when it is profitable.

So MOC doesn't blindly say:

> "LOOK! THREE REPEATED BYTES!"

It asks:

> "Yes, but is this financially responsible?"

If a reference isn't worthwhile, the data is stored as a literal block instead.

---

# Variable-length integers

Offsets and lengths are stored using **variable-length integer encoding**.

Small values require fewer bytes, while larger values use additional bytes.

Conceptually:

```text
small number  →  small encoding
large number  →  larger encoding
```

This keeps references compact without forcing every offset and length to consume a fixed number of bytes.

---

# The MOC file format

MOC uses a simple binary format.

A compressed file begins with:

```text
M O C 5
```

followed by the original input size.

The stream then contains tokens representing either:

```text
LITERAL
```

or:

```text
REFERENCE
```

The decoder reads these tokens and reconstructs the original byte stream.

The format was deliberately kept relatively simple so that the decoder remains understandable and the project can evolve without turning into a binary-format archaeology expedition.

---

# Decoder

The decoder performs the inverse operation.

```text
              .moc FILE
                  │
                  ▼
           Read file header
                  │
                  ▼
          Validate MOC format
                  │
                  ▼
            Read next token
                  │
          ┌───────┴────────┐
          │                │
       Literal          Reference
          │                │
          ▼                ▼
    Copy literal       Read offset
        bytes          Read length
          │                │
          │                ▼
          │         Copy referenced
          │             bytes
          │                │
          └───────┬────────┘
                  ▼
             Continue
                  │
                  ▼
             Reconstructed
                output
```

The decoder validates the compressed stream and rejects malformed or corrupted input rather than blindly producing questionable output.

Because silently producing the wrong file is significantly worse than saying:

> "Nope. This file is broken."

---

# Overlapping references

MOC supports **overlapping LZ-style matches**.

This matters for highly repetitive data.

A reference can point to data that overlaps the region currently being reconstructed, allowing patterns to effectively repeat during decoding.

The compressor and decoder therefore use compatible overlapping-copy semantics.

This allows compact representations of strongly repetitive data.

---

# Performance

MOC V10.0 has been tested against repetitive workloads.

## 1 MB repetitive input

```text
Input size : 1,048,576 bytes
Output size: 16,472 bytes

Compression ratio: 63.66x
```

The compressed stream successfully decompresses back to:

```text
1,048,576 bytes
```

and a byte-for-byte comparison using `cmp` reports no differences.

---

## 500 MB stress test

The final implementation was also tested with a **500 MB repetitive input**.

The complete:

```text
compression
     ↓
decompression
     ↓
byte-for-byte comparison
```

pipeline succeeded.

Conceptually:

```text
500 MB original
      │
      ▼
   MOC V10
      │
      ▼
 compressed data
      │
      ▼
    decoder
      │
      ▼
500 MB restored
      │
      ▼
     cmp
      │
      ▼
   IDENTICAL
```

No output from `cmp` means the files are byte-for-byte identical.

---

# Memory safety

Compression was tested using **Valgrind**.

The result:

```text
All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors
```

Decompression was also tested:

```text
All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors
```

The tested compression and decompression paths therefore showed:

```text
Definitely lost : 0 bytes
Indirectly lost : 0 bytes
Possibly lost   : 0 bytes
Errors          : 0
```

Memory management was treated as part of the algorithm rather than something to check only at the end.

---

# Corruption testing

A compressor is only useful if its decoder doesn't cheerfully accept nonsense.

MOC was tested with deliberately corrupted compressed streams.

Invalid data is rejected with:

```text
Error: invalid or corrupted MOC file.
```

This behavior is intentional.

A decompressor should not confidently manufacture a wrong output file just because someone gave it a damaged input.

---

# Project evolution

This project did not magically appear as V10.

It evolved.

## V3 — The beginning

The first working compression prototype.

The question was:

```text
Can I build this?
```

Answer:

**Yes.**

---

## V6 — Faster matching

V6 introduced a **3-byte hash index** instead of relying on brute-force match searching.

The question changed from:

```text
Can it compress?
```

to:

```text
Can it compress without taking forever?
```

---

## V7 — Better memory behavior

V7 replaced the earlier heap-allocated linked-list style match index with an **array-based hash chain**.

This reduced allocation overhead and made memory behavior more predictable.

---

## V7.3 / V7.4 — Bounded indexing

The project increasingly focused on memory behavior.

The match index was redesigned so that it no longer needed to grow with the entire input.

This was a major shift in the project:

> Memory usage became a design constraint, not an afterthought.

---

## V8 / V9 — Robustness

The focus shifted toward:

* memory safety
* large-input testing
* edge cases
* error handling
* corruption handling
* reliable decompression
* stress testing

---

## V10 — Final prototype

The objective became:

> Stop adding toys. Start proving the thing works.

V10 focuses on:

* stable compression
* stable decompression
* bounded match indexing
* corruption detection
* memory safety
* large-file testing
* clean command-line operation

---

# Repository note

You may notice a few extra files in the repository that look like they were invited to the party without checking the guest list.

Some files are:

* testing artifacts
* sample inputs
* intermediate experiments
* temporary development files
* files uploaded during development by mistake

They are **not part of the core MOC V10.0 implementation** and can safely be ignored when evaluating the project.

The most important files for understanding the final implementation are:

```text
src/main.c
src/moc.c
include/moc.h
Makefile
README.md
```

The `benchmarks/` directory contains test data used to validate compression, decompression, performance, and large-input behavior.

In short:

> **If a random `.txt`, backup, or experimental file looks suspicious, it probably is.**
>
> The algorithm isn't hiding there. We promise.

---

# Build

Requirements:

* GCC
* Make
* A standard C environment

Build:

```bash
make
```

Clean:

```bash
make clean
```

The project is compiled with:

```text
-Wall
-Wextra
-std=c11
```

---

# Usage

## Compress

```bash
./moc moc-compress <input> <output>
```

Example:

```bash
./moc moc-compress benchmarks/data/stress/repetitive_1MB.txt output.moc
```

---

## Decompress

```bash
./moc moc-decompress <input> <output>
```

Example:

```bash
./moc moc-decompress output.moc restored.txt
```

---

## Verify lossless restoration

For lossless compression, verification is straightforward:

```bash
cmp input.txt restored.txt
```

No output means:

```text
THE FILES ARE IDENTICAL.
```

Which is exactly what we want.

---

# Command-line interface

MOC V10.0 supports:

```bash
./moc --help
```

and:

```bash
./moc --version
```

The primary V10 compression interface is:

```text
moc-compress
moc-decompress
```

The project also retains several experimental and educational operations developed during earlier versions:

```text
middle
patterns
match
baseline
compress
decompress
```

These are useful for exploring the ideas that led to the final compressor.

---

# Project structure

```text
MiddleOut/
│
├── include/
│   ├── baseline.h
│   ├── middleout.h
│   ├── moc.h
│   ├── pattern.h
│   ├── reference.h
│   └── rle.h
│
├── src/
│   ├── baseline.c
│   ├── main.c
│   ├── middleout.c
│   ├── moc.c
│   ├── pattern.c
│   ├── reference.c
│   └── rle.c
│
├── benchmarks/
│   └── data/
│
├── Makefile
└── README.md
```

The main architecture is intentionally separated:

```text
                    main.c
                      │
              CLI + file handling
                      │
                      ▼
                    moc.c
                      │
        ┌─────────────┼─────────────┐
        │             │             │
        ▼             ▼             ▼
     Hashing      Match finding   Encoding
        │             │             │
        └─────────────┼─────────────┘
                      │
                      ▼
                 MOC5 stream
                      │
                      ▼
                 moc-decompress
```

---

# Design philosophy

MOC follows a few simple engineering principles.

### 1. Correctness before cleverness

A compression ratio is meaningless if the decompressed file is wrong.

### 2. Memory is a resource

If an algorithm works only because the computer happens to have enough RAM, that does not automatically mean the memory problem has been solved.

### 3. Measure things

Compression ratio, execution time, memory behavior, corruption handling and round-trip correctness should be tested rather than assumed.

### 4. Keep the implementation understandable

This is a systems project, not a competition to see how many macros can be hidden inside one C file.

### 5. Fail safely

Corrupted compressed data should be rejected.

---

# What MOC is NOT

MOC V10.0 is **not intended to replace production compressors such as gzip, zstd, or Brotli**.

Those projects have decades of optimization, mature formats, extensive fuzzing, platform-specific tuning and enormous test suites behind them.

MOC is instead a **real working systems prototype** demonstrating:

* compression algorithms
* LZ-style references
* hashing
* bounded-memory indexing
* binary file formats
* variable-length encoding
* C memory management
* error handling
* performance testing
* systems-level debugging

The important part is that the prototype actually works.

---

# Current limitations

## Maximum input size

The current MOC5 format stores the original input size in a 32-bit unsigned integer.

Therefore the representable original size is approximately:

```text
4 GiB
```

---

## Compression scope

MOC currently focuses on a relatively simple LZ-style representation.

It does not attempt to combine the many sophisticated entropy coders, statistical models and optimizations used by mature production compressors.

---

## Streaming

The current public compression API operates on memory buffers:

```c
moc_compress(...)
moc_decompress(...)
```

The core API is therefore not yet a fully streaming compressor.

That is a future engineering direction rather than something hidden from the user.

---

# Testing philosophy

The project follows a simple rule:

> **If it can't survive `cmp`, Valgrind and ugly input, it isn't finished.**

Testing includes:

* repetitive data
* large inputs
* compression/decompression round trips
* exact byte comparison
* memory leak detection
* corrupted streams
* truncated streams
* command-line validation
* clean rebuilds

---

# Real-world relevance

Although MOC is not designed to compete with mature compressors, the engineering concepts involved are directly relevant to real systems software.

The project demonstrates practical experience with:

### Systems programming

* C
* pointers
* dynamic memory
* binary data
* file I/O
* command-line interfaces

### Algorithms

* hashing
* pattern matching
* greedy selection
* LZ-style back references
* variable-length encoding

### Performance engineering

* bounded data structures
* candidate limiting
* avoiding unnecessary searches
* large-input stress testing

### Reliability

* error handling
* corruption detection
* exact round-trip verification
* memory leak testing

### Software engineering

* incremental versioning
* debugging
* regression testing
* Git
* reproducible builds
* documenting design decisions

---

# Why "Middle-Out"?

Because the project originally explored **middle-out traversal and pattern discovery**.

The name survived.

The implementation evolved.

The algorithm eventually became more conventional LZ-style compression machinery.

The name stayed because changing the name at V10 would be emotionally irresponsible.

---

# Version

```text
MOC V10.0
```

Status:

```text
Stable working prototype
```

The V10.0 implementation is the final version of the current project scope.

---

# Author's note

This project started with a fairly simple question:

> **"Can I make my own compression algorithm?"**

The answer eventually became:

> **"Yes. But then you have to debug memory management, binary formats, integer encoding, hashing, corruption handling, large files, and approximately seventeen different ways to accidentally ruin your weekend."**

MOC is the result of that process.

It was intentionally built from the ground up in C because the goal was not merely to *use* a compression library.

The goal was to understand what happens underneath one.

If you're reading the code, feel free to judge it.

Preferably after checking whether it works.

---

# TL;DR

```text
                 MOC V10.0

        Custom lossless LZ-style compressor
                        │
                        ├── 3-byte hashing
                        │
                        ├── bounded hash chains
                        │
                        ├── match scoring
                        │
                        ├── variable-length references
                        │
                        ├── overlapping matches
                        │
                        ├── custom MOC5 format
                        │
                        ├── corruption detection
                        │
                        ├── Valgrind-clean tested paths
                        │
                        ├── 1 MB → 63.66x
                        │
                        └── 500 MB round-trip verified
                                │
                                ▼
                     A slightly weird,
                     but functioning
                     compression engine.
```

**It began as an algorithm experiment.**

**It ended as a systems project.**

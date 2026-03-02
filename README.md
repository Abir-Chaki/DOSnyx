# DOSnyx

DOSnyx is a 64-bit hobby operating system written in C++ (freestanding) and x86_64 assembly.

It runs in long mode, uses identity paging, and features a working interrupt system with proper exception handling.

---

## 🚀 Features

* 64-bit Long Mode
* Custom Bootloader (NASM)
* GDT Setup
* Identity Paging (2MB)
* Full IDT (0–31 CPU Exceptions)
* Page Fault Handler (with CR2 reporting)
* PIC Remapping
* Timer IRQ (IRQ0)
* Keyboard IRQ (IRQ1)
* Basic In-Memory Filesystem
* Built-in Console
* Notepad-style App (`write`)

---

## 🧠 Exception Handling

DOSnyx includes a working exception system:

* Divide-by-zero detection
* Page fault handling
* Error code decoding
* CR2 register reporting
* Clean kernel panic screen

---

## 🛠 Requirements

You need:

* `x86\_64-elf-gcc` toolchain
* `nasm`
* `ld`
* `qemu-system-x86\_64`
* `make`
---
## Alternatively

You could run:

* `chmod +x build.sh`
* `chmod +x install.sh`
* `./build.sh`
* `./install.sh`

---

## 🔧 Build

### In bash Terminal
make clean
make
make run


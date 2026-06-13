# 🌌 SP Native Addons: Cross-Language Guide

Welcome to the comprehensive guide for building native addons for the **SP Programming Language**! While SP is built in C++, its minimalist ABI and C-style argument passing allow you to use almost any language that supports C linkage.

---

## 🏗️ Core Concepts

To interface with SP, you need to understand two key structures:

### 1. `sp_value` (NaN-Boxing)
SP uses **NaN-boxing** for values. Every SP value is a 64-bit unsigned integer.
- **Numbers**: Standard `double` (64-bit float).
- **Other Types**: Boxed with a specific NaN-pattern and tag.

### 2. `sp_args` (`std::vector` Layout)
Arguments are passed as a reference to a C++ `std::vector<Value>`. In most modern compilers (GCC/Clang), this corresponds to three pointers: `start`, `finish`, and `end_of_storage`.

> [!TIP]
> Use our [**`sp_addon.h`**](./include/sp_addon.h) for the easiest C/C++ development!

---

## ⚡ Language Examples

The following examples demonstrate how to implement a basic `add_numbers` function in various languages. Since SP uses a stable C-style ABI, you can use any language that can export C symbols.

### 📑 Table of Contents
- [⚡ Zig](#-zig)
- [🦀 Rust](#-rust)
- [🐹 Go](#-go)
- [🏺 Odin](#-odin)
- [👑 Nim](#-nim)
- [💎 Crystal](#-crystal)
- [🍎 Swift](#-swift)
- [🐉 D](#-d)
- [👴 FreePascal](#-freepascal)
- [🐍 Python](#-python)
- [♯ C#](#-c)
- [☕ Java](#-java)
- [🟣 Julia](#-julia)
- [📜 JavaScript](#-javascript)
- [🎯 Kotlin](#-kotlin)
- [λ Haskell](#-haskell)
- [🔢 Fortran](#-fortran)
- [⚙️ Assembly (x86_64)](#-assembly-x86_64)
- [💼 COBOL](#-cobol)
- [🐱 LOLCODE](#-lolcode)
- [💀 Brainfuck](#-brainfuck)
- [🔱 Common Lisp](#-common-lisp)
- [🐚 Bash](#-bash)

---

### ⚡ Zig
Zig's memory-safe and low-level features make it an excellent choice for SP addons.

```zig
const std = @import("std");

pub const Value = extern struct { bits: u64 };
pub const StdVector = extern struct {
    start: [*]Value,
    finish: [*]Value,
    end_of_storage: [*]Value,

    pub fn len(self: *const StdVector) usize {
        return (@intFromPtr(self.finish) - @intFromPtr(self.start)) / @sizeOf(Value);
    }
};

export fn add_numbers(_: ?*anyopaque, args: *const StdVector) u64 {
    if (args.len() < 2) return 0;
    const v1 = args.start[0].bits;
    const v2 = args.start[1].bits;
    // Check if both are numbers (not NaN-boxed)
    if ((v1 & 0x7FF0000000000000) != 0x7FF0000000000000 and (v2 & 0x7FF0000000000000) != 0x7FF0000000000000) {
        const d1: f64 = @bitCast(v1);
        const d2: f64 = @bitCast(v2);
        return @bitCast(d1 + d2);
    }
    return 0;
}
```
**Compile:** `zig build-lib -dynamic addon.zig -femit-bin=libmy_addon.so`

---

### 🦀 Rust
Rust is powerful and safe. Use `cdylib` and `#[no_mangle]`.

```rust
#[repr(C)]
pub struct Value { bits: u64 }

#[repr(C)]
pub struct StdVector {
    start: *const Value,
    finish: *const Value,
    end_of_storage: *const Value,
}

#[no_mangle]
pub unsafe extern "C" fn add_numbers(_i: *mut (), args: *const StdVector) -> u64 {
    let args = &*args;
    let len = (args.finish as usize - args.start as usize) / 8;
    if len < 2 { return 0; }
    let v1 = (*args.start).bits;
    let v2 = (*args.start.add(1)).bits;
    if (v1 & 0x7FF0000000000000) != 0x7FF0000000000000 && (v2 & 0x7FF0000000000000) != 0x7FF0000000000000 {
        let d1 = f64::from_bits(v1);
        let d2 = f64::from_bits(v2);
        return (d1 + d2).to_bits();
    }
    0
}
```
**Compile:** `cargo build --release`

---

### 🐹 Go
Go can create addons using `cgo`.

```go
package main

/*
#include <stdint.h>
typedef struct { uint64_t bits; } Value;
typedef struct { Value* s; Value* f; Value* e; } StdVector;
*/
import "C"
import "unsafe"

//export add_numbers
func add_numbers(interp unsafe.Pointer, args *C.StdVector) C.uint64_t {
	len := (uintptr(unsafe.Pointer(args.f)) - uintptr(unsafe.Pointer(args.s))) / 8
	if len < 2 { return 0 }
	v1 := uint64(args.s.bits)
	v2 := uint64((*C.Value)(unsafe.Pointer(uintptr(unsafe.Pointer(args.s)) + 8)).bits)
	if (v1 & 0x7FF0000000000000) != 0x7FF0000000000000 && (v2 & 0x7FF0000000000000) != 0x7FF0000000000000 {
		d1 := *(*float64)(unsafe.Pointer(&v1))
		d2 := *(*float64)(unsafe.Pointer(&v2))
		res := d1 + d2
		return *(*C.uint64_t)(unsafe.Pointer(&res))
	}
	return 0
}
```
**Compile:** `go build -o libmy_addon.so -buildmode=c-shared main.go`

---

### 🏺 Odin
Odin's simple FFI makes it a great fit.

```odin
package main

Value :: struct { bits: u64 }
StdVector :: struct { start, finish, end: ^Value }

@(export)
add_numbers :: proc "c" (interp: rawptr, args: ^StdVector) -> u64 {
    len := (int(uintptr(args.finish)) - int(uintptr(args.start))) / 8
    if len < 2 { return 0 }
    v1 := args.start[0].bits
    v2 := args.start[1].bits
    if (v1 & 0x7FF0000000000000) != 0x7FF0000000000000 && (v2 & 0x7FF0000000000000) != 0x7FF0000000000000 {
        return transmute(u64)(transmute(f64)v1 + transmute(f64)v2)
    }
    return 0
}
```
**Compile:** `odin build addon.odin -build-mode:shared -out:libmy_addon.so`

---

### 👑 Nim
Nim is incredibly flexible and compiles to C.

```nim
type
  Value = object
    bits: uint64
  StdVector = object
    start, finish, storage: ptr Value

proc add_numbers(interp: pointer, args: ptr StdVector): uint64 {.exportc, dynlib, cdecl.} =
  let len = (cast[ByteAddress](args.finish) - cast[ByteAddress](args.start)) div 8
  if len < 2: return 0
  let v1 = args.start[0].bits
  let v2 = args.start[1].bits
  if (v1 and 0x7FF0000000000000) != 0x7FF0000000000000 and (v2 and 0x7FF0000000000000) != 0x7FF0000000000000:
    return cast[uint64](cast[float64](v1) + cast[float64](v2))
  return 0
```
**Compile:** `nim c --app:lib -d:release addon.nim`

---

### 💎 Crystal
Crystal is fast and Ruby-like.

```crystal
lib SP
  struct Value; bits : UInt64; end
  struct StdVector; start, finish, storage : Value*; end
end

fun add_numbers(interp : Void*, args : SP::StdVector*) : UInt64
  len = (args.value.finish.address - args.value.start.address) // 8
  return 0_u64 if len < 2
  v1 = args.value.start[0].bits
  v2 = args.value.start[1].bits
  if (v1 & 0x7FF0000000000000_u64) != 0x7FF0000000000000_u64 && (v2 & 0x7FF0000000000000_u64) != 0x7FF0000000000000_u64
    d1 = Boxed(Float64).new(v1).value
    d2 = Boxed(Float64).new(v2).value
    return (d1 + d2).unsafe_as(UInt64)
  end
  0_u64
end
```
**Compile:** `crystal build --shared addon.cr`

---

### 🍎 Swift
Swift's `@_cdecl` makes it easy to export symbols.

```swift
struct Value { var bits: UInt64 }
struct StdVector { var start, finish, storage: UnsafePointer<Value> }

@_cdecl("add_numbers")
public func add_numbers(interp: UnsafeMutableRawPointer?, args: UnsafePointer<StdVector>?) -> UInt64 {
    guard let args = args?.pointee else { return 0 }
    let len = (Int(bitPattern: args.finish) - Int(bitPattern: args.start)) / 8
    if len < 2 { return 0 }
    let v1 = args.start[0].bits
    let v2 = args.start[1].bits
    if (v1 & 0x7FF0000000000000) != 0x7FF0000000000000 && (v2 & 0x7FF0000000000000) != 0x7FF0000000000000 {
        let d1 = Double(bitPattern: v1)
        let d2 = Double(bitPattern: v2)
        return (d1 + d2).bitPattern
    }
    return 0
}
```
**Compile:** `swiftc -emit-library -o libmy_addon.so addon.swift`

---

### 🐉 D
D's `extern(C)` provides first-class interop.

```d
import std.stdio;

struct Value { ulong bits; }
struct StdVector { Value* start, finish, storage; }

extern(C) ulong add_numbers(void* interp, StdVector* args) {
    long len = (cast(long)args.finish - cast(long)args.start) / 8;
    if (len < 2) return 0;
    ulong v1 = args.start[0].bits;
    ulong v2 = args.start[1].bits;
    if ((v1 & 0x7FF0000000000000) != 0x7FF0000000000000 && (v2 & 0x7FF0000000000000) != 0x7FF0000000000000) {
        double d1 = *cast(double*)&v1;
        double d2 = *cast(double*)&v2;
        double res = d1 + d2;
        return *cast(ulong*)&res;
    }
    return 0;
}
```
**Compile:** `ldc2 -shared addon.d -of=libmy_addon.so`

---

### 👴 FreePascal
Classic, fast, and simple.

```pascal
type
  PValue = ^TValue;
  TValue = record bits: QWord; end;
  PStdVector = ^TStdVector;
  TStdVector = record start, finish, storage: PValue; end;

function add_numbers(interp: Pointer; args: PStdVector): QWord; cdecl; export;
var v1, v2: QWord;
begin
  if (PtrUint(args^.finish) - PtrUint(args^.start)) div 8 < 2 then Exit(0);
  v1 := args^.start[0].bits;
  v2 := args^.start[1].bits;
  if ((v1 and $7FF0000000000000) <> $7FF0000000000000) and ((v2 and $7FF0000000000000) <> $7FF0000000000000) then
    Exit(QWord(Pointer(@Double(Pointer(@v1)^ + Double(Pointer(@v2)^)))^));
  Result := 0;
end;
```
**Compile:** `fpc -Mdelphi -Tlinux -Pauto -Wl-shared addon.pas`


### 🐍 Python
Python can be used via **Cython** to generate a native shared library that exports C symbols.

```python
# addon.pyx
from libc.stdint cimport uint64_t

# Export the function to C
cdef public uint64_t add_numbers(void* interp, void* args_ptr):
    cdef sp_args* args = <sp_args*>args_ptr
    if (args.finish - args.start) < 2: return 0
    cdef uint64_t v1 = args.start[0].bits
    cdef uint64_t v2 = args.start[1].bits
    if (v1 & 0x7FF0000000000000) != 0x7FF0000000000000 and (v2 & 0x7FF0000000000000) != 0x7FF0000000000000:
        return (<uint64_t*>&(<double>(<double*>&v1)[0] + (<double*>&v2)[0]))[0]
    return 0
```
**Compile:** `cython addon.pyx && gcc -shared -o libmy_addon.so addon.c $(python3-config --includes --ldflags) -fPIC`

---

### ♯ C#
Modern C# supports **NativeAOT**, allowing you to compile your code into a standalone native shared library.

```csharp
using System.Runtime.InteropServices;

public class SPAddon {
    [UnmanagedCallersOnly(EntryPoint = "add_numbers")]
    public static ulong AddNumbers(IntPtr interp, IntPtr argsPtr) {
        unsafe {
            StdVector* args = (StdVector*)argsPtr;
            long len = (args->Finish - args->Start) / 8;
            if (len < 2) return 0;
            ulong v1 = args->Start[0].Bits;
            ulong v2 = args->Start[1].Bits;
            if ((v1 & 0x7FF0000000000000) != 0x7FF0000000000000 && (v2 & 0x7FF0000000000000) != 0x7FF0000000000000) {
                double d1 = BitConverter.Int64BitsToDouble((long)v1);
                double d2 = BitConverter.Int64BitsToDouble((long)v2);
                return (ulong)BitConverter.DoubleToInt64Bits(d1 + d2);
            }
        }
        return 0;
    }
}
```
**Compile:** `dotnet publish -c Release -r linux-x64 /p:NativeLib=Shared`

---

### ☕ Java
Using **GraalVM Native Image**, you can compile Java code into a native shared library with C-style exports.

```java
import org.graalvm.nativeimage.IsolateThread;
import org.graalvm.nativeimage.c.function.CEntryPoint;

public class SPAddon {
    @CEntryPoint(name = "add_numbers")
    public static long addNumbers(IsolateThread thread, long interp, long argsPtr) {
        long v1 = getBits(argsPtr, 0); 
        long v2 = getBits(argsPtr, 1);
        return Double.doubleToRawLongBits(Double.longBitsToDouble(v1) + Double.longBitsToDouble(v2));
    }
}
```
**Compile:** `native-image --shared -H:Name=libmy_addon`

---

### 🟣 Julia
Julia's `PackageCompiler.jl` can generate native shared libraries for FFI.

```julia
module MyAddon
    Base.@ccallable function add_numbers(interp::Ptr{Cvoid}, args_ptr::Ptr{Cvoid})::UInt64
        args = unsafe_load(Ptr{StdVector}(args_ptr))
        len = Int(args.finish - args.start) ÷ 8
        len < 2 && return 0
        v1 = unsafe_load(args.start, 1).bits
        v2 = unsafe_load(args.start, 2).bits
        if (v1 & 0x7FF0000000000000) != 0x7FF0000000000000 && (v2 & 0x7FF0000000000000) != 0x7FF0000000000000
            return reinterpret(UInt64, reinterpret(Float64, v1) + reinterpret(Float64, v2))
        end
        return 0
    end
end
```
**Compile:** `using PackageCompiler; create_library(".", "libmy_addon", lib_name="my_addon")`

---

### 📜 JavaScript
To use JavaScript, you can embed a tiny engine like **QuickJS** inside a C wrapper.

```c
#include "quickjs.h"
#include "sp_addon.h"

uint64_t add_numbers(void* interp, const sp_args* args) {
    if (SP_ARGS_LEN(args) < 2) return 0;
    sp_value v1 = SP_GET_ARG(args, 0);
    sp_value v2 = SP_GET_ARG(args, 1);
    if (SP_IS_NUMBER(v1) && SP_IS_NUMBER(v2)) {
        double res = SP_AS_NUMBER(v1) + SP_AS_NUMBER(v2);
        return SP_MAKE_NUMBER(res).bits;
    }
    return 0;
}
```
**Compile:** `gcc -shared -fPIC addon.c -lquickjs -o libmy_addon.so`

---

### 🎯 Kotlin
**Kotlin/Native** is designed for high-performance native binaries and easy C interop.

```kotlin
@CName("add_numbers")
fun add_numbers(interp: COpaquePointer?, args_ptr: COpaquePointer?): Long {
    val args = args_ptr?.reinterpret<StdVector>()?.pointed ?: return 0
    val len = (args.finish.rawValue - args.start.rawValue) / 8
    if (len < 2) return 0
    val v1 = args.start!![0].bits
    val v2 = args.start!![1].bits
    if ((v1 and 0x7FF0000000000000L.toULong()) != 0x7FF0000000000000L.toULong()) {
        val d1 = Double.fromBits(v1.toLong())
        val d2 = Double.fromBits(v2.toLong())
        return (d1 + d2).toBits()
    }
    return 0
}
```
**Compile:** `kotlinc-native addon.kt -produce library -o libmy_addon`

---

### λ Haskell
Haskell can export functions to C using the Foreign Function Interface (FFI).

```haskell
{-# LANGUAGE ForeignFunctionInterface #-}
module Addon where
import Foreign.Ptr
import Data.Word

foreign export ccall add_numbers :: Ptr () -> Ptr () -> IO Word64

add_numbers :: Ptr () -> Ptr StdVector -> IO Word64
add_numbers _ argsPtr = do
    args <- peek argsPtr
    let len = (ptrToWordPtr (finish args) - ptrToWordPtr (start args)) `div` 8
    if len < 2 then return 0
    else do
        v1 <- bits <$> peekElemOff (start args) 0
        v2 <- bits <$> peekElemOff (start args) 1
        if (v1 .&. 0x7FF0000000000000) /= 0x7FF0000000000000
           && (v2 .&. 0x7FF0000000000000) /= 0x7FF0000000000000
        then return $ reinterpret (asDouble v1 + asDouble v2)
        else return 0
```
**Compile:** `ghc -shared -dynamic -fPIC addon.hs -o libmy_addon.so`

---

### 🔢 Fortran
Modern Fortran's `iso_c_binding` makes it trivial to export C-compatible functions.

```fortran
function add_numbers(interp, args) bind(c, name="add_numbers")
    use iso_c_binding
    implicit none
    integer(c_int64_t) :: add_numbers
    type(c_ptr), value :: interp, args
    real(c_double) :: d1, d2
    integer(c_int64_t) :: b1, b2
    ! Logic to extract bits and bit-cast to double
    add_numbers = transfer(d1 + d2, add_numbers)
end function
```
**Compile:** `gfortran -shared -fPIC addon.f90 -o libmy_addon.so`

---

### ⚙️ Assembly (x86_64)
For the ultimate performance, you can write addons in pure assembly. This example uses **NASM** and standard System V ABI calling conventions.

```nasm
; addon.asm
; rdi = interp, rsi = args (sp_args*)
section .text
global add_numbers

add_numbers:
    mov rax, [rsi + 8]  ; rax = finish
    sub rax, [rsi]      ; rax = finish - start
    cmp rax, 16
    jl .error           ; Less than 2 doubles?

    mov r8, [rsi]       ; r8 = start
    mov r9, [r8]        ; r9 = v1.bits
    mov r10, [r8 + 8]   ; r10 = v2.bits

    ; NaN check (SP_IS_NUMBER)
    mov r11, 0x7FF0000000000000
    mov rdx, r9
    and rdx, r11
    cmp rdx, r11
    je .error
    mov rdx, r10
    and rdx, r11
    cmp rdx, r11
    je .error

    movq xmm0, r9
    movq xmm1, r10
    addsd xmm0, xmm1    ; Floating point addition
    movq rax, xmm0      ; Bitwise return
    ret

.error:
    xor rax, rax
    ret
```
**Compile:** `nasm -f elf64 addon.asm -o addon.o && gcc -shared -fPIC addon.o -o libmy_addon.so`

---

### 💼 COBOL
Even business logic from the 60s can run on SP! Using **GnuCOBOL**, you can export C-compatible symbols.

```cobol
IDENTIFICATION DIVISION.
PROGRAM-ID. add_numbers.
DATA DIVISION.
LINKAGE SECTION.
01 INTERP USAGE POINTER.
01 ARGS-PTR USAGE POINTER.
01 ARGS.
    05 START-PTR USAGE POINTER.
    05 FINISH-PTR USAGE POINTER.
    05 END-STORAGE-PTR USAGE POINTER.
PROCEDURE DIVISION USING BY VALUE INTERP, BY VALUE ARGS-PTR.
    SET ADDRESS OF ARGS TO ARGS-PTR.
    * Perform manual bit extraction or call C helper...
    * GnuCOBOL allows mixing C and COBOL for high-performance math.
    GOBACK.
```
**Compile:** `cobc -z -O3 -shared addon.cob -o libmy_addon.so`

---

### 🐱 LOLCODE
I CAN HAS NATIVE ADDON? Using a C-transpiler or wrapper, you can write SP logic in **LOLCODE**.

```lolcode
HAI 1.2
  CAN HAS STDIO?
  I HAS A ARGS ITZ A POINTER
  I HAS A V1 ITZ ARG 0
  I HAS A V2 ITZ ARG 1
  VISIBLE "CALCULATING SUM..."
  I HAS A SUM ITZ SUM OF V1 AN V2
  FOUND IT SUM
KTHXBYE
```
**Link:** `gcc -shared -fPIC addon.c -llolcode -o libmy_addon.so`

---

### 💀 Brainfuck
The ultimate systems programming language. Since Brainfuck is minimalist, it is usually hosted inside a small C wrapper that executes the code.

```brainfuck
[ SP Addon: add_numbers ]
,>,[<+>-]<.
```
**Compile:** `gcc -shared -fPIC bf_wrapper.c -o libmy_addon.so`

---

### 🔱 Common Lisp
**ECL (Embeddable Common Lisp)** allows you to compile Lisp into native machine code and export C symbols.

```lisp
(defun add-numbers (interp args)
  (declare (type (pointer-to void) interp args))
  (ffi:c-inline (interp args) (:pointer-void :pointer-void) :uint64_t
    "/* Add logic here */"
    :one-liner t))
```
```
**Compile:** `ecl -build-node shared-library -o libmy_addon.so addon.lisp`

---

### 🐚 Bash
Because why not? While Bash cannot be compiled into a `.so` directly, you can create a thin C wrapper that calls your script or uses `popen` to execute shell commands.

```c
#include "sp_addon.h"
#include <stdio.h>
#include <stdlib.h>

uint64_t add_numbers(void* i, const sp_args* args) {
    if (SP_ARGS_LEN(args) < 2) return 0;
    
    double d1 = SP_AS_NUMBER(SP_GET_ARG(args, 0));
    double d2 = SP_AS_NUMBER(SP_GET_ARG(args, 1));
    
    char cmd[256];
    sprintf(cmd, "echo 'scale=10; %f + %f' | bc", d1, d2);
    
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) return 0;
    
    double result;
    if (fscanf(fp, "%lf", &result) != 1) result = 0;
    pclose(fp);
    
    return SP_MAKE_NUMBER(result).bits;
}
```
**Compile:** `gcc -O3 -shared -fPIC addon.c -o libmy_addon.so`

---


### 🛠️ Compiling & Linking Addons
When compiling your addon, there are a few critical requirements:

1.  **Shared Library**: The output MUST be a shared object (`.so`).
2.  **Position Independent Code**: You must use `-fPIC` (C/C++) or equivalent options.
3.  **No Linking Required**: Because the SP interpreter is self-contained, your addon **does not need to link against the SP binary** at compile-time. Symbols are resolved dynamically when the addon is loaded.

> [!TIP]
> Always ensure your shared object starts with the `lib` prefix (e.g., `libmath.so`) to be correctly discovered by the `use` statement in SP.

---

Happy Coding with **SP**! 

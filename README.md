# What is this?

This [will be] is a virtual machine and associated tooling that simulate a microcomputer
architecture that is designed to violate almost every assumption made by programmers
about how things work at the low level.  Specifically, in every way this was possible
without making the result unusuable, every decision as been made to be the least usual
in a modern architecture.

It's essentially the unholy child of a blasphemous union between a PDP-1 and a M68k CPU.

The bullet points:
* Bytes are 9 bits, and not octets
* Memory layout of integral types are big-endian for words (most significant byte
  first) but larger integers store words little-endian.
* Arithmetic is signed-magnitude
* Address registers are larger than data registers and cannot be interchanged
* The address space is segmented and nonlinear in such a way that the address
  whose binary representation is 0 is both common and useful

# Oh God why?

This is meant as a crucible for portability.  The intent is that if you are writing a backend
for a complier or interpreter of some sort, having it sucessfuly generate (working) code
for the nightmare VM confirms that you make no unwarranted assumptions about the target
architecture that are likely to break someday.

This is true also with one step of remove: a C compiler targetting this architecture would
be fully compliant with the standard but be effectively a trial by fire for portability
of the C program itself:  it violates most assumptions that the language never makes
but that the *programmers* almost always do (such as memory layout, size of bytes,
integer overflow, and how pointers behave).

(Writing a C compiler for the Nightmare architecture is left as an exercise for the reader
 and is almost certainly more complicated than first appears; `LLVM` for instance makes
 many assumptions about the target architecture that the *compiler* doesn't require such
 as the [size of a byte](https://archive.fosdem.org/2017/schedule/event/llvm_16_bit/attachments/slides/1839/export/events/attachments/llvm_16_bit/slides/1839/fosdem_2017_llvm_16bit_char.pdf)).

But mostly, this was written as an exercise in itself for _this_ programmer as the result
of a conversation about portability.  :-)

# Building

Ironically(?) neither the VM nor its associated tools are very portable:  they expect
a POSIX environment with a C++20 compiler (the Makefiles use `g++` by default).  That said,
most of the dependencies on the environment are _fairly_ isolated and it shouldn't be
too hard to adapt.

The assembler requires [RE-flex](https://github.com/Genivia/RE-flex) and `libutf8proc` for
proper UTF-8 support.  The former can be built easily from its repo and the latter is
almost certainly available in your favorite package manager.

> *omoikane — 3/17/26, 7:56 PM
> You see, most VMs, you know, will have 8 bit bytes.  You're on 255 here, all the way up, all the way up, you're 255 on your byte.  Where can you go from there?  Where?  Nowhere.  Exactly.  What we do is, if we need that extra push over the cliff, we put it on the 9th bit.*

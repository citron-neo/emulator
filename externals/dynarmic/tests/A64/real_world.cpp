// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <oaknut/oaknut.hpp>

#include "dynarmic/tests/A64/testenv.h"
#include "dynarmic/tests/native/testenv.h"
#include "dynarmic/interface/A64/a64.h"

using namespace Dynarmic;
/* Following C program:
int M[64];
int grob(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l) {
    M[a] += M[b]; // TOTAL GCC DESTRUCTION
    return a * b * c * d * e * f * g * h * i * j * k * l;
}
int _start() {
    return grob(
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12]),
        grob(M[1], M[2], M[3], M[4], M[5], M[6], M[7], M[8], M[9], M[10], M[11], M[12])
    );
}
#ifdef __x86_64__
#include <stdio.h>
int main() {
  return printf("%i", start_e());
}
#endif

cat < a64-linker.ld >> EOF
ENTRY(_start);
PHDRS { text PT_LOAD; rodata PT_LOAD; data PT_LOAD; }
SECTIONS {
    . = 0;
    .text : { *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :rodata
    .data : ALIGN(CONSTANT(MAXPAGESIZE)) { *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }
}
EOF
aarch64-linux-gnu-gcc -Wl,-Ta64-linker.ld -Wall -Wextra -ffreestanding -nostdlib -fno-whole-program -O2 grob.c -o grob | aarch64-linux-gnu-objdump -SC grob | awk '{print "env.code_mem.emplace_back(0x"$2"); //" $0}'
aarch64-linux-gnu-gcc -Wl,-Ta64-linker.ld -Wall -Wextra -ffreestanding -nostdlib -fno-whole-program -O2 grob.c -o grob | aarch64-linux-gnu-objdump -SC grob | awk '{print $2", "}'
*/
TEST_CASE("high register pressure proper handling with block linking 1", "[a64][c]") {
    A64TestEnv env;
    A64::UserConfig conf{};
    conf.callbacks = &env;
    A64::Jit jit{conf};

    REQUIRE(conf.HasOptimization(OptimizationFlag::BlockLinking));
    env.code_mem = { 0x90000008, 0x91230108, 0xb860d909, 0xb861d90a, 0x0b0a0129, 0xb820d909, 0x1b017c00, 0xb94003e1, 0x1b027c00, 0x1b037c00, 0x1b047c00, 0x1b057c00, 0x1b067c00, 0x1b077c00, 0x1b017c00, 0xb9400be1, 0x1b017c00, 0xb94013e1, 0x1b017c00, 0xb9401be1, 0x1b017c00, 0xd65f03c0, 0xd503201f, 0xd503201f, 0xa9a27bfd, 0x90000000, 0x91230000, 0x910003fd, 0xa90153f3, 0xa9025bf5, 0xa90363f7, 0xa9046bf9, 0xa90573fb, 0x29408c01, 0x2941b40e, 0xb863d804, 0xb861d802, 0x2942ac0c, 0x0b040042, 0x1b037c24, 0x2943a40a, 0x29449c08, 0x1b0e7c84, 0x29459406, 0xb821d802, 0x1b0d7c84, 0x29408c01, 0x2941b40e, 0x1b0c7c84, 0x1b0b7c84, 0x2942ac0c, 0x1b0a7c84, 0x1b097c84, 0x2943a40a, 0x1b087c84, 0x1b077c84, 0x29449c08, 0xb863d80f, 0x1b037c23, 0x1b067c84, 0xb861d802, 0x0b0f0042, 0x1b0e7c63, 0x1b057c84, 0x29459406, 0xb821d802, 0x1b0d7c63, 0x2943f002, 0x2940d801, 0x1b0c7c63, 0x2941e81b, 0x2942e019, 0x1b0b7c63, 0xb90067e2, 0x1b0a7c63, 0x1b097c63, 0x1b087c63, 0x1b077c63, 0x1b067c63, 0x1b057c63, 0x29449402, 0x290d17e2, 0xb876d805, 0xb861d802, 0x29459c06, 0x0b050042, 0xb821d802, 0x1b167c21, 0x290e1fe6, 0x2940d40c, 0x1b1b7c21, 0x2941a408, 0x290f27e8, 0x2942ac0a, 0x1b1a7c21, 0xb86cd802, 0xb875d805, 0x29102fea, 0x0b050042, 0x1b197c21, 0x2943b80d, 0x29113bed, 0x2944c00f, 0x291243ef, 0x1b187c21, 0x2945c811, 0xb82cd802, 0x29134bf1, 0x1b157d8c, 0x2941f813, 0x2940d00b, 0x29147bf3, 0x29429402, 0x291517e2, 0x29439c06, 0x29161fe6, 0x2944a408, 0xb874d805, 0xb86bd802, 0x291727e8, 0x0b050042, 0x2945b80a, 0xb82bd802, 0x29183bea, 0x1b147d6b, 0x2941c00f, 0x2940cc0a, 0x291943ef, 0x2942c811, 0x291a4bf1, 0x2943881e, 0x291b0bfe, 0x29449805, 0x291c1be5, 0x2945a007, 0x291d23e7, 0xb86ad802, 0xb873d805, 0x0b050042, 0xb82ad802, 0x1b137d4a, 0x2941b80d, 0x2940f809, 0x291e3bed, 0x2942c00f, 0x291f43ef, 0x2943c811, 0xb90103f1, 0xb90107f2, 0x29449402, 0xb9010be2, 0xb9010fe5, 0xb869d802, 0xb87ed805, 0x29459c06, 0x0b050042, 0xb829d802, 0x1b1e7d29, 0xb90113e6, 0xb90117e7, 0x2941bc0e, 0x2940c808, 0xb9011bee, 0xb9011fef, 0x2942c410, 0xb90123f0, 0xb90127f1, 0x29439402, 0xb9012be2, 0xb9012fe5, 0xb868d802, 0x29449c06, 0xb90133e6, 0xb90137e7, 0xb872d805, 0x2945b80d, 0x0b050042, 0xb828d802, 0x1b127d08, 0xb9013bed, 0xb9013fee, 0x2941c00f, 0x2940c407, 0xb90143ef, 0xb90147f0, 0x29429402, 0xb9014be2, 0xb9014fe5, 0x2943b806, 0xb90153e6, 0xb90157ee, 0x2944c00f, 0xb9015bef, 0xb9015ff0, 0x29459402, 0xb90163e2, 0xb867d802, 0xb90167e5, 0xb871d805, 0x0b050042, 0xb827d802, 0x1b117ce7, 0x2940c002, 0x2941b406, 0xb9016be6, 0xb9016fed, 0x2942bc0e, 0xb90173ee, 0xb90177ef, 0x29439805, 0xb9017be5, 0xb9017fe6, 0x2944bc0e, 0xb90183ee, 0xb90187ef, 0x29459805, 0xb9018be5, 0xb862d805, 0xb9018fe6, 0xb870d806, 0x0b0600a5, 0xb822d805, 0x1b107c42, 0x2941b80d, 0x2940bc06, 0xb90193ed, 0xb90197ee, 0x2942b805, 0xb9019be5, 0xb9019fee, 0x2943b405, 0xb901a3e5, 0xb901a7ed, 0xb86fd80d, 0x2944940e, 0xb901abee, 0xb901afe5, 0x2945940e, 0xb901b7e5, 0xb866d805, 0xb901b3ee, 0x0b0d00a5, 0xb826d805, 0x2941dc0d, 0x2940b805, 0xb901bbed, 0xb901bff7, 0x2942b417, 0xb901c3f7, 0xb901c7ed, 0x2943b417, 0xb901cbf7, 0xb901cfed, 0x2944b417, 0xb901d3f7, 0xb901d7ed, 0x2945b417, 0xb901dbf7, 0xb86ed817, 0xb901dfed, 0xb865d80d, 0x0b1701ad, 0xb825d80d, 0xb863d817, 0x1b047c63, 0xb864d80d, 0x0b1701ad, 0xb824d80d, 0xb94067e0, 0xb9408bed, 0x1b007c21, 0xb9406be0, 0x1b1c7c21, 0x1b007c21, 0xb9406fe0, 0x1b007c21, 0xb94073e0, 0x1b007c21, 0xb94077e0, 0x1b007c21, 0xb9407be0, 0x1b007d8c, 0xb9407fe0, 0x1b037c21, 0x1b007d8c, 0xb94083e0, 0x1b007d8c, 0xb94087e0, 0x1b007d8c, 0xb9408fe0, 0x1b0d7d8c, 0xb940f3ed, 0x1b007d8c, 0xb94093e0, 0x1b0d7d29, 0x1b007d8c, 0xb94097e0, 0x1b007d8c, 0xb9409be0, 0x1b007d8c, 0xb9409fe0, 0x1b007d8c, 0xb940a3e0, 0x1b007d6b, 0xb940a7e0, 0x1b0c7c21, 0x1b007d6b, 0xb940abe0, 0x1b007d6b, 0xb940afe0, 0x1b007d6b, 0xb940b3e0, 0x1b007d6b, 0xb940b7e0, 0x1b007d6b, 0xb940bbe0, 0xb9413bed, 0x1b007d6b, 0xb940bfe0, 0x1b007d6b, 0xb940c3e0, 0x1b007d6b, 0xb940c7e0, 0x1b007d6b, 0xb940cbe0, 0x1b007d4a, 0xb940cfe0, 0x1b0b7c21, 0x1b007d4a, 0xb940d3e0, 0x1b007d4a, 0xb940d7e0, 0x1b007d4a, 0xb940dbe0, 0x1b007d4a, 0xb940dfe0, 0x1b007d4a, 0xb940e3e0, 0x1b007d4a, 0xb940e7e0, 0x1b007d4a, 0xb940ebe0, 0x1b007d4a, 0xb940efe0, 0x1b007d4a, 0xb940f7e0, 0x1b007d29, 0xb940fbe0, 0x1b0a7c21, 0x1b007d29, 0x295f8fe0, 0x1b007d20, 0x1b037c00, 0xb94107e3, 0x1b037c00, 0xb9410be3, 0x1b037c00, 0xb9410fe3, 0x1b037c00, 0xb94113e3, 0x1b037c00, 0xb94117e3, 0x1b037c00, 0x1b007c21, 0xb9411be0, 0x1b007d08, 0xb9411fe0, 0x1b007d08, 0xb94123e0, 0x1b007d08, 0xb94127e0, 0x1b007d08, 0xb9412be0, 0x1b007d08, 0xb9412fe0, 0x1b007d08, 0xb94133e0, 0x1b007d08, 0xb94137e0, 0x1b007d08, 0xb9413fe0, 0xb941bff7, 0xa94153f3, 0x1b0d7d08, 0xb9416fed, 0xa9425bf5, 0xa9446bf9, 0x1b007d08, 0xb94143e0, 0x1b087c21, 0x1b007ce7, 0xb94147e0, 0x1b007ce7, 0xb9414be0, 0x1b007ce7, 0xb9414fe0, 0x1b007ce7, 0xb94153e0, 0x1b007ce7, 0xb94157e0, 0x1b007ce7, 0xb9415be0, 0x1b007ce7, 0xb9415fe0, 0x1b007ce7, 0xb94163e0, 0x1b007ce7, 0xb94167e0, 0x1b007ce7, 0xb9416be0, 0x1b007c42, 0xb94173e0, 0x1b077c21, 0x1b0d7c42, 0xb94193ed, 0x1b007c42, 0xb94177e0, 0x1b007c42, 0xb9417be0, 0x1b007c42, 0xb9417fe0, 0x1b007c42, 0xb94183e0, 0x1b007c40, 0xb94187e2, 0x1b027c00, 0xb9418be2, 0x1b027c00, 0xb9418fe2, 0x1b027c00, 0xb94197e2, 0x1b007c20, 0x1b0f7cc1, 0x1b0d7c21, 0xb941a7ed, 0x1b027c21, 0xb9419be2, 0x1b027c21, 0xb9419fe2, 0xa94573fb, 0x1b027c21, 0xb941a3e2, 0x1b027c21, 0xb941abe2, 0x1b0d7c21, 0xb941bbed, 0x1b027c21, 0xb941afe2, 0x1b027c21, 0xb941b3e2, 0x1b027c21, 0xb941b7e2, 0x1b027c21, 0x1b017c01, 0x1b0e7ca0, 0x1b0d7c00, 0xb941c7ed, 0x1b177c00, 0xb941c3f7, 0x1b177c00, 0xb941cbf7, 0x1b0d7c00, 0xb941cfed, 0x1b177c00, 0xb941d3f7, 0x1b0d7c00, 0xb941d7ed, 0x1b177c00, 0xb941dbf7, 0x1b0d7c00, 0xb941dfed, 0x1b177c00, 0xa94363f7, 0xa8de7bfd, 0x1b0d7c00, 0x1b007c20, 0x14000000 };
    jit.SetPC(0x60); // at _start
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.GetRegister(0) == 0);
}

/*
Following C program:
extern int printf(const char*, ...);
int square(int num) {
    return (num > 10) ? printf((void*)(num - 10)) : num * num;
}
*/
TEST_CASE("Block branching (unpredictable)", "[a64][c]") {
    A64TestEnv env;
    A64::UserConfig conf{};
    conf.callbacks = &env;
    //conf.very_verbose_debugging_output = true;
    A64::Jit jit{conf};
    REQUIRE(conf.HasOptimization(OptimizationFlag::BlockLinking));
    oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
    {
        using namespace oaknut::util;
        oaknut::Label lb0_2, lb_printf, lb_hlt;
        code.ADD(W0, W0, 11);
        code.CMP(W0, 11);
        code.B(LT, lb0_2);
        code.SUB(W0, W0, 10);
        code.B(lb_printf);
        code.l(lb0_2);
        code.MUL(W0, W0, W0);
        code.l(lb_hlt);
        code.B(lb_hlt);
        code.l(lb_printf);
        code.RET();
    }
    jit.SetPC(0); // at _start
    env.ticks_left = env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });
}

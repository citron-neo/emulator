// SPDX-FileCopyrightText: Copyright © 2020 Skyline Team and Contributors
// SPDX-License-Identifier: MPL-2.0

#include "common/bit_field.h"
#include "common/common_types.h"

namespace Core::NCE {

enum SystemRegister : u32 {
    TpidrEl0 = 0x5E82,
    TpidrroEl0 = 0x5E83,
    CntfrqEl0 = 0x5F00,
    CntpctEl0 = 0x5F01,
};

// https://developer.arm.com/documentation/ddi0596/2021-12/Base-Instructions/SVC--Supervisor-Call-
union SVC {
    constexpr explicit SVC(u32 raw_) : raw{raw_} {}

    constexpr bool Verify() {
        return (this->GetSig0() == 0x1 && this->GetSig1() == 0x6A0);
    }

    constexpr u32 GetSig0() {
        return decltype(sig0)::ExtractValue(raw);
    }

    constexpr u32 GetValue() {
        return decltype(value)::ExtractValue(raw);
    }

    constexpr u32 GetSig1() {
        return decltype(sig1)::ExtractValue(raw);
    }

    u32 raw;

private:
    BitField<0, 5, u32> sig0;   // 0x1
    BitField<5, 16, u32> value; // 16-bit immediate
    BitField<21, 11, u32> sig1; // 0x6A0
};
static_assert(sizeof(SVC) == sizeof(u32));
static_assert(SVC(0xD40000C1).Verify());
static_assert(SVC(0xD40000C1).GetValue() == 0x6);

// https://developer.arm.com/documentation/ddi0596/2021-12/Base-Instructions/MRS--Move-System-Register-
union MRS {
    constexpr explicit MRS(u32 raw_) : raw{raw_} {}

    constexpr bool Verify() {
        return (this->GetSig() == 0xD53);
    }

    constexpr u32 GetRt() {
        return decltype(rt)::ExtractValue(raw);
    }

    constexpr u32 GetSystemReg() {
        return decltype(system_reg)::ExtractValue(raw);
    }

    constexpr u32 GetSig() {
        return decltype(sig)::ExtractValue(raw);
    }

    u32 raw;

private:
    BitField<0, 5, u32> rt;          // destination register
    BitField<5, 15, u32> system_reg; // source system register
    BitField<20, 12, u32> sig;       // 0xD53
};
static_assert(sizeof(MRS) == sizeof(u32));
static_assert(MRS(0xD53BE020).Verify());
static_assert(MRS(0xD53BE020).GetSystemReg() == CntpctEl0);
static_assert(MRS(0xD53BE020).GetRt() == 0x0);

// https://developer.arm.com/documentation/ddi0596/2021-12/Base-Instructions/MSR--register---Move-general-purpose-register-to-System-Register-
union MSR {
    constexpr explicit MSR(u32 raw_) : raw{raw_} {}

    constexpr bool Verify() {
        return this->GetSig() == 0xD51;
    }

    constexpr u32 GetRt() {
        return decltype(rt)::ExtractValue(raw);
    }

    constexpr u32 GetSystemReg() {
        return decltype(system_reg)::ExtractValue(raw);
    }

    constexpr u32 GetSig() {
        return decltype(sig)::ExtractValue(raw);
    }

    u32 raw;

private:
    BitField<0, 5, u32> rt;          // source register
    BitField<5, 15, u32> system_reg; // destination system register
    BitField<20, 12, u32> sig;       // 0xD51
};
static_assert(sizeof(MSR) == sizeof(u32));
static_assert(MSR(0xD51BD040).Verify());
static_assert(MSR(0xD51BD040).GetSystemReg() == TpidrEl0);
static_assert(MSR(0xD51BD040).GetRt() == 0x0);

// https://developer.arm.com/documentation/ddi0596/2021-12/Base-Instructions/LDXR--Load-Exclusive-Register-
// https://developer.arm.com/documentation/ddi0596/2021-12/Base-Instructions/LDXP--Load-Exclusive-Pair-of-Registers-
// https://developer.arm.com/documentation/ddi0596/2021-12/Base-Instructions/STXR--Store-Exclusive-Register-
// https://developer.arm.com/documentation/ddi0596/2021-12/Base-Instructions/STXP--Store-Exclusive-Pair-of-registers-
union Exclusive {
    constexpr explicit Exclusive(u32 raw_) : raw{raw_} {}

    constexpr bool Verify() {
        return this->GetSig() == 0x10;
    }

    constexpr u32 GetSig() {
        return decltype(sig)::ExtractValue(raw);
    }

    constexpr u32 AsOrdered() {
        return raw | decltype(o0)::FormatValue(1);
    }

    u32 raw;

private:
    BitField<0, 5, u32> rt;    // memory operand
    BitField<5, 5, u32> rn;    // register operand 1
    BitField<10, 5, u32> rt2;  // register operand 2
    BitField<15, 1, u32> o0;   // ordered
    BitField<16, 5, u32> rs;   // status register
    BitField<21, 2, u32> l;    // operation type
    BitField<23, 7, u32> sig;  // 0x10
    BitField<30, 2, u32> size; // size
};
static_assert(Exclusive(0xC85FFC00).Verify());
static_assert(Exclusive(0xC85FFC00).AsOrdered() == 0xC85FFC00);
static_assert(Exclusive(0xC85F7C00).AsOrdered() == 0xC85FFC00);
static_assert(Exclusive(0xC8200440).AsOrdered() == 0xC8208440);

} // namespace Core::NCE

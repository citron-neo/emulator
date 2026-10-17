/* This file is part of the sirit project.
 * Copyright (c) 2019 sirit
 * This software may be used and distributed according to the terms of the
 * 3-Clause BSD License
 */

#include "sirit/sirit.h"

#include "stream.h"

namespace Sirit {

Id Module::OpSubgroupBallotKHR(Id result_type, Id predicate) {
    code->Reserve(4);
    return *code << OpId{spv::Op::OpSubgroupBallotKHR, result_type} << predicate << EndOp{};
}

Id Module::OpSubgroupReadInvocationKHR(Id result_type, Id value, Id index) {
    code->Reserve(5);
    return *code << OpId{spv::Op::OpSubgroupReadInvocationKHR, result_type} << value << index
                 << EndOp{};
}

Id Module::OpSubgroupAllKHR(Id result_type, Id predicate) {
    code->Reserve(4);
    return *code << OpId{spv::Op::OpSubgroupAllKHR, result_type} << predicate << EndOp{};
}

Id Module::OpSubgroupAnyKHR(Id result_type, Id predicate) {
    code->Reserve(4);
    return *code << OpId{spv::Op::OpSubgroupAnyKHR, result_type} << predicate << EndOp{};
}

Id Module::OpSubgroupAllEqualKHR(Id result_type, Id predicate) {
    code->Reserve(4);
    return *code << OpId{spv::Op::OpSubgroupAllEqualKHR, result_type} << predicate << EndOp{};
}

Id Module::OpGroupNonUniformElect(Id result_type, Id scope) {
    code->Reserve(4);
    return *code << OpId{spv::Op::OpGroupNonUniformElect, result_type} << scope << EndOp{};
}

Id Module::OpGroupNonUniformBroadcastFirst(Id result_type, Id scope, Id value) {
    code->Reserve(5);
    return *code << OpId{spv::Op::OpGroupNonUniformBroadcastFirst, result_type} << scope << value
                 << EndOp{};
}

Id Module::OpGroupNonUniformBroadcast(Id result_type, Id scope, Id value, Id id) {
    code->Reserve(6);
    return *code << OpId{spv::Op::OpGroupNonUniformBroadcast, result_type} << scope << value
                 << id << EndOp{};
}

Id Module::OpGroupNonUniformShuffle(Id result_type, Id scope, Id value, Id id) {
    code->Reserve(6);
    return *code << OpId{spv::Op::OpGroupNonUniformShuffle, result_type} << scope << value << id
                  << EndOp{};
}

Id Module::OpGroupNonUniformShuffleXor(Id result_type, Id scope, Id value, Id mask) {
    code->Reserve(6);
    return *code << OpId{spv::Op::OpGroupNonUniformShuffleXor, result_type} << scope << value
                 << mask << EndOp{};
}

Id Module::OpGroupNonUniformQuadSwap(Id result_type, Id scope, Id value, Id direction) {
    code->Reserve(6);
    return *code << OpId{spv::Op::OpGroupNonUniformQuadSwap, result_type} << scope << value
                 << direction << EndOp{};
}

Id Module::OpGroupNonUniformAll(Id result_type, Id scope, Id predicate) {
   code->Reserve(5);
   return *code << OpId{spv::Op::OpGroupNonUniformAll, result_type} << scope << predicate << EndOp{};
}

Id Module::OpGroupNonUniformAny(Id result_type, Id scope, Id predicate) {
   code->Reserve(5);
   return *code << OpId{spv::Op::OpGroupNonUniformAny, result_type} << scope << predicate << EndOp{};
}

Id Module::OpGroupNonUniformAllEqual(Id result_type, Id scope, Id value) {
   code->Reserve(5);
   return *code << OpId{spv::Op::OpGroupNonUniformAllEqual, result_type} << scope << value << EndOp{};
}

Id Module::OpGroupNonUniformBallot(Id result_type, Id scope, Id predicate) {
   code->Reserve(5);
   return *code << OpId{spv::Op::OpGroupNonUniformBallot, result_type} << scope << predicate << EndOp{};
}

} // namespace Sirit

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/result.h"

namespace FileSys {

constexpr Result ResultPathNotFound{ErrorModule::FS, 1};
constexpr Result ResultPathAlreadyExists{ErrorModule::FS, 2};
constexpr Result ResultUnsupportedSdkVersion{ErrorModule::FS, 50};
constexpr Result ResultPartitionNotFound{ErrorModule::FS, 1001};
constexpr Result ResultTargetNotFound{ErrorModule::FS, 1002};
constexpr Result ResultPortSdCardNoDevice{ErrorModule::FS, 2001};
constexpr Result ResultNotImplemented{ErrorModule::FS, 3001};
constexpr Result ResultUnsupportedVersion{ErrorModule::FS, 3002};
constexpr Result ResultOutOfRange{ErrorModule::FS, 3005};
constexpr Result ResultAllocationMemoryFailedInFileSystemBuddyHeapA{ErrorModule::FS, 3294};
constexpr Result ResultAllocationMemoryFailedInNcaFileSystemDriverI{ErrorModule::FS, 3341};
constexpr Result ResultAllocationMemoryFailedInNcaReaderA{ErrorModule::FS, 3363};
constexpr Result ResultAllocationMemoryFailedInAesCtrCounterExtendedStorageA{ErrorModule::FS, 3399};
constexpr Result ResultAllocationMemoryFailedInIntegrityRomFsStorageA{ErrorModule::FS, 3412};
constexpr Result ResultAllocationMemoryFailedMakeUnique{ErrorModule::FS, 3422};
constexpr Result ResultAllocationMemoryFailedAllocateShared{ErrorModule::FS, 3423};
constexpr Result ResultInvalidAesCtrCounterExtendedEntryOffset{ErrorModule::FS, 4012};
constexpr Result ResultIndirectStorageCorrupted{ErrorModule::FS, 4021};
constexpr Result ResultInvalidIndirectEntryOffset{ErrorModule::FS, 4022};
constexpr Result ResultInvalidIndirectEntryStorageIndex{ErrorModule::FS, 4023};
constexpr Result ResultInvalidIndirectStorageSize{ErrorModule::FS, 4024};
constexpr Result ResultInvalidBucketTreeSignature{ErrorModule::FS, 4032};
constexpr Result ResultInvalidBucketTreeEntryCount{ErrorModule::FS, 4033};
constexpr Result ResultInvalidBucketTreeNodeEntryCount{ErrorModule::FS, 4034};
constexpr Result ResultInvalidBucketTreeNodeOffset{ErrorModule::FS, 4035};
constexpr Result ResultInvalidBucketTreeEntryOffset{ErrorModule::FS, 4036};
constexpr Result ResultInvalidBucketTreeEntrySetOffset{ErrorModule::FS, 4037};
constexpr Result ResultInvalidBucketTreeNodeIndex{ErrorModule::FS, 4038};
constexpr Result ResultInvalidBucketTreeVirtualOffset{ErrorModule::FS, 4039};
constexpr Result ResultRomNcaInvalidPatchMetaDataHashType{ErrorModule::FS, 4084};
constexpr Result ResultRomNcaInvalidIntegrityLayerInfoOffset{ErrorModule::FS, 4085};
constexpr Result ResultRomNcaInvalidPatchMetaDataHashDataSize{ErrorModule::FS, 4086};
constexpr Result ResultRomNcaInvalidPatchMetaDataHashDataOffset{ErrorModule::FS, 4087};
constexpr Result ResultRomNcaInvalidPatchMetaDataHashDataHash{ErrorModule::FS, 4088};
constexpr Result ResultRomNcaInvalidSparseMetaDataHashType{ErrorModule::FS, 4089};
constexpr Result ResultRomNcaInvalidSparseMetaDataHashDataSize{ErrorModule::FS, 4090};
constexpr Result ResultRomNcaInvalidSparseMetaDataHashDataOffset{ErrorModule::FS, 4091};
constexpr Result ResultRomNcaInvalidSparseMetaDataHashDataHash{ErrorModule::FS, 4091};
constexpr Result ResultNcaBaseStorageOutOfRangeB{ErrorModule::FS, 4509};
constexpr Result ResultNcaBaseStorageOutOfRangeC{ErrorModule::FS, 4510};
constexpr Result ResultNcaBaseStorageOutOfRangeD{ErrorModule::FS, 4511};
constexpr Result ResultInvalidNcaSignature{ErrorModule::FS, 4517};
constexpr Result ResultNcaFsHeaderHashVerificationFailed{ErrorModule::FS, 4520};
constexpr Result ResultInvalidNcaKeyIndex{ErrorModule::FS, 4521};
constexpr Result ResultInvalidNcaFsHeaderHashType{ErrorModule::FS, 4522};
constexpr Result ResultInvalidNcaFsHeaderEncryptionType{ErrorModule::FS, 4523};
constexpr Result ResultInvalidNcaPatchInfoIndirectSize{ErrorModule::FS, 4524};
constexpr Result ResultInvalidNcaPatchInfoAesCtrExSize{ErrorModule::FS, 4525};
constexpr Result ResultInvalidNcaPatchInfoAesCtrExOffset{ErrorModule::FS, 4526};
constexpr Result ResultInvalidNcaHeader{ErrorModule::FS, 4528};
constexpr Result ResultInvalidNcaFsHeader{ErrorModule::FS, 4529};
constexpr Result ResultNcaBaseStorageOutOfRangeE{ErrorModule::FS, 4530};
constexpr Result ResultInvalidHierarchicalSha256BlockSize{ErrorModule::FS, 4532};
constexpr Result ResultInvalidHierarchicalSha256LayerCount{ErrorModule::FS, 4533};
constexpr Result ResultHierarchicalSha256BaseStorageTooLarge{ErrorModule::FS, 4534};
constexpr Result ResultHierarchicalSha256HashVerificationFailed{ErrorModule::FS, 4535};
constexpr Result ResultInvalidNcaHierarchicalIntegrityVerificationLayerCount{ErrorModule::FS, 4541};
constexpr Result ResultInvalidNcaIndirectStorageOutOfRange{ErrorModule::FS, 4542};
constexpr Result ResultInvalidNcaHeader1SignatureKeyGeneration{ErrorModule::FS, 4543};
constexpr Result ResultInvalidCompressedStorageSize{ErrorModule::FS, 4547};
constexpr Result ResultInvalidNcaMetaDataHashDataSize{ErrorModule::FS, 4548};
constexpr Result ResultInvalidNcaMetaDataHashDataHash{ErrorModule::FS, 4549};
constexpr Result ResultUnexpectedInCompressedStorageA{ErrorModule::FS, 5324};
constexpr Result ResultUnexpectedInCompressedStorageB{ErrorModule::FS, 5325};
constexpr Result ResultUnexpectedInCompressedStorageC{ErrorModule::FS, 5326};
constexpr Result ResultUnexpectedInCompressedStorageD{ErrorModule::FS, 5327};
constexpr Result ResultUnexpectedInPathA{ErrorModule::FS, 5328};
constexpr Result ResultInvalidArgument{ErrorModule::FS, 6001};
constexpr Result ResultInvalidPath{ErrorModule::FS, 6002};
constexpr Result ResultTooLongPath{ErrorModule::FS, 6003};
constexpr Result ResultInvalidCharacter{ErrorModule::FS, 6004};
constexpr Result ResultInvalidPathFormat{ErrorModule::FS, 6005};
constexpr Result ResultDirectoryUnobtainable{ErrorModule::FS, 6006};
constexpr Result ResultNotNormalized{ErrorModule::FS, 6007};
constexpr Result ResultInvalidOffset{ErrorModule::FS, 6061};
constexpr Result ResultInvalidSize{ErrorModule::FS, 6062};
constexpr Result ResultNullptrArgument{ErrorModule::FS, 6063};
constexpr Result ResultInvalidOpenMode{ErrorModule::FS, 6072};
constexpr Result ResultFileExtensionWithoutOpenModeAllowAppend{ErrorModule::FS, 6201};
constexpr Result ResultReadNotPermitted{ErrorModule::FS, 6202};
constexpr Result ResultWriteNotPermitted{ErrorModule::FS, 6203};
constexpr Result ResultUnsupportedSetSizeForIndirectStorage{ErrorModule::FS, 6325};
constexpr Result ResultUnsupportedWriteForCompressedStorage{ErrorModule::FS, 6387};
constexpr Result ResultUnsupportedOperateRangeForCompressedStorage{ErrorModule::FS, 6388};
constexpr Result ResultPermissionDenied{ErrorModule::FS, 6400};
constexpr Result ResultBufferAllocationFailed{ErrorModule::FS, 6705};

} // namespace FileSys

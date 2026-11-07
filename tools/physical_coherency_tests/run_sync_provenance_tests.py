"""Actual synchronization journal and Scheduler bodies; Vulkan/device execution is simulated."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--vulkan-include', type=Path)
p.add_argument('--positive-only', action='store_true', help='Iterative fixture check only; excludes mutation acceptance')
p.add_argument('--content', action='store_true', help='Also validate exact transfer content and copy provenance')
p.add_argument('--output-dir',type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-sync-provenance').resolve()
stub = out / 'stubs/video_core/vulkan_common'
stub.mkdir(parents=True, exist_ok=True)
(stub / 'vulkan_wrapper.h').write_text('#pragma once\n// Fixture dispatch types precede payload.\n')
inputs = set()


def read(path):
    inputs.add(path)
    return (root / path).read_text()


def body(path, marker):
    text = read(path)
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


header = 'src/video_core/renderer_vulkan/vk_scheduler.h'
cpp = 'src/video_core/renderer_vulkan/vk_scheduler.cpp'
checked = body(header, '    class CheckedSubmission {') + ';\nusing Submission=std::shared_ptr<CheckedSubmission>;\n'
checked += body(header, '    struct TransferBuffer {') + ';\n'
checked += body(header, '    class CheckedTransfer {') + ';\nusing Transfer=std::shared_ptr<CheckedTransfer>;\n'
checked += body(header, '    struct ReplicaCopyRequest {') + ';\n'
checked += body(header, '    struct ReplicaCopyRegion {') + ';\n'
checked += body(header, '    struct ReplicaCopiesRequest {') + ';\n'
checked += body(header, '    struct ReconciliationReplica {') + ';\n'
checked += body(header, '    struct ReconciliationRequest {') + ';\n'
checked += 'enum class WriteState { Pending, Submitted, Skipped, Complete, Unknown };\n'
checked += body(header,'    class PrivateProducerDomain {')+';\n'
checked += body(header, '    class CheckedWrite {') + ';\nusing Write=std::shared_ptr<CheckedWrite>;\n'
checked += body(header, '    struct WriteFootprint {') + ';\nusing ObservedWrites=std::shared_ptr<const std::vector<Write>>;\n'
methods = '\n'.join(body(cpp, marker) for marker in [
    'u64 Scheduler::Flush(',
    'Scheduler::Submission Scheduler::FlushChecked(', 'VkResult Scheduler::PollChecked(',
    'u64 Scheduler::SubmitExecution(', 'Scheduler::Write Scheduler::RecordCheckedWrite(',
    'Scheduler::ObservedWrites Scheduler::RecordObservedWrites(',
    'const VideoCommon::PhysicalWriteIdentity* Scheduler::CompletedPhysicalIdentity(',
    'bool Scheduler::HasConfirmedMemoryDependency(', 'void Scheduler::RecordProvenanceBarrier(',
    'Scheduler::WriteState Scheduler::PollCheckedWrite(',
    'Scheduler::Transfer Scheduler::RecordTransferBatch(',
    'Scheduler::Transfer Scheduler::RecordTransferBatchImpl(', 'bool Scheduler::CompletedReplicaCopyHasRoot(', 'bool Scheduler::CompletedReplicaCopy(',
    'bool Scheduler::PrivateProducerDomain::Initialize(', 'bool Scheduler::PrivateProducerDomain::Fill(',
    'bool Scheduler::PrivateProducerDomain::CpuWrite(', 'Scheduler::WriteState Scheduler::PrivateProducerDomain::Poll(',
    'bool Scheduler::PrivateProducerDomain::Available(',
    'Scheduler::WriteState Scheduler::PollCheckedTransfer(',
    'bool Scheduler::InvalidatePhysicalWrite(', 'void Scheduler::RevokeBufferContent(', 'void Scheduler::RecordBufferFill(', 'void Scheduler::RecordBufferCopy(',
    'const TransferContent* Scheduler::CompletedTransferContent(',
    'bool Scheduler::RetireCheckedWritesAfterDeviceIdle('])
master = 'src/video_core/renderer_vulkan/vk_master_semaphore.cpp'
methods += '\n' + body(master, 'static constexpr std::array<VkPipelineStageFlags, 2> master_wait_stage_masks') + ';\n'
methods += body(master, 'VkResult MasterSemaphore::SubmitQueueTimeline(')
runtime = 'src/video_core/renderer_vulkan/vk_buffer_cache.cpp'
bindings = '\n'.join(body(runtime, marker) for marker in [
    'bool BufferCacheRuntime::SupportsControlledTransfers(',
    'std::shared_ptr<Scheduler::PrivateProducerDomain> BufferCacheRuntime::CreatePrivateProducerDomain(',
    'void BufferCacheRuntime::CapturePhysicalOrigin(',
    'std::shared_ptr<const VideoCommon::PhysicalWriteIdentity>\nBufferCacheRuntime::PhysicalIdentityForBinding('])
template = read('tools/physical_coherency_tests/first_registration_pipeline.cpp.in')
scaffold = template[template.index('struct Allocation {'):template.index('// @TRANSFER_METHODS@')]
for key, value in [('CHECKED_SUBMISSION', checked), ('SUBMISSION_METHODS', methods),
                   ('POLL_TIMELINE', body('src/video_core/renderer_vulkan/vk_master_semaphore.h',
                                          '    [[nodiscard]] VkResult PollTimeline(')),
                   ('RAW_STORAGE_BIND', '')]:
    scaffold = scaffold.replace('// @' + key + '@', value)
scaffold = scaffold.replace('// @TRANSFER_MAPPING_GATE@', 'template <typename Memory>\n' + body(header, '    Transfer RecordControlledPhysicalFill(') + '\ntemplate <typename Memory>\n' +
                           body(header, '    const TransferContent* TransferContentForMapping(') +
                           '\ntemplate <typename Memory>\n' + body(header, '    const TransferContent* CurrentTransferContent(') +
                           '\ntemplate <typename Memory>\n' + body(header, '    const TransferContent* CurrentTransferContentRange(') +
                           '\ntemplate <typename Memory>\n' + body(header, '    bool CurrentTransferHasRoot(') +
                           '\ntemplate <typename Memory>\n' + body(header, '    BoundedReconciliationPlanner::Result PlanControlledReconciliation(') +
                           '\ntemplate <typename Memory>\n' + body(header, '    Transfer RecordControlledReplicaCopies(') + '\ntemplate <typename Memory>\n' + body(header, '    Transfer RecordControlledReplicaCopy(') +
                           '\ntemplate <typename Memory>\n' + body(header, '    bool CurrentReplicaCopyHasRoot('))
scaffold = scaffold.replace('// @SCHEDULER_RECORDING@', '\n'.join([
    'template <typename T>\n' + body(header, marker) for marker in [
        '    void RecordWithUploadBuffer(', '    void EnqueueCommand(', '    void Record(T&& c)']]) +
    '\n' + body(header, '    void RevokeTransferContentEligibility('))
scaffold = scaffold.replace('// @REVOKE_BUFFER_CONTENT@', body(
    'src/video_core/renderer_vulkan/vk_buffer_cache.h', '    void RevokeContentEligibility('))
scaffold = scaffold.replace('// @BUFFER_MAPPED@', body('src/video_core/renderer_vulkan/vk_buffer_cache.h',
    '    [[nodiscard]] std::span<u8> Mapped('))
source = '''#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>
#include "common/common_types.h"
#include "video_core/buffer_cache/controlled_transfer.h"
#include "video_core/renderer_vulkan/vk_sync_provenance.h"
#include "video_core/renderer_vulkan/vk_transfer_content.h"
#include "video_core/renderer_vulkan/vk_transfer_eligibility.h"
#include "video_core/renderer_vulkan/vk_replica_copy_provenance.h"
#include "video_core/renderer_vulkan/vk_private_producer_guard.h"
''' + scaffold + bindings + '''
struct StorageObserverHarness {
    std::shared_ptr<VideoCommon::PhysicalWriteCoverage> storage_write_coverage =
        std::make_shared<VideoCommon::PhysicalWriteCoverage>();
    StorageObserverHarness(Scheduler&, BufferCacheRuntime& runtime) {
        runtime.SetPhysicalObservationMemory(&runtime.test_mapping);
    }
};
} // namespace Vulkan
constexpr u64 A = 65536;
struct Scenario {
    Driver gpu;
    Vulkan::Device device;
    Vulkan::Scheduler scheduler{device};
    Vulkan::BufferCacheRuntime runtime{device, scheduler};
    Scenario() {driver = &gpu;}
};
''' + read('tools/physical_coherency_tests/sync_dependency_fixture.cpp.in')
source += read('tools/physical_coherency_tests/private_producer_test_access.inc')
if a.content:
    source += read('tools/physical_coherency_tests/transfer_content_fixture.cpp.in')
    source += read('tools/physical_coherency_tests/replica_copy_fixture.cpp.in')
    source += read('tools/physical_coherency_tests/mixed_replica_fixture.cpp.in')
    source += read('tools/physical_coherency_tests/destination_replica_fixture.cpp.in')
    source += read('tools/physical_coherency_tests/combined_replica_fixture.cpp.in')
    source += read('tools/physical_coherency_tests/multi_region_fixture.cpp.in')
    source += read('tools/physical_coherency_tests/mixed_holes_fixture.cpp.in')
    source += read('tools/physical_coherency_tests/reconciliation_planner_fixture.cpp.in')
    source += read('tools/physical_coherency_tests/physical_alias_invalidation_fixture.cpp.in')
    source += read('tools/physical_coherency_tests/private_producer_fixture.cpp.in')
source += '\nint main() {TestSyncDependencies();' + ('TestTransferContent();TestReplicaCopy();TestReplicaChain();TestMixedReplicaCopy();TestDestinationReplicaCopy();TestCombinedReplicaCopy();TestMultiRegionReplicaCopy();TestMixedInternalHoles();TestReconciliationPlanner();TestPhysicalAliasInvalidation();TestPrivateProducerDomain();' if a.content else '') + '}\n'
command = [a.cxx, '-std=c++23', '-O1', '-Wall', '-Wextra', '-Werror',
           '-pthread',
           '-Wno-unused-parameter', '-Wno-missing-field-initializers',
           '-I', str(out / 'stubs'), '-I', str(root / 'src'),
           '-I', str(a.vulkan_include or root / 'externals/Vulkan-Headers/include')]


def run(name, text):
    path = out / (name + '.cpp')
    path.write_text(text)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(command + [str(path), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    (out / (name + '.txt')).write_text(result.stdout + result.stderr)
    return result


result = run('dependencies', source)
print(result.stdout, end='')
if result.returncode:
    raise RuntimeError(result.stderr)
if a.positive_only:
    raise SystemExit(0)
if a.content and sys.platform.startswith('linux'):
    for name,guard,replacement in [
        ('private-guard-not-retained','transfer->exclusion=std::move(exclusion);','transfer->exclusion.reset();'),
        ('private-completes-while-pending','if (transfer->emission.load(std::memory_order_acquire) == CheckedTransfer::Emission::Pending)\n        return WriteState::Pending;',
         'if (transfer->emission.load(std::memory_order_acquire) == CheckedTransfer::Emission::Pending) { if(transfer->exclusion) (void)transfer->exclusion->CompleteChecked(); return WriteState::Pending; }'),
        ('private-without-worker-recheck','if (transfer->exclusion && !transfer->exclusion->Ready())','if (false)'),
        ('private-without-physical-fanout','if(!scheduler->InvalidatePhysicalWrite(*previous->resources[replica].physical,offset,size)) return false;','')]:
        assert source.count(guard)==1,name
        result=run(name,source.replace(guard,replacement))
        if result.returncode==0 or 'Assertion' not in result.stderr:
            raise RuntimeError(f'{name} failed incorrectly: {result.returncode} {result.stderr}')
        print('PASS expected private provider mutation assertion:',name)
if a.content:
    for name, guard, replacement in [
        ('fanout-without-physical-revocation', 'if (!InvalidatePhysicalWrite(*physical, offset, size)) return {};', ''),
        ('fanout-after-recording', 'if (!InvalidatePhysicalWrite(*physical, offset, size)) return {};', ''),
        ('fanout-native-only', 'if (physical->Backing() != written->Backing()) continue;',
         'if (pin.Identity() != writer.Representation() || physical->Backing() != written->Backing()) continue;'),
        ('fanout-without-backing-identity', 'if (physical->Backing() != written->Backing()) continue;', ''),
        ('fanout-wrong-local-projection', 'target.logical_offset + begin - target.physical_offset, end - begin',
         'begin - source.physical_offset, end - begin'),
        ('fanout-missing-page-projection', 'target.logical_offset + begin - target.physical_offset, end - begin',
         'begin - target.physical_offset, end - begin'),
        ('fanout-whole-pin', 'target.logical_offset + begin - target.physical_offset, end - begin', '0, pin.Size()'),
        ('fanout-whole-writer', 'const auto written = writer.Slice(offset, size);',
         'const auto written = writer.Slice(0, writer.Size()); (void)offset; (void)size;'),
        ('fanout-stale-writer', 'if (!written || !writer.CapturedEpochCurrent())', 'if (!written)')]:
        assert source.count(guard)==1,name
        changed=source.replace(guard,replacement)
        if name=='fanout-after-recording':
            changed=changed.replace('return RecordTransferBatch(buffers, commands);',
                'const auto result = RecordTransferBatch(buffers, commands);\n        (void)InvalidatePhysicalWrite(*physical, offset, size); return result;')
        result=run(name,changed)
        if result.returncode==0 or 'Assertion' not in result.stderr:
            raise RuntimeError(f'{name} failed incorrectly: {result.returncode} {result.stderr}')
        print('PASS expected physical fan-out mutation assertion:',name)
for name, guard, replacement in [
    ('missing-native-identity', 'a->Representation() != b->Representation()', 'false'),
    ('missing-range-coverage', 'if (!advance) return false;', 'if (!advance) break;'),
    ('missing-submission-boundary', 'before->submission != after->submission', 'false'),
    ('missing-barrier-record', 'if (submission) submission->synchronization.RecordBarrier(source, destination, memory);', '(void)submission;'),
    ('missing-actual-barrier', 'cmdbuf.PipelineBarrier(source, destination, 0, memory);', '(void)cmdbuf;')]:
    assert source.count(guard) == 1, name
    result = run(name, source.replace(guard, replacement))
    if result.returncode == 0 or 'Assertion' not in result.stderr:
        raise RuntimeError(f'{name} failed to expose the removed guard: {result.returncode} {result.stderr}')
    print('PASS expected mutation failure:', name)
if a.content:
    for name, guard, replacement, count in [
        ('planner-without-current-evidence', 'return current ? Reason::None : Reason::CurrentUnavailable;', 'return (static_cast<void>(current), Reason::None);', 1),
        ('planner-without-mapping-reason', 'if (!resource.physical->MatchesCurrent(memory)) return Reason::Remapped;', '(void)resource;', 1),
        ('planner-without-sync-evidence', 'if (replica.receipt ? !CompletedReplicaCopy(*receipt,parent) : !CompletedTransferContent(parent))', 'if (false)', 1),
        ('planner-query-submits', 'return Planner::Build({parent->content,request.scope,request.destination,replicas},evidence);', '(void)Flush(); return Planner::Build({parent->content,request.scope,request.destination,replicas},evidence);', 1),
        ('batch-without-current-qualification', 'if (!qualify()) return {};', 'if ((static_cast<void>(qualify), false)) return {};', 2),
        ('batch-without-planned-revocation', 'RevokeBufferContent(destination.Handle(), destination.Offset() + operation.destination_offset, operation.size);', '(void)destination;', 1),
        ('batch-revokes-internal-holes', 'RevokeBufferContent(destination.Handle(), destination.Offset() + operation.destination_offset, operation.size);', 'if (transfer->replica_copy && transfer->replica_copy->IsBatch()) { const auto r=transfer->replica_copy->Regions(); RevokeBufferContent(destination.Handle(), destination.Offset()+r.front().destination_offset,r.back().destination_offset+r.back().size-r.front().destination_offset); } else RevokeBufferContent(destination.Handle(), destination.Offset() + operation.destination_offset, operation.size);', 1),
        ('batch-keeps-unrecorded-suffix', 'RevokeBufferContent(destination.Handle(), destination.Offset() + operation.destination_offset, operation.size);', 'if (operation.destination_offset == transfer->content->Commands().front().destination_offset) RevokeBufferContent(destination.Handle(), destination.Offset() + operation.destination_offset, operation.size);', 1),
        ('batch-without-hole-current-gate', 'for (const auto& part : plan->DestinationRoots())\n                    if (', 'for (const auto& part : plan->DestinationRoots())\n                    if (part.offset >= request.destination_offset && part.offset < request.destination_offset + request.size) continue; else if (', 1),
        ('batch-checks-first-hole-only', 'for (const auto& part : plan->DestinationRoots())\n                    if (', 'for (const auto& part : plan->DestinationRoots())\n                    if (part.offset > request.destination_offset + request.size / 2 && part.offset < request.destination_offset + request.size) continue; else if (', 1),
        ('batch-checks-first-complement-piece-only', 'for (const auto& part : plan->DestinationRoots())\n                    if (', 'for (const auto& part : plan->DestinationRoots())\n                    if (&part != plan->DestinationRoots().data() && (&part-1)->offset + (&part-1)->size == part.offset) continue; else if (', 1),
        ('batch-keeps-first-write', 'RevokeBufferContent(destination.Handle(), destination.Offset() + operation.destination_offset, operation.size);', 'if (operation.destination_offset != transfer->content->Commands().front().destination_offset) RevokeBufferContent(destination.Handle(), destination.Offset() + operation.destination_offset, operation.size);', 1),
        ('batch-records-only-prefix', 'transfer->positions[index++] = journal.RecordAccess();', 'transfer->positions[index++] = journal.RecordAccess(); if (transfer->replica_copy && transfer->replica_copy->IsBatch()) break;', 1),
        ('target-current-prefix-only', 'for (const auto& part : plan->DestinationRoots()) {', 'for (const auto& part : plan->DestinationRoots()) {\n                        if (&part != plan->DestinationRoots().data()) break;', 1),
        ('target-without-completed-history', '!CompletedReplicaCopy(destination_receipt, parent)', 'false', 1),
        ('target-without-history-identity', 'link->replica_copy->DestinationCopy() != (link->replica_destination ? link->replica_destination->replica_copy : nullptr)', 'false', 1),
        ('chain-without-ancestor-evidence', 'const auto& consumer = link->submission;', 'if (link != result) continue;\n    const auto& consumer = link->submission;', 1),
        ('chain-without-ancestor-storage', 'for (const auto& resource : link->resources)', 'if (link != result) continue;\n            for (const auto& resource : link->resources)', 1),
        ('chain-source-uses-history', 'CurrentReplicaCopyHasRoot(receipt, memory, parent, part.offset, part.size,\n                                                          part.fill, part.root_offset)', 'receipt->replica_copy->HasRoot(part.offset, part.size, part.fill, part.root_offset)', 1),
        ('mixed-current-prefix-only', 'for (const auto& part : plan->SourceRoots()) {', 'for (const auto& part : plan->SourceRoots()) {\n                if (&part != plan->SourceRoots().data()) break;', 1),
        ('replica-without-entry-sync', '!consumer->synchronization.ProvesEarlierSubmission(prior, link->positions[0], copy)', '(static_cast<void>(prior), false)', 1),
        ('replica-without-release-sync', '!consumer->synchronization.ProvesRelease(link->positions[0], copy, later)', '(static_cast<void>(later), false)', 1),
        ('replica-without-queue-link', 'consumer->queue_predecessor.lock() != producer', 'false', 1),
        ('replica-without-queue-identity', 'consumer->queue != producer->queue || consumer->queue != device.GetGraphicsQueue().Handle()', 'false', 1),
        ('replica-without-physical-alias', '!std::ranges::equal(source_physical->Segments(), destination_physical->Segments())', 'false', 1),
        ('replica-without-host-exclusion', '!source.pin.CopyProvenanceEligible() || !destination.pin.CopyProvenanceEligible()', 'false', 1),
        ('replica-without-worker-epoch', '!resource.physical->CapturedEpochCurrent()', 'false', 1),
        ('replica-without-release-command', 'cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, release);', '(void)release;', 1),
        ('replica-without-copy-coupling', '!replica_copy->MatchesCopy(commands[0].source_offset, commands[0].destination_offset, commands[0].size)', 'false', 1),
        ('replica-without-buffer-coupling', '!matches(0, replica_copy->SourceBuffer()) || !matches(1, replica_copy->DestinationBuffer())', '(static_cast<void>(matches), false)', 1),
        ('replica-without-completion', 'PollCheckedTransfer(result) != WriteState::Complete', 'false', 1),
        ('replica-without-current-interval', '!CurrentTransferContentRange(result, memory, result->TargetIndex(), offset, size)', 'false', 1),
        ('replica-without-exact-root', 'return result->replica_copy->HasRoot(offset, size, fill, root_offset);', 'return true;', 1),
        ('replica-without-current-source', '!current_source()', '(static_cast<void>(current_source), false)', 2),
        ('replica-without-current-target', '!current_destination()', '(static_cast<void>(current_destination), false)', 2)]:
        assert source.count(guard) == count, name
        result = run(name, source.replace(guard, replacement))
        if result.returncode == 0 or 'Assertion' not in result.stderr:
            raise RuntimeError(f'{name} failed incorrectly: {result.returncode} {result.stderr}')
        print('PASS expected replica mutation failure:', name)
    for name, guard, replacement in [
        ('current-root-without-current-interval', 'const auto* content = CurrentTransferContentRange(transfer, memory, buffer, offset, size);', 'const auto* content = transfer ? transfer->PlannedContent().get() : nullptr;'),
        ('current-root-without-exact-root', 'content->HasRoot(static_cast<TransferContent::Index>(buffer), offset, size, fill, root_offset)', 'true'),
        ('range-without-overlap-gate', '!transfer->current_ranges[buffer].Contains(offset, size)', 'false'),
        ('range-without-native-allocation', 'if (pin.Handle() != destination) continue;', 'if (false) continue;'),
        ('range-without-pin-offset', 'Revoke(begin - pin.Offset(), end - begin)', 'Revoke(begin, end - begin)'),
        ('range-without-submit-failure-gate', 'if (current_content_uncertain.load(std::memory_order_acquire)) return nullptr;', '/* missing failure-domain gate */'),
        ('typed-fill-without-command', 'cmdbuf.FillBuffer(destination, offset, size, value);', '(void)value;'),
        ('typed-copy-without-command', 'cmdbuf.CopyBuffer(source, destination, recorded_regions);', '(void)recorded_regions;'),
        ('current-without-record-revocation', 'RevokeTransferContentEligibility();\n        EnqueueCommand', '/* missing cancellation */\n        EnqueueCommand'),
        ('current-without-eligibility-gate', 'if (!transfer || !transfer->current_eligible) return nullptr;', 'if (!transfer) return nullptr;'),
        ('current-without-retirement-gate', 'return !resource.pin.ContentEligible();', 'return false;'),
        ('content-without-completion', 'if (PollCheckedTransfer(transfer) != WriteState::Complete) return nullptr;', 'if (false) return nullptr;'),
        ('content-without-mapping-check', 'if (!origin->MatchesCurrent(memory)) return nullptr;', '(void)origin;'),
        ('copy-without-device-command', 'cmdbuf.CopyBuffer(source.Handle(), destination.Handle(), region);', '(void)region;'),
        ('fill-without-device-command', 'cmdbuf.FillBuffer(destination.Handle(), destination.Offset() + operation.destination_offset,\n                                          operation.size, operation.value);', '(void)destination;'),
        ('content-without-sync-evidence', 'transfer->positions[i], next)) return nullptr;', 'transfer->positions[i], next)) {}'),
        ('content-without-host-retention',
         'if (!transfer->submission ||\n            transfer->worker_pending.load(std::memory_order_acquire) ||\n            !transfer->submission->queue_attempted.load(std::memory_order_acquire) ||\n            transfer->submission->result.load(std::memory_order_acquire) == VK_NOT_READY) return false;',
         '(void)transfer;')]:
        assert source.count(guard) == 1, name
        result = run(name, source.replace(guard, replacement))
        if result.returncode == 0 or 'Assertion' not in result.stderr:
            raise RuntimeError(f'{name} failed incorrectly: {result.returncode} {result.stderr}')
        print('PASS expected content mutation failure:', name)
    header = read('src/video_core/renderer_vulkan/vk_transfer_content.h').replace('#pragma once', '')
    expanded = source.replace('#include "video_core/renderer_vulkan/vk_transfer_content.h"', header)
    replica_header = read('src/video_core/renderer_vulkan/vk_replica_copy_provenance.h').replace('#pragma once', '').replace(
        '#include "video_core/renderer_vulkan/vk_transfer_content.h"', '')
    expanded = expanded.replace('#include "video_core/renderer_vulkan/vk_replica_copy_provenance.h"', replica_header)
    planner_header = read('src/video_core/renderer_vulkan/vk_reconciliation_planner.h').replace('#pragma once', '').replace(
        '#include "video_core/renderer_vulkan/vk_replica_copy_provenance.h"', '')
    expanded = expanded.replace('#include "video_core/renderer_vulkan/vk_reconciliation_planner.h"', planner_header)
    for name, guard, replacement in [
        ('combined-source-flattens-projection', 'source_copy->HasRoot(parent_offset, end - begin, fill, root_offset + begin - offset)', 'source_copy->HasRoot(parent_offset, end - begin, fill, parent_offset)'),
        ('batch-without-complement-coverage', 'if (retained != extent) return {};', ''),
        ('batch-without-window-tail', 'cursor != begin + size || ', ''),
        ('batch-without-complement-root-proof', '!(destination_copy ? destination_copy->HasRoot(part.offset,part.size,part.fill,part.root_offset)\n                    : parent->HasRoot(destination,part.offset,part.size,part.fill,part.root_offset))', 'false'),
        ('batch-flattens-root-projection', 'parent->HasRoot(region.source,projection,end-begin,fill,root_offset+begin-offset)', 'parent->HasRoot(region.source,projection,end-begin,fill,projection)'),
        ('combined-complement-uses-anchor', ': !copied && destination_copy', ': false && destination_copy'),
        ('target-without-complement-tail', 'if (preserved != destination_size) return {};', '/* missing complement suffix */'),
        ('target-without-complement-order', 'part.offset != preserved || ', ''),
        ('target-with-covered-complement', '(part.offset < destination_offset + size && destination_offset < part.offset + part.size) ||', ''),
        ('target-without-owned-complements', 'std::copy(complements_.begin(), complements_.end(), destination_roots.begin());', '(void)complements_;'),
        ('target-without-hop-limit', 'destination_copy->Depth() >= MaxHops || ', ''),
        ('mixed-without-tail-coverage', 'if (cursor != source_offset + size) return {};', '/* unchecked suffix */'),
        ('mixed-without-ordered-partition', 'part.offset != cursor || ', ''),
        ('mixed-without-historical-roots', '!(source_copy ? source_copy->HasRoot(part.offset, part.size, part.fill, part.root_offset)\n                              : parent->HasRoot(source, part.offset, part.size, part.fill, part.root_offset))', 'false'),
        ('mixed-without-owned-declarations', 'std::copy(roots_.begin(), roots_.end(), source_roots.begin());', '(void)roots_;'),
        ('chain-without-hop-limit', 'source_copy->Depth() >= MaxHops || ', ''),
        ('stale-whole-replica-promotion', 'if (!root || root->command != fill || root->offset != root_offset + begin - offset) return false;', 'if (!root) return false;'),
        ('missing-left-complement', 'if (part.offset < offset) {', 'if (false) {'),
        ('missing-right-complement', 'if (begin < part.offset + part.size) {', 'if (false) {')]:
        assert expanded.count(guard) == 1, name
        result = run(name, expanded.replace(guard, replacement))
        if result.returncode == 0 or 'Assertion' not in result.stderr:
            raise RuntimeError(f'{name} failed incorrectly: {result.returncode} {result.stderr}')
        print('PASS expected content mutation failure:', name)
    eligibility_header = read('src/video_core/renderer_vulkan/vk_transfer_eligibility.h').replace('#pragma once', '')
    expanded_eligibility = source.replace('#include "video_core/renderer_vulkan/vk_transfer_eligibility.h"', eligibility_header)
    for name, guard, replacement in [
        ('eligibility-without-left-complement', 'if (span.begin < offset && !append({span.begin, offset}))', 'if (false)'),
        ('eligibility-without-right-complement', 'if (end < span.end && !append({end, span.end}))', 'if (false)')]:
        assert expanded_eligibility.count(guard) == 1, name
        result = run(name, expanded_eligibility.replace(guard, replacement))
        if result.returncode == 0 or 'Assertion' not in result.stderr:
            raise RuntimeError(f'{name} failed incorrectly: {result.returncode} {result.stderr}')
        print('PASS expected eligibility mutation failure:', name)
inputs.add('src/video_core/renderer_vulkan/vk_reconciliation_planner.h')
inputs.add('src/video_core/renderer_vulkan/vk_private_producer_guard.h')
inputs.add('src/video_core/renderer_vulkan/vk_sync_provenance.h')
inputs.add('src/video_core/renderer_vulkan/vk_transfer_content.h')
inputs.add('src/video_core/renderer_vulkan/vk_transfer_eligibility.h')
inputs.update(['src/video_core/renderer_vulkan/vk_replica_copy_provenance.h',
    'src/video_core/renderer_vulkan/vk_buffer_payload.h', 'src/video_core/buffer_cache/physical_write_identity.h',
    'src/video_core/vulkan_common/vulkan_wrapper.h'])
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sorted(inputs)},
    'boundary': 'Actual production dependency and submission methods, simulated Vulkan/device execution; no authority',
}, indent=2))

"""Actual synchronization journal and Scheduler bodies; Vulkan/device execution is simulated."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--vulkan-include', type=Path)
p.add_argument('--content', action='store_true', help='Also validate exact transfer content and copy provenance')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m4-sync-provenance'
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
checked += 'enum class WriteState { Pending, Submitted, Skipped, Complete, Unknown };\n'
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
    'Scheduler::Transfer Scheduler::RecordTransferBatchImpl(', 'bool Scheduler::CompletedReplicaCopyHasRoot(',
    'Scheduler::WriteState Scheduler::PollCheckedTransfer(',
    'void Scheduler::RevokeBufferContent(', 'void Scheduler::RecordBufferFill(', 'void Scheduler::RecordBufferCopy(',
    'const TransferContent* Scheduler::CompletedTransferContent(',
    'void Scheduler::RetireCheckedWritesAfterDeviceIdle('])
master = 'src/video_core/renderer_vulkan/vk_master_semaphore.cpp'
methods += '\n' + body(master, 'static constexpr std::array<VkPipelineStageFlags, 2> master_wait_stage_masks') + ';\n'
methods += body(master, 'VkResult MasterSemaphore::SubmitQueueTimeline(')
runtime = 'src/video_core/renderer_vulkan/vk_buffer_cache.cpp'
bindings = '\n'.join(body(runtime, marker) for marker in [
    'void BufferCacheRuntime::CapturePhysicalOrigin(',
    'std::shared_ptr<const VideoCommon::PhysicalWriteIdentity>\nBufferCacheRuntime::PhysicalIdentityForBinding('])
template = read('tools/physical_coherency_tests/first_registration_pipeline.cpp.in')
scaffold = template[template.index('struct Allocation {'):template.index('// @TRANSFER_METHODS@')]
for key, value in [('CHECKED_SUBMISSION', checked), ('SUBMISSION_METHODS', methods),
                   ('POLL_TIMELINE', body('src/video_core/renderer_vulkan/vk_master_semaphore.h',
                                          '    [[nodiscard]] VkResult PollTimeline(')),
                   ('RAW_STORAGE_BIND', '')]:
    scaffold = scaffold.replace('// @' + key + '@', value)
scaffold = scaffold.replace('// @TRANSFER_MAPPING_GATE@', 'template <typename Memory>\n' +
                           body(header, '    const TransferContent* TransferContentForMapping(') +
                           '\ntemplate <typename Memory>\n' + body(header, '    const TransferContent* CurrentTransferContent(') +
                           '\ntemplate <typename Memory>\n' + body(header, '    const TransferContent* CurrentTransferContentRange(') +
                           '\ntemplate <typename Memory>\n' + body(header, '    bool CurrentTransferHasRoot(') +
                           '\ntemplate <typename Memory>\n' + body(header, '    Transfer RecordControlledReplicaCopy(') +
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
if a.content:
    source += read('tools/physical_coherency_tests/transfer_content_fixture.cpp.in')
    source += read('tools/physical_coherency_tests/replica_copy_fixture.cpp.in')
source += '\nint main() {TestSyncDependencies();' + ('TestTransferContent();TestReplicaCopy();TestReplicaChain();' if a.content else '') + '}\n'
command = [a.cxx, '-std=c++23', '-O1', '-Wall', '-Wextra', '-Werror',
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
        ('chain-without-ancestor-evidence', 'const auto& consumer = link->submission;', 'if (link != result) continue;\n    const auto& consumer = link->submission;', 1),
        ('chain-without-ancestor-storage', 'for (const auto& resource : link->resources)', 'if (link != result) continue;\n            for (const auto& resource : link->resources)', 1),
        ('chain-source-uses-history', 'CurrentReplicaCopyHasRoot(receipt, memory, parent, request.source_offset, request.size,\n                                                       request.source_fill, request.source_root_offset)', 'receipt->replica_copy->HasRoot(request.source_offset, request.size, request.source_fill, request.source_root_offset)', 1),
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
        ('replica-without-current-interval', '!CurrentTransferContentRange(result, memory, 1, offset, size)', 'false', 1),
        ('replica-without-exact-root', 'return result->replica_copy->HasRoot(offset, size, fill, root_offset);', 'return true;', 1),
        ('replica-without-current-source', '!current_source()', '(static_cast<void>(current_source), false)', 2),
        ('replica-without-current-target', '!CurrentTransferHasRoot(parent, memory, request.destination, 0, destination.pin.Size(),\n                                    request.destination_fill, request.destination_root_offset)', 'false', 2)]:
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
         'if (transfer->worker_pending.load(std::memory_order_acquire) ||\n            !transfer->submission->queue_attempted.load(std::memory_order_acquire) ||\n            transfer->submission->result.load(std::memory_order_acquire) == VK_NOT_READY) return;',
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
    for name, guard, replacement in [
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

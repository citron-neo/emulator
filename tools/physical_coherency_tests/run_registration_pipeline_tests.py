# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract actual cache/native-transfer/checked-submit bodies; Vulkan dispatch remains a fixture.

Run run_first_registration_tests.py first. This suite never certifies real driver execution or
ordinary-game F2 repair. Native compilation of the actual Rasterizer endpoint is done by CI.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--boost-include', type=Path)
p.add_argument('--repeat', type=int, default=10)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
previous = root / '.diagnostics/m3b-first-registration'
for name, digest in json.loads((previous / 'extraction.json').read_text())['sha256'].items():
    if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
        raise RuntimeError('Stale prerequisite: rerun run_first_registration_tests.py: ' + name)
out = root / '.diagnostics/m3b-registration-pipeline'
subprocess.run([sys.executable, str(root / 'tools/gpu_lifecycle_tests/run_thread_tests.py'),
                '--cxx', a.cxx, '--repeat', '1', '--handoff-controls'], check=True)
thread_fixture = root / '.diagnostics/gpu-lifecycle' / ('windows' if os.name == 'nt' else 'linux')
thread_source = (thread_fixture / 'gpu_thread.cpp').read_text().split('using VideoCommon::GPUThread::ThreadManager;')[0]
thread_source = thread_source.replace(
    '#define LOG_ERROR(category, format, reason) ((void)(reason))',
    '#undef LOG_ERROR\n#define LOG_ERROR(...) ((void)0)')
thread_source = thread_source.replace('int Acquire() { return 0; }',
    'struct Scope {~Scope() {}}; Scope Acquire() { return {}; }')
start = thread_source.index('namespace Vulkan::vk { struct Exception')
end = thread_source.index('namespace Core {', start)
thread_source = thread_source[:start] + 'namespace Vulkan {namespace vk=::vk;}\n' + thread_source[end:]
stub = out / 'stubs/video_core/vulkan_common'
stub.mkdir(parents=True, exist_ok=True)
(stub / 'vulkan_wrapper.h').write_text('#pragma once\n// Fixture vk types are defined before the actual payload header.\n')
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


cache_path = 'src/video_core/buffer_cache/buffer_cache.h'
base = read('src/video_core/buffer_cache/buffer_cache_base.h')
registration = body('src/video_core/buffer_cache/buffer_cache_base.h', '    class ControlledRegistration {')
registration += ';\n    using ControlledRegistrationPtr=std::shared_ptr<ControlledRegistration>;\n'
registration += body('src/video_core/buffer_cache/buffer_cache_base.h', '    class ControlledCpuWrite {')
registration += ';\n    using ControlledCpuWritePtr=std::shared_ptr<ControlledCpuWrite>;\n'
declarations = read('src/video_core/renderer_vulkan/vk_scheduler.h')
checked = body('src/video_core/renderer_vulkan/vk_scheduler.h', '    class CheckedSubmission {')
checked += ';\n    using Submission=std::shared_ptr<CheckedSubmission>;\n'
checked += body('src/video_core/renderer_vulkan/vk_scheduler.h', '    struct TransferBuffer {') + ';\n'
checked += body('src/video_core/renderer_vulkan/vk_scheduler.h', '    class CheckedTransfer {')
checked += ';\n using Transfer=std::shared_ptr<CheckedTransfer>;\n'
checked += body('src/video_core/renderer_vulkan/vk_scheduler.h', '    struct ReplicaCopyRequest {') + ';\n'
checked += body('src/video_core/renderer_vulkan/vk_scheduler.h', '    struct ReplicaCopyRegion {') + ';\n'
checked += body('src/video_core/renderer_vulkan/vk_scheduler.h', '    struct ReplicaCopiesRequest {') + ';\n'
checked += body('src/video_core/renderer_vulkan/vk_scheduler.h', '    struct ReconciliationReplica {') + ';\n'
checked += body('src/video_core/renderer_vulkan/vk_scheduler.h', '    struct ReconciliationRequest {') + ';\n'
checked += 'enum class WriteState { Pending, Submitted, Skipped, Complete, Unknown };\n'
checked += body('src/video_core/renderer_vulkan/vk_scheduler.h','    class PrivateProducerDomain {')+';\n'
checked += body('src/video_core/renderer_vulkan/vk_scheduler.h', '    class CheckedWrite {')
checked += ';\n    using Write=std::shared_ptr<CheckedWrite>;\n'
checked += body('src/video_core/renderer_vulkan/vk_scheduler.h', '    struct WriteFootprint {')
checked += ';\n using ObservedWrites=std::shared_ptr<const std::vector<Write>>;\n'
timeline = body('src/video_core/renderer_vulkan/vk_master_semaphore.h',
                '    [[nodiscard]] VkResult PollTimeline(')
scheduler_path = 'src/video_core/renderer_vulkan/vk_scheduler.cpp'
submission = body('src/video_core/renderer_vulkan/vk_master_semaphore.cpp',
                  'static constexpr std::array<VkPipelineStageFlags, 2> master_wait_stage_masks') + ';\n'
submission += body('src/video_core/renderer_vulkan/vk_master_semaphore.cpp',
                   'VkResult MasterSemaphore::SubmitQueueTimeline(') + '\n'
for marker in ['Scheduler::Submission Scheduler::FlushChecked(',
               'u64 Scheduler::Flush(',
               'VkResult Scheduler::PollChecked(', 'u64 Scheduler::SubmitExecution(',
               'Scheduler::Write Scheduler::RecordCheckedWrite(',
               'Scheduler::ObservedWrites Scheduler::RecordObservedWrites(',
               'const VideoCommon::PhysicalWriteIdentity* Scheduler::CompletedPhysicalIdentity(',
               'bool Scheduler::HasConfirmedMemoryDependency(',
               'void Scheduler::RecordProvenanceBarrier(',
               'Scheduler::WriteState Scheduler::PollCheckedWrite(',
               'Scheduler::Transfer Scheduler::RecordTransferBatch(',
               'Scheduler::Transfer Scheduler::RecordTransferBatchImpl(', 'bool Scheduler::CompletedReplicaCopyHasRoot(', 'bool Scheduler::CompletedReplicaCopy(',
               'bool Scheduler::PrivateProducerDomain::Initialize(', 'bool Scheduler::PrivateProducerDomain::Fill(',
               'bool Scheduler::PrivateProducerDomain::CpuWrite(', 'Scheduler::WriteState Scheduler::PrivateProducerDomain::Poll(',
               'bool Scheduler::PrivateProducerDomain::Available(',
               'Scheduler::WriteState Scheduler::PollCheckedTransfer(',
               'bool Scheduler::InvalidatePhysicalWrite(', 'void Scheduler::RevokeBufferContent(', 'void Scheduler::RecordBufferFill(', 'void Scheduler::RecordBufferCopy(',
               'const TransferContent* Scheduler::CompletedTransferContent(',
               'bool Scheduler::RetireCheckedWritesAfterDeviceIdle(']:
    submission += body(scheduler_path, marker) + '\n'
runtime_path = 'src/video_core/renderer_vulkan/vk_buffer_cache.cpp'
transfer = body(runtime_path, 'VkBufferCopy MakeBufferCopy(') + '\n'
transfer += body(runtime_path, 'class ControlledTransfer final') + ';\n'
for marker in ['void BufferCacheRuntime::BeginStorageWriteObservation(',
               'void BufferCacheRuntime::CapturePhysicalOrigin(',
               'std::shared_ptr<const VideoCommon::PhysicalWriteIdentity>\nBufferCacheRuntime::PhysicalIdentityForBinding(',
               'void BufferCacheRuntime::EndStorageWriteObservation(',
               'void BufferCacheRuntime::BindStorageBuffer(Buffer&']:
    transfer += body(runtime_path, marker) + '\n'
for marker in ['bool BufferCacheRuntime::SupportsControlledTransfers(',
               'std::shared_ptr<Scheduler::PrivateProducerDomain> BufferCacheRuntime::CreatePrivateProducerDomain(',
               'std::shared_ptr<VideoCommon::ControlledBufferTransfer> BufferCacheRuntime::\n    PrepareControlledTransfer(',
               'void BufferCacheRuntime::RetireControlledTransfersAfterDeviceIdle(',
               'void BufferCacheRuntime::CopyBuffer(']:
    transfer += body(runtime_path, marker) + '\n'
cache = ''
for marker in ['auto BufferCache<P>::BeginControlledFirstRegistration(',
               'BufferId BufferCache<P>::ControlledReadyBuffer(',
               'BufferId BufferCache<P>::ControlledBufferId(',
               'bool BufferCache<P>::BeginControlledRefresh(',
               'auto BufferCache<P>::PollControlledRegistration(',
               'Core::Memory::PreparedCpuWriteResult BufferCache<P>::CommitControlledCachedWrite(',
               'auto BufferCache<P>::BeginControlledCpuWrite(',
               'auto BufferCache<P>::BeginControlledCpuWriteRequest(',
               'Core::Memory::PreparedCpuWriteResult BufferCache<P>::CommitControlledCpuWriteRequest(',
               'void BufferCache<P>::AbandonControlledCpuWrite(',
               'auto BufferCache<P>::PollControlledCpuWrite(',
               'Core::Memory::PreparedCpuWriteResult BufferCache<P>::CommitControlledCpuTransaction(',
               'void BufferCache<P>::CheckControlledSynchronization(',
               'bool BufferCache<P>::ControlledReadbacksIdle(',
               'bool BufferCache<P>::SynchronizeBuffer(',
               'void BufferCache<P>::Register(', 'void BufferCache<P>::Unregister(',
               'void BufferCache<P>::ChangeRegister(',
               'BufferCache<P>::CommitControlledCpuWrite(']:
    text = read(cache_path)
    method_start = text.index(marker)
    template_start = text.rfind('template <class P>', 0, method_start)
    # Include the return type if the selected marker begins at the function name.
    cache += text[template_start:method_start] + body(cache_path, marker) + '\n'
old = (previous / 'first_registration.cpp').read_text()
cpu = old[:old.index('using DAddr=u64;')]
tracker = old[old.index('struct DeviceTracker {'):old.index('struct BufferCopy {')]
template = read('tools/physical_coherency_tests/first_registration_pipeline.cpp.in')
template = template.replace('// @SCHEDULER_RECORDING@', '\n'.join([
    'template <typename T>\n' + body('src/video_core/renderer_vulkan/vk_scheduler.h', marker) for marker in [
        '    void RecordWithUploadBuffer(', '    void EnqueueCommand(', '    void Record(T&& c)']]) +
    '\n' + body('src/video_core/renderer_vulkan/vk_scheduler.h', '    void RevokeTransferContentEligibility('))
template = template.replace('// @REVOKE_BUFFER_CONTENT@', body(
    'src/video_core/renderer_vulkan/vk_buffer_cache.h', '    void RevokeContentEligibility('))
template = template.replace('// @BUFFER_MAPPED@', body('src/video_core/renderer_vulkan/vk_buffer_cache.h',
    '    [[nodiscard]] std::span<u8> Mapped('))
template = template.replace('using DAddr=u64;',
    'using VideoCommon::ControlledBufferTransfer;\nusing VideoCommon::ControlledTransferPoll;\nusing DAddr=u64;')
for name, value in [('POLL_TIMELINE', timeline), ('CHECKED_SUBMISSION', checked),
                    ('TRANSFER_MAPPING_GATE', 'template <typename Memory>\n' + body('src/video_core/renderer_vulkan/vk_scheduler.h', '    Transfer RecordControlledPhysicalFill(') + '\ntemplate <typename Memory>\n' + body(
                        'src/video_core/renderer_vulkan/vk_scheduler.h',
                        '    const TransferContent* TransferContentForMapping(') + '\ntemplate <typename Memory>\n' + body(
                        'src/video_core/renderer_vulkan/vk_scheduler.h',
                        '    const TransferContent* CurrentTransferContent(') + '\ntemplate <typename Memory>\n' + body(
                        'src/video_core/renderer_vulkan/vk_scheduler.h',
                        '    const TransferContent* CurrentTransferContentRange(') + '\ntemplate <typename Memory>\n' + body(
                        'src/video_core/renderer_vulkan/vk_scheduler.h',
                        '    bool CurrentTransferHasRoot(') + '\ntemplate <typename Memory>\n' + body(
                        'src/video_core/renderer_vulkan/vk_scheduler.h', '    BoundedReconciliationPlanner::Result PlanControlledReconciliation(') + '\ntemplate <typename Memory>\n' + body(
                        'src/video_core/renderer_vulkan/vk_scheduler.h', '    Transfer RecordControlledReplicaCopies(') + '\ntemplate <typename Memory>\n' + body('src/video_core/renderer_vulkan/vk_scheduler.h', '    Transfer RecordControlledReplicaCopy(') +
                        '\ntemplate <typename Memory>\n' + body('src/video_core/renderer_vulkan/vk_scheduler.h',
                        '    bool CurrentReplicaCopyHasRoot(')),
                    ('RAW_STORAGE_BIND', body('src/video_core/renderer_vulkan/vk_buffer_cache.h',
                        '    void BindStorageBuffer(VkBuffer buffer,')),
                    ('DEVICE_WRITE_GUARDS',
                     'template <typename Func>\n' + body('src/core/device_memory_manager.h', '    [[nodiscard]] bool WithControlledPhysicalAliases(') + '\n' +
                     'template <typename Func>\n' + body('src/core/device_memory_manager.h', '    [[nodiscard]] bool WithControlledMapping(') + '\n' +
                     'template <typename Func>\n' + body('src/core/device_memory_manager.h', '    [[nodiscard]] bool WithCurrentControlledAliases(')),
                    ('ALIAS_CONTAINER', body('src/core/device_memory_manager.inc', 'class MultiAddressContainer {') + ';'),
                    ('SUBMISSION_METHODS', submission), ('TRANSFER_METHODS', transfer),
                    ('DEVICE_TRACKER', tracker), ('REGISTRATION_CLASS', registration),
                    ('CACHE_METHODS', cache),
                    ('REQUEST_ADVANCE', body('src/video_core/renderer_vulkan/vk_rasterizer.cpp',
                        'Core::Memory::ControlledCpuWriteRequest::Phase RasterizerVulkan::AdvanceControlledCpuWriteRequest(')),
                    ('REQUEST_QUEUE', body('src/video_core/renderer_vulkan/vk_rasterizer.cpp',
                        'bool RasterizerVulkan::QueueControlledCpuWriteRequest(')),
                    ('GPU_THREAD', thread_source)]:
    marker = '// @' + name + '@'
    assert template.count(marker) == 1
    template = template.replace(marker, value)
source = cpu + template
cpp = out / 'registration_pipeline.cpp'
cpp.write_text(source)
cmd = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
       '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-I', str(out / 'stubs'),
       '-I', str(previous / 'stubs'), '-I', str(thread_fixture / 'shims'), '-I', str(root / 'src'),
       '-I', str(root / 'externals/Vulkan-Headers/include')]
if a.boost_include:
    cmd += ['-I', str(a.boost_include)]
cmd += [str(root / 'src/common/host_memory.cpp'), str(root / 'src/common/page_table.cpp'),
        str(root / 'src/common/virtual_buffer.cpp')]
if os.name == 'nt':
    cmd += [str(root / 'src/common/dynamic_library.cpp')]
exe = out / ('registration_pipeline.exe' if os.name == 'nt' else 'registration_pipeline')
subprocess.run(cmd + [str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe), str(a.repeat)], check=True, timeout=60)
control = out / 'early_completion.cpp'
assert source.count(timeline) == 1
control.write_text(source.replace(timeline, '[[nodiscard]] VkResult PollTimeline(u64) const noexcept {return VK_SUCCESS;}'))
subprocess.run(cmd + [str(control), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 82 or 'mistaken for completed readback' not in result.stderr:
    raise RuntimeError(f'Early completion control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative control: accepting submission without target completion fails delayed-readback gate (82)')
publication = 'memory_tracker.MarkRegionAsCpuModified(alias, size);'
assert source.count(publication) == 1
control.write_text(source.replace(publication, 'if (alias < ALIAS) ' + publication))
subprocess.run(cmd + [str(control), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 83 or 'missed a real tracker alias' not in result.stderr:
    raise RuntimeError(f'Missing alias publication control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative control: dropping a real device alias from post-store publication fails (83)')
barrier = '.dstAccessMask = VK_ACCESS_HOST_READ_BIT'
assert source.count(barrier) == 2
control.write_text(source.replace(barrier, '.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT'))
subprocess.run(cmd + [str(control), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 84 or 'missing explicit host visibility barrier' not in result.stderr:
    raise RuntimeError(f'Missing host barrier control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative control: missing explicit transfer-to-host read visibility fails (84)')
preserve = 'std::memcpy(target.data(), bytes.data() + segment.logical_offset, target.size());'
assert source.count(preserve) == 1
control.write_text(source.replace(preserve, '(void)target;'))
subprocess.run(cmd + [str(control), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 85 or 'GPU dirty complement lost' not in result.stderr:
    raise RuntimeError(f'Missing preservation control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative control: dropping checked GPU preservation loses complement bytes (85)')
guard = 'CheckControlledSynchronization(device_addr, size);'
assert source.count(guard) == 1
control.write_text(source.replace(guard, '/* deliberately bypass transaction exclusion */'))
subprocess.run(cmd + [str(control), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 86 or 'upload consumed publication' not in result.stderr:
    raise RuntimeError(f'Missing upload exclusion control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative control: upload inside preservation/store/publication is rejected (86)')
publication = 'memory_tracker.MarkRegionAsCpuModified(alias, tx.size);'
assert source.count(publication) == 1
control.write_text(source.replace(publication, '(void)alias;'))
subprocess.run(cmd + [str(control), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 87 or 'transaction omitted real tracker publication' not in result.stderr:
    raise RuntimeError(f'Missing transaction publication control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative control: transaction must publish each physical device alias after store (87)')
hook = 'if (TryBoundCpuWrite(vaddr, data)) [[unlikely]] return;'
assert source.count(hook) == 2  # Actual ordinary CPU body plus disabled-handshake legacy witness.
control.write_text(source.replace(hook, '/* deliberately bypass bound ordinary routing */', 1))
subprocess.run(cmd + [str(control), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 88 or 'bound ordinary Write bypassed its transaction' not in result.stderr:
    raise RuntimeError(f'Ordinary routing bypass control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative control: bypassing the actual ordinary Write transaction branch fails (88)')
preserved = 'if (submitted) return tx.phase = Phase::Preserved;'
assert source.count(preserved) == 1
control.write_text(source.replace(preserved, '''if (submitted) {
    if(tx.request) tx.request->Publish(Core::Memory::ControlledCpuWriteRequest::Phase::Stored,
                                     Core::Memory::PreparedCpuWriteResult::Stored);
    return tx.phase = Phase::Preserved;
}'''))
subprocess.run(cmd + [str(control), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 89 or 'request completed before actual store publication' not in result.stderr:
    raise RuntimeError(f'Early request success control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative control: completed readback cannot acknowledge a still-unperformed CPU store (89)')
owner = 'Owners{operation, preservation, std::move(extra_owner)}'
assert source.count(owner) == 1
control.write_text(source.replace(owner, 'Owners{operation, preservation, {}}'))
accept_stubs = out / 'missing-accept-retention/core'
accept_stubs.mkdir(parents=True, exist_ok=True)
owner_header = read('src/core/controlled_cpu_page.h')
accept_retention = 'accepted[i] = request;'
assert owner_header.count(accept_retention) == 1
accept_stubs.joinpath('controlled_cpu_page.h').write_text(owner_header.replace(accept_retention, ''))
accept_cmd = cmd[:1] + ['-I', str(accept_stubs.parent)] + cmd[1:]
subprocess.run(accept_cmd + [str(control), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 90 or 'accepted request owner escaped Unknown retention' not in result.stderr:
    raise RuntimeError(f'Dropped request retention control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative control: Unknown admission must retain the accepted CPU request after caller drop (90)')
closed = body('src/core/controlled_cpu_page.h', 'inline void CpuWriteOwner::Close()')
owner_stubs = out / 'open-owner-control/core'
owner_stubs.mkdir(parents=True, exist_ok=True)
owner_stubs.joinpath('controlled_cpu_page.h').write_text(
    read('src/core/controlled_cpu_page.h').replace(closed,
        'inline void CpuWriteOwner::Close() { std::unique_lock lock{mutex}; }'))
control_cmd = cmd[:1] + ['-I', str(owner_stubs.parent)] + cmd[1:]
subprocess.run(control_cmd + [str(cpp), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 92 or 'closed Memory owner executed a queued CPU store' not in result.stderr:
    raise RuntimeError(f'Missing owner close control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative lifecycle control: bypassing per-Memory Close permits a forbidden queued store (92)')
pending_stubs = out / 'pending-unknown-control/common'
pending_stubs.mkdir(parents=True, exist_ok=True)
bundle = read('src/common/physical_registration_bundle.h')
pending_guard = 'if (!domain->Quarantine(ticket)) return false;'
assert bundle.count(pending_guard) == 1
pending_stubs.joinpath('physical_registration_bundle.h').write_text(bundle.replace(
    pending_guard, 'if (!active || !domain->MarkUnknown(ticket)) return false;'))
pending_cmd = cmd[:1] + ['-I', str(pending_stubs.parent)] + cmd[1:]
subprocess.run(pending_cmd + [str(cpp), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 93 or 'accepted parked Unknown lost its execution identity' not in result.stderr:
    raise RuntimeError(f'Parked Unknown control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative lifecycle control: parked accepted Unknown must retain its execution identity (93)')
subprocess.run(accept_cmd + [str(cpp), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 94 or 'accepted construction failure lost request identity' not in result.stderr:
    raise RuntimeError(f'Construction Unknown control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative lifecycle control: pre-transaction Unknown must retain the accepted request (94)')
cpp.write_text(source.replace('if (!ControlledReadbacksIdle(operation)) {',
                              'if (false && !ControlledReadbacksIdle(operation)) {'))
subprocess.run(cmd + [str(cpp), '-o', str(exe)], check=True)
result = subprocess.run([str(exe), '1'], capture_output=True, text=True, timeout=60)
if result.returncode != 95 or 'readback conflict accepted before producer retirement' not in result.stderr:
    raise RuntimeError(f'Readback isolation control failed incorrectly: {result.returncode} {result.stderr}')
print('PASS negative isolation control: pending physical-alias readback must reject before acceptance (95)')

# Exercise the real alias transfer/factory/checked-submit bodies against the same
# fake Vulkan dispatch. Kept separate so original mutation controls retain their
# exact failure sites. These are fault-injection fixtures, not native driver proof.
alias_source = source.rsplit('int main(int argc,char** argv) {', 1)[0]
alias_source = alias_source.replace(
    '#include "video_core/buffer_cache/controlled_transfer.h"',
    '#include "video_core/buffer_cache/controlled_transfer.h"\n'
    '#include "video_core/buffer_cache/physical_alias_copy.h"')
declaration = 'std::shared_ptr<VideoCommon::ControlledBufferTransfer> PrepareControlledTransfer(const Buffer&);'
assert alias_source.count(declaration) == 1
alias_source = alias_source.replace(declaration, declaration + '\n'
    'std::shared_ptr<VideoCommon::ControlledAliasTransfer> PrepareAliasTransfer('
    'StagedBufferPin,StagedBufferPin,std::span<const VideoCommon::AliasCopyRange>);')
alias_source += '\nnamespace Vulkan {\n' + body(runtime_path, 'class AliasTransfer final') + ';\n'
alias_source += body(runtime_path,
    'std::shared_ptr<VideoCommon::ControlledAliasTransfer> BufferCacheRuntime::PrepareAliasTransfer(')
alias_source += '\n}\n' + r'''
int main() {
    using Poll=VideoCommon::ControlledTransferPoll;
    using Admission=Common::PhysicalRangeAdmission;
    for(int mode=0;mode<7;++mode) {
        auto s=std::make_unique<Scenario>();
        auto source=std::make_unique<Vulkan::Buffer>(s->runtime,A,4096);
        auto target=std::make_unique<Vulkan::Buffer>(s->runtime,ALIAS,4096);
        const auto source_owner=source->WeakPayload(),target_owner=target->WeakPayload();
        auto from=source->PinStagedRepresentation(0,4096);
        auto to=target->PinStagedRepresentation(0,4096);assert(from && to);
        auto* input=reinterpret_cast<Allocation*>(from->Handle());
        auto* output=reinterpret_cast<Allocation*>(to->Handle());
        std::fill(input->bytes.begin(),input->bytes.end(),u8{99});
        std::fill(output->bytes.begin(),output->bytes.end(),u8{10});
        auto unaligned=source->PinStagedRepresentation(1,4095);assert(unaligned);
        const std::array invalid{VideoCommon::AliasCopyRange{0,0,4,0}};
        assert(!s->runtime.PrepareAliasTransfer(*unaligned,*to,invalid));
        unaligned.reset();
        const std::array copies{VideoCommon::AliasCopyRange{128,256,256,0}};
        auto transfer=s->runtime.PrepareAliasTransfer(*from,*to,copies);assert(transfer);
        auto domain=s->views.front().physical.Epoch()->ranges;
        const std::array access{Admission::Access{0,256,Admission::Mode::Write}};
        auto ticket=domain->Enqueue(access,transfer);assert(ticket);
        assert(domain->TryStart(ticket)==Admission::Start::Granted);
        if(mode==5) s->runtime.staged_pin_domain->Seal();
        const bool submitted=transfer->Submit();
        assert(submitted==(mode!=5));
        assert(!transfer->Submit()); // No second recording, even after refusal/failure.
        if(mode!=5) {
            assert(transfer->Poll()==Poll::Pending);
            if(mode==1) s->gpu.submit_result=VK_ERROR_UNKNOWN;
            s->scheduler.RunWorker();
            assert(s->gpu.submissions==1);
            source.reset();target.reset();from.reset();to.reset(); // Real payload pins own both resources.
            assert(!source_owner.expired() && !target_owner.expired());
            if(mode==1) assert(transfer->Poll()==Poll::Unknown);
            else {
                assert(transfer->Poll()==Poll::Pending); // Callback return is not GPU completion.
                s->gpu.Complete();
                if(mode==2) s->gpu.counter_result=VK_ERROR_UNKNOWN;
                if(mode==3) s->gpu.invalidate_result=VK_ERROR_UNKNOWN;
                if(mode==4) reinterpret_cast<Allocation*>(s->gpu.readback_target)->bytes[256+7]^=1;
                if(mode==6) s->gpu.wait_result=VK_ERROR_UNKNOWN;
                if(mode==0) {
                    assert(transfer->Poll()==Poll::Complete && transfer->Poll()==Poll::Complete);
                    for(size_t i=0;i<4096;++i) assert(output->bytes[i]==(i>=256 && i<512?99:10));
                    assert(s->gpu.host_read_barrier);
                } else assert(transfer->Poll()==Poll::Unknown);
            }
        } else assert(transfer->Poll()==Poll::Unknown);
        if(mode==0) assert(domain->Complete(ticket));
        else assert(domain->MarkUnknown(ticket));
        auto reader=domain->Enqueue(access,std::make_shared<int>(1));assert(reader);
        if(mode==0) {
            assert(domain->TryStart(reader)==Admission::Start::Granted);
            assert(domain->Complete(reader));
        } else {
            assert(domain->TryStart(reader)==Admission::Start::Park);
            assert(domain->Cancel(reader));
        }
        std::weak_ptr<VideoCommon::ControlledAliasTransfer> retained=transfer;
        source.reset();target.reset();from.reset();to.reset();
        transfer.reset();ticket.reset();
        if(mode!=0) assert(!retained.expired()); // Unknown domain retains actual native owners.
        if(mode!=0) assert(!source_owner.expired() && !target_owner.expired());
        s->runtime.RetireControlledTransfersAfterDeviceIdle(); // Fixture trusted-idle boundary.
        assert(source_owner.expired() && target_owner.expired());
        if(auto alive=retained.lock()) {
            assert(alive->Poll()==Poll::Unknown);
            assert(!alive->SourceIdentity() && !alive->DestinationIdentity());
        }
    }
    std::cout<<"PASS actual alias transfer fault fixture: exact copy/complements, delayed completion, "
               "submit/counter/wait/invalidate/corruption/seal failures, one-shot submission, dropped cache "
               "owners and Unknown quarantine (Vulkan dispatch remains a fixture)\n";
}
'''
alias_cpp = out / 'alias_transfer.cpp'
alias_cpp.write_text(alias_source)
alias_exe = out / ('alias_transfer.exe' if os.name == 'nt' else 'alias_transfer')
subprocess.run(cmd + [str(alias_cpp), '-o', str(alias_exe)], check=True)
subprocess.run([str(alias_exe)], check=True, timeout=60)

# Separate checked-write provenance fixture. The scheduler, fill recorder and
# direct Draw worker command are extracted actual bodies; GPU execution is fake.
provenance = source.rsplit('int main(int argc,char** argv) {', 1)[0]
provenance += '\nnamespace Vulkan {\n' + body(runtime_path, 'auto MakeBufferClear(') + '\n'
provenance += body(runtime_path, 'Scheduler::Write BufferCacheRuntime::ClearBufferChecked(') + '\n}\n'
draw = body('src/video_core/renderer_vulkan/vk_rasterizer.cpp',
            '        RecordShaderStorageWrite([draw_params, draw_pipeline]')
draw = draw[draw.index('{') + 1:-1]
provenance += r'''
struct DrawParams {bool is_indexed=false;u32 num_vertices=3,num_instances=1,first_index=0,base_vertex=0,base_instance=0;};
struct Pipeline {bool failed=false;bool IsFailed() const {return failed;}};
bool ActualDrawWorker(vk::CommandBuffer cmdbuf,const Pipeline* draw_pipeline,DrawParams draw_params) {
''' + draw + '\n}\n'
provenance += r'''
int main() {
    using State=Vulkan::Scheduler::WriteState;
    for(int mode=0;mode<9;++mode) {
        auto s=std::make_unique<Scenario>();
        auto buffer=std::make_unique<Vulkan::Buffer>(s->runtime,A,4096);
        auto weak_payload=buffer->WeakPayload();
        auto pin=buffer->PinStagedRepresentation(0,4096);assert(pin);
        const auto identity=pin->Identity();
        const auto handle=pin->Handle();
        auto* bytes=reinterpret_cast<Allocation*>(handle);
        std::fill(bytes->bytes.begin(),bytes->bytes.end(),u8{10});
        auto resources=std::make_shared<Vulkan::StagedBufferPin>(*pin);
        Pipeline pipeline{mode==5};
        driver->draw_target=handle;
        if(mode==1) driver->submit_result=VK_ERROR_DEVICE_LOST;
        Vulkan::Scheduler::Write write;
        if(mode==0 || mode==8) write=s->runtime.ClearBufferChecked(*pin,7,0x63636363);
        else write=s->scheduler.RecordCheckedWrite(resources,identity,0,4096,7,
            [&,handle](vk::CommandBuffer command) {
                if(mode==5 || mode==6) return ActualDrawWorker(command,&pipeline,DrawParams{mode==6});
                Vulkan::MakeBufferClear(handle,0,4096,0x63636363)(command);
                if(mode==4) throw std::runtime_error("partial command recording");
                return true;
            });
        assert(write && write->ResourceIdentity()==identity && write->Offset()==0 && write->Size()==4096);
        assert(write->IntentRevision()==7);
        assert(write->SubmissionIdentity()!=nullptr);
        assert(s->scheduler.PollCheckedWrite(write)==State::Pending);
        if(mode==8) s->runtime.staged_pin_domain->Seal();
        buffer.reset();pin.reset();resources.reset();
        assert(!weak_payload.expired());
        s->scheduler.RunWorker();
        if(mode==1 || mode==4 || mode==8) assert(s->scheduler.PollCheckedWrite(write)==State::Unknown);
        else assert(s->scheduler.PollCheckedWrite(write)==State::Submitted); // Queue acceptance is not completion.
        s->gpu.Complete();
        if(mode==2) driver->counter_result=VK_ERROR_DEVICE_LOST;
        if(mode==3) driver->wait_result=VK_ERROR_DEVICE_LOST;
        if(mode==7) s->scheduler.submission_owner->store(false);
        const auto expected=mode==5?State::Skipped:((mode==0 || mode==6)?State::Complete:State::Unknown);
        assert(s->scheduler.PollCheckedWrite(write)==expected);
        driver->submit_result=driver->counter_result=driver->wait_result=VK_SUCCESS;
        assert(s->scheduler.PollCheckedWrite(write)==expected); // Unknown cannot recover into Complete.
        if(mode==0 || mode==6) for(auto byte:bytes->bytes) assert(byte==99);
        if(mode==5) {assert(driver->draws==0);for(auto byte:bytes->bytes) assert(byte==10);}
        if(mode==6) assert(driver->draws==1);
        assert(!weak_payload.expired()); // Scheduler retains accepted evidence even if cache owners disappear.
        s->scheduler.RetireCheckedWritesAfterDeviceIdle();
        assert(weak_payload.expired() && write->ResourceIdentity()==nullptr);
        assert(s->scheduler.PollCheckedWrite(write)==State::Unknown);
    }
    std::cout<<"PASS actual checked-write provenance fixture: skipped failed-pipeline Draw, indexed emission, "
               "delayed completion, submit/counter/wait/partial-record/seal/owner failures, sticky Unknown "
               "and retained payload until trusted idle (Vulkan execution remains a fixture)\n";
}
'''
provenance_cpp = out / 'write_provenance.cpp'
provenance_cpp.write_text(provenance)
provenance_exe = out / ('write_provenance.exe' if os.name == 'nt' else 'write_provenance')
subprocess.run(cmd + [str(provenance_cpp), '-o', str(provenance_exe)], check=True)
subprocess.run([str(provenance_exe)], check=True, timeout=60)

# Ordinary recorder integration: actual binding capture, actual rasterizer recorder,
# actual Draw worker and actual scheduler; descriptor/driver execution stays fake.
observer = provenance[:provenance.index('int main() {')]
for index, marker in enumerate([
        '            RecordShaderStorageWrite([buffer_obj = buffer->Handle(), offset, stride',
        '            RecordShaderStorageWrite([draw_buffer_obj = draw_buffer->Handle(),',
        '        RecordShaderStorageWrite([buffer_obj = buffer->Handle(), offset, params,',
        '        RecordShaderStorageWrite([indirect_buffer = buffer->Handle(),',
        '    RecordShaderStorageWrite([dim]']):
    worker = body('src/video_core/renderer_vulkan/vk_rasterizer.cpp', marker)
    observer += f'\nbool ShaderWorker{index}(vk::CommandBuffer cmdbuf, [[maybe_unused]] const Pipeline* draw_pipeline) {{\n'
    observer += '''[[maybe_unused]] VkBuffer buffer_obj=driver->draw_target,draw_buffer_obj=buffer_obj,indirect_buffer=buffer_obj;
[[maybe_unused]] u32 offset=0,offset_base=0,stride=4,indirect_offset=0;
[[maybe_unused]] std::array<u32,3> dim{1,1,1};
struct Params {bool is_indexed=true;u32 max_draw_counts=1,stride=4;};
[[maybe_unused]] Params params;
'''
    observer += worker[worker.index('{') + 1:-1] + '\n}\n'
observer += r'''
namespace Vulkan {
struct StorageObserverHarness {
    Scheduler& scheduler;BufferCacheRuntime& buffer_cache_runtime;
    bool observe_storage_writes=true,storage_observation_incomplete=false;
    std::vector<Scheduler::Write> observed_storage_writes;
    std::shared_ptr<VideoCommon::PhysicalWriteCoverage> storage_write_coverage=
        std::make_shared<VideoCommon::PhysicalWriteCoverage>();
    StorageObserverHarness(Scheduler& s,BufferCacheRuntime& r):scheduler{s},buffer_cache_runtime{r} {
        observed_storage_writes.reserve(256);
        r.SetPhysicalObservationMemory(&r.test_mapping);
    }
    void RecordObservedShaderStorageWrite(std::function<bool(vk::CommandBuffer)>);
    void MarkStorageObservationIncomplete();
''' + 'template <typename Command>\n' + body('src/video_core/renderer_vulkan/vk_rasterizer.h',
           '    void RecordShaderStorageWrite(Command&& command)') + r'''
};
''' + body('src/video_core/renderer_vulkan/vk_rasterizer.cpp',
           'void RasterizerVulkan::MarkStorageObservationIncomplete(').replace(
               'RasterizerVulkan::', 'StorageObserverHarness::') + '\n' + body('src/video_core/renderer_vulkan/vk_rasterizer.cpp',
           'void RasterizerVulkan::RecordObservedShaderStorageWrite(').replace(
               'RasterizerVulkan::', 'StorageObserverHarness::') + '\n}\n'
observer += r'''
int main() {
    using State=Vulkan::Scheduler::WriteState;
    for(int mode=0;mode<9;++mode) {
        auto s=std::make_unique<Scenario>();
        Vulkan::StorageObserverHarness rasterizer{s->scheduler,s->runtime};
        auto buffer=std::make_unique<Vulkan::Buffer>(s->runtime,mode==4?0:A,4096);
        auto weak=buffer->WeakPayload();
        const auto handle=buffer->Handle();
        driver->draw_target=handle;
        Pipeline pipeline{mode==1};
        std::fill(reinterpret_cast<Allocation*>(handle)->bytes.begin(),
                  reinterpret_cast<Allocation*>(handle)->bytes.end(),u8{10});
        if(mode==3) rasterizer.observe_storage_writes=false;
        s->runtime.BeginStorageWriteObservation(rasterizer.observe_storage_writes);
        if(mode==0) {
            s->runtime.BindStorageBuffer(*buffer,0,4096,false);
            assert(s->runtime.StorageWriteObservationValid() && s->runtime.StorageWriteFootprints().empty());
        }
        buffer->gpu_write_revision.Mark();
        s->runtime.BindStorageBuffer(*buffer,0,4096,true);
        if(mode==5) s->runtime.BindStorageBuffer(handle,0,4096,true);
        if(mode==6) for(int i=0;i<64;++i) s->runtime.BindStorageBuffer(*buffer,0,4096,true);
        if(mode==7) s->scheduler.checked_writes.resize(256); // Capacity refusal, never command refusal.
        auto draw=[&](vk::CommandBuffer command) {return ActualDrawWorker(command,&pipeline,{});};
        rasterizer.RecordShaderStorageWrite(draw);
        const bool covered=mode<3 || mode==8;
        assert(rasterizer.storage_observation_incomplete==(!covered && mode!=3));
        assert(rasterizer.observed_storage_writes.size()==(covered?1u:0u));
        assert(driver->submissions==0 && driver->draws==0); // No per-command flush or execution.
        if(covered) {
            auto first=rasterizer.observed_storage_writes.front();
            assert(first->IntentRevision()==2 && first->Offset()==0 && first->Size()==4096);
            buffer->gpu_write_revision.Mark();
            s->runtime.EndStorageWriteObservation();
            s->runtime.BeginStorageWriteObservation(true);
            s->runtime.BindStorageBuffer(*buffer,128,256,true);
            rasterizer.RecordShaderStorageWrite(draw);
            auto second=rasterizer.observed_storage_writes.back();
            assert(second->IntentRevision()==3 && second->Offset()==128 && second->Size()==256);
            assert(first->ResourceIdentity()==second->ResourceIdentity());
            assert(first->SubmissionIdentity()==second->SubmissionIdentity());
            assert(s->scheduler.PollCheckedWrite(first)==State::Pending);
            s->scheduler.RetireCheckedWritesAfterDeviceIdle();
            assert(first->ResourceIdentity()!=nullptr); // Idle cannot release unsubmitted host work.
            s->runtime.EndStorageWriteObservation();buffer.reset();
            assert(!weak.expired());
            s->scheduler.RunWorker(); // Command recording alone is not queue acceptance.
            assert(s->scheduler.PollCheckedWrite(first)==State::Pending && driver->submissions==0);
            if(mode==2) driver->submit_result=VK_ERROR_DEVICE_LOST;
            bool threw=false;
            VkResult propagated=VK_SUCCESS;
            s->scheduler.fail_end_pending=mode==8;
            try {
                s->scheduler.SubmitExecution(nullptr,nullptr); // Ordinary Flush's actual submit body.
                s->scheduler.RunWorker();
            } catch(const vk::Exception& error) {threw=true;propagated=error.GetResult();}
              catch(const std::exception&) {threw=true;}
            s->scheduler.fail_end_pending=false;
            const bool failed=mode==2 || mode==8;
            assert(first->HistoricalPhysicalIdentity());
            assert(!s->scheduler.CompletedPhysicalIdentity(first)); // Open scope is never complete coverage.
            rasterizer.storage_write_coverage->Seal();
            assert(threw==failed); // Ordinary failure policy must survive diagnostics.
            assert(s->scheduler.PollCheckedWrite(first)==(failed?State::Unknown:State::Submitted));
            if(mode==2) {
                assert(propagated==VK_ERROR_DEVICE_LOST && driver->reported_losses==1);
                assert(driver->counter==0 && driver->submitted_tick==0 && driver->submitted.empty());
                driver->submit_result=VK_SUCCESS;
                s->gpu.Complete();
                assert(driver->counter==0 && s->scheduler.PollCheckedWrite(first)==State::Unknown);
                assert(!s->scheduler.CompletedPhysicalIdentity(first));
            }
            if(!failed) {
                s->gpu.Complete();
                assert(s->scheduler.PollCheckedWrite(first)==(mode==1?State::Skipped:State::Complete));
                assert(bool(s->scheduler.CompletedPhysicalIdentity(first))==(mode!=1));
                assert(driver->draws==(mode==1?0u:2u));
                for(auto byte:reinterpret_cast<Allocation*>(handle)->bytes) assert(byte==(mode==1?10:99));
            }
            s->scheduler.RetireCheckedWritesAfterDeviceIdle();
            assert(weak.expired() && s->scheduler.PollCheckedWrite(first)==State::Unknown);
        } else {
            s->runtime.EndStorageWriteObservation();
            s->scheduler.SubmitExecution(nullptr,nullptr);s->scheduler.RunWorker();s->gpu.Complete();
            assert(driver->draws==1 && driver->submissions==1);
            if(mode==7) s->scheduler.checked_writes.clear();
        }
    }
    {
        auto s=std::make_unique<Scenario>();
        Vulkan::StorageObserverHarness rasterizer{s->scheduler,s->runtime};
        Vulkan::Buffer a{s->runtime,A,4096},b{s->runtime,ALIAS,4096};
        driver->draw_target=a.Handle();
        s->runtime.BeginStorageWriteObservation(true);
        s->runtime.BindStorageBuffer(a,0,128,true);
        s->runtime.BindStorageBuffer(b,256,512,true);
        Pipeline pipeline;
        rasterizer.RecordShaderStorageWrite([&](vk::CommandBuffer command) {
            return ActualDrawWorker(command,&pipeline,{});
        });
        s->runtime.EndStorageWriteObservation();
        const auto& writes=rasterizer.observed_storage_writes;
        assert(writes.size()==2 && writes[0]->ResourceIdentity()!=writes[1]->ResourceIdentity());
        assert(writes[0]->SubmissionIdentity()==writes[1]->SubmissionIdentity());
        assert(writes[0]->Offset()==0 && writes[0]->Size()==128);
        assert(writes[1]->Offset()==256 && writes[1]->Size()==512);
        s->scheduler.SubmitExecution(nullptr,nullptr);s->scheduler.RunWorker();s->gpu.Complete();
        for(const auto& write:writes) assert(s->scheduler.PollCheckedWrite(write)==State::Complete);
        s->scheduler.RetireCheckedWritesAfterDeviceIdle();
    }
    {
        auto s=std::make_unique<Scenario>();
        Vulkan::StorageObserverHarness rasterizer{s->scheduler,s->runtime};
        Vulkan::Buffer buffer{s->runtime,A,4096};
        driver->draw_target=buffer.Handle();
        Pipeline pipeline;
        for(size_t i=0;i<257;++i) {
            s->runtime.BeginStorageWriteObservation(true);
            s->runtime.BindStorageBuffer(buffer,0,4096,true);
            rasterizer.RecordShaderStorageWrite([&](vk::CommandBuffer command) {
                return ActualDrawWorker(command,&pipeline,{});
            });
            s->runtime.EndStorageWriteObservation();
            if(i==199) assert(!rasterizer.storage_observation_incomplete);
        }
        assert(rasterizer.storage_observation_incomplete && rasterizer.observed_storage_writes.size()==256);
        rasterizer.storage_write_coverage->Seal();
        s->scheduler.SubmitExecution(nullptr,nullptr);s->scheduler.RunWorker();s->gpu.Complete();
        assert(driver->draws==257 && driver->submissions==1);
        for(const auto& write:rasterizer.observed_storage_writes) {
            assert(s->scheduler.PollCheckedWrite(write)==State::Complete);
            assert(write->HistoricalPhysicalIdentity() && !write->PhysicalCoverageComplete());
            assert(!s->scheduler.CompletedPhysicalIdentity(write));
        }
        s->scheduler.RetireCheckedWritesAfterDeviceIdle();
    }
    {
        auto s=std::make_unique<Scenario>();
        // An allocation predating observation must not be labelled from today's mapping.
        Vulkan::Buffer unqualified{s->runtime,A,4096};
        Vulkan::StorageObserverHarness rasterizer{s->scheduler,s->runtime};
        s->runtime.CapturePhysicalOrigin(unqualified);
        assert(!unqualified.physical_origin); // Constructor-only origin, even after observation starts.
        s->runtime.BeginStorageWriteObservation(true);
        s->runtime.BindStorageBuffer(unqualified,0,4096,true);
        assert(!s->runtime.StorageWriteObservationValid());
        s->runtime.EndStorageWriteObservation();
        Vulkan::Buffer original{s->runtime,A,8192},alias{s->runtime,ALIAS,8192};
        auto a=s->runtime.PhysicalIdentityForBinding(original,3840,512);
        auto b=s->runtime.PhysicalIdentityForBinding(alias,3840,512);
        assert(a && b && a->Segments().size()==2 && a->Size()==512 && a->NativeOffset()==3840);
        assert(a->OverlapsPhysical(*b) && a->Backing()==b->Backing());
        assert(a->Representation()!=b->Representation());
        const auto old_physical=a->Segments().front().physical_offset;
        s->runtime.test_mapping.generation+=2; // Same final geometry cannot erase mapping history.
        assert(!a->MatchesCurrent(s->runtime.test_mapping));
        assert(a->MatchesCurrent(s->runtime.test_mapping,false));
        assert(!s->runtime.PhysicalIdentityForBinding(original,3840,512));
        s->runtime.test_mapping.bias=4096;s->runtime.test_mapping.generation+=2;
        assert(!a->MatchesCurrent(s->runtime.test_mapping));
        assert(!s->runtime.PhysicalIdentityForBinding(original,3840,512));
        assert(a->Segments().front().physical_offset==old_physical); // Never resolve a replacement origin.
        Vulkan::Buffer replacement{s->runtime,A,8192};
        auto fresh=s->runtime.PhysicalIdentityForBinding(replacement,3840,512);
        assert(fresh && fresh->Segments().front().physical_offset==old_physical+4096);
        assert(fresh->Representation()!=a->Representation());
        Vulkan::Buffer wrong_representation{s->runtime,A,8192};
        wrong_representation.physical_origin=replacement.physical_origin;
        assert(!s->runtime.PhysicalIdentityForBinding(wrong_representation,3840,512));
        s->runtime.InvalidatePhysicalOrigin(replacement);
        s->runtime.CapturePhysicalOrigin(replacement);
        assert(!s->runtime.PhysicalIdentityForBinding(replacement,3840,512));
        s->runtime.test_mapping.hole=true;
        Vulkan::Buffer hole{s->runtime,A,4096};
        assert(!s->runtime.PhysicalIdentityForBinding(hole,0,4096));
        s->runtime.test_mapping.hole=false;
        s->runtime.test_mapping.backing->alive.store(false);
        assert(!a->OwnersAlive() && a->Segments().front().physical_offset==old_physical);
    }
    for(bool skipped:{false,true}) {
        auto s=std::make_unique<Scenario>();
        Vulkan::Buffer buffer{s->runtime,A,4096};
        driver->draw_target=buffer.Handle();
        auto pin=buffer.PinStagedRepresentation(0,4096);assert(pin);
        const std::array<Vulkan::Scheduler::WriteFootprint,1> ranges{{
            {pin->RetainedOwner(),pin->Identity(),0,4096,1}}};
        Pipeline pipeline{skipped};
        std::vector<Vulkan::Scheduler::Write> writes;
        for(auto worker:{ShaderWorker0,ShaderWorker1,ShaderWorker2,ShaderWorker3,ShaderWorker4}) {
            auto observed=s->scheduler.RecordObservedWrites(ranges,[&,worker](vk::CommandBuffer command) {
                return worker(command,&pipeline);
            });
            assert(observed && observed->size()==1);
            writes.push_back(observed->front());
            assert(writes.front()->SubmissionIdentity()==writes.back()->SubmissionIdentity());
        }
        assert(driver->submissions==0);
        s->scheduler.SubmitExecution(nullptr,nullptr);s->scheduler.RunWorker();s->gpu.Complete();
        assert(driver->draws==(skipped?2u:5u)); // Compute emits; all three failed Draw variants skip.
        for(size_t i=0;i<writes.size();++i)
            assert(s->scheduler.PollCheckedWrite(writes[i])==(skipped && i<3?State::Skipped:State::Complete));
        s->scheduler.RetireCheckedWritesAfterDeviceIdle();
    }
    std::cout<<"PASS ordinary storage observer actual-body fixture: bind identity/range/revision, "
               "shared batch without flush, skipped Draw, pending versus completed, ordinary submit throws, "
               "unsubmitted retention, disabled/scratch/raw/capacity refusal executes once; no authority\n";
    std::cout<<"PASS immutable physical identity: aliases, partial pages, old remap geometry, "
               "unqualified/rebased/invalidated/mismatched representations and dead epochs; "
               "device-loss propagation with no timeline success; closed scope and 257-command overflow gate\n";
}
'''
observer_cpp = out / 'storage_observer.cpp'
sync_fixture = read('tools/physical_coherency_tests/sync_dependency_fixture.cpp.in')
sync_fixture += '\n' + read('tools/physical_coherency_tests/transfer_content_fixture.cpp.in')
sync_fixture += '\n' + read('tools/physical_coherency_tests/replica_copy_fixture.cpp.in')
sync_fixture += '\n' + read('tools/physical_coherency_tests/mixed_replica_fixture.cpp.in')
sync_fixture += '\n' + read('tools/physical_coherency_tests/destination_replica_fixture.cpp.in')
sync_fixture += '\n' + read('tools/physical_coherency_tests/combined_replica_fixture.cpp.in')
sync_fixture += '\n' + read('tools/physical_coherency_tests/multi_region_fixture.cpp.in')
sync_fixture += '\n' + read('tools/physical_coherency_tests/mixed_holes_fixture.cpp.in')
sync_fixture += '\n' + read('tools/physical_coherency_tests/reconciliation_planner_fixture.cpp.in')
observer = observer.replace('int main() {', sync_fixture + '\nint main() {')
observer = observer.replace('    std::cout<<"PASS ordinary storage observer',
                            '    TestSyncDependencies();\n    TestTransferContent();\n    TestReplicaCopy();\n    TestReplicaChain();\n    TestMixedReplicaCopy();\n    TestDestinationReplicaCopy();\n    TestCombinedReplicaCopy();\n    TestMultiRegionReplicaCopy();\n    TestMixedInternalHoles();\n    TestReconciliationPlanner();\n    std::cout<<"PASS ordinary storage observer')
observer_cpp.write_text(observer)
observer_exe = out / ('storage_observer.exe' if os.name == 'nt' else 'storage_observer')
subprocess.run(cmd + [str(observer_cpp), '-o', str(observer_exe)], check=True)
subprocess.run([str(observer_exe)], check=True, timeout=60)
coverage_guard = '!write->PhysicalCoverageComplete() ||'
assert observer.count(coverage_guard) == 1
coverage_cpp = out / 'storage_coverage_bypass.cpp'
coverage_cpp.write_text(observer.replace(coverage_guard, 'false ||'))
coverage_exe = out / ('storage_coverage_bypass.exe' if os.name == 'nt' else 'storage_coverage_bypass')
subprocess.run(cmd + [str(coverage_cpp), '-o', str(coverage_exe)], check=True)
negative = subprocess.run([str(coverage_exe)], capture_output=True, text=True, timeout=60)
(out / 'storage-coverage-negative.txt').write_text(negative.stdout + negative.stderr)
assert negative.returncode != 0 and 'CompletedPhysicalIdentity(write)' in negative.stderr
print('PASS negative physical coverage control: ignoring shared coverage admits an overflowing prefix')
inputs.update(['src/common/producer_drain.h','src/video_core/renderer_vulkan/vk_private_producer_guard.h',
               'src/common/physical_registration_bundle.h', 'src/core/controlled_cpu_page.h',
               'src/video_core/buffer_cache/controlled_conflict.h', 'src/common/range_sets.inc',
               'src/video_core/gpu_thread.cpp','src/video_core/gpu_thread.h',
               'src/core/hle/kernel/k_process.cpp','src/core/memory.h',
               'src/video_core/buffer_cache/controlled_transfer.h', 'src/video_core/physical_access_plan.h',
               'src/video_core/renderer_vulkan/vk_buffer_payload.h',
               'src/video_core/buffer_cache/physical_write_identity.h',
               'src/video_core/renderer_vulkan/vk_sync_provenance.h',
               'src/video_core/renderer_vulkan/vk_transfer_content.h',
               'src/video_core/renderer_vulkan/vk_transfer_eligibility.h',
               'src/video_core/renderer_vulkan/vk_replica_copy_provenance.h',
               'src/video_core/vulkan_common/vulkan_wrapper.h',
               'src/video_core/buffer_cache/physical_alias_copy.h'])
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sorted(inputs)},
    'real': ['cold device physical plan and HostMemory pins', 'actual BufferCache controlled registration/refresh/poll and Register',
             'actual controlled Vulkan upload/readback and cache buffer pin ownership',
             'actual Scheduler checked submit/poll and MasterSemaphore timeline submission/poll bodies',
             'actual ordinary Memory::Write8/16/32/64, scoped ordinary binding and CPU stores',
             'actual MemoryTrackerBase/WordManager'],
    'fixtures': ['device mapping snapshot and CPU cached-tag fanout', 'Vulkan dispatch, allocation and command/device execution',
                 'cache binding teardown and usage stamping'],
    'acceptance': 'Neither this fixture suite nor native compilation closes ordinary-game F2',
}, indent=2))

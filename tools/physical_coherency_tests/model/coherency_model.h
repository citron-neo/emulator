// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Coherency {
using Id = std::uint64_t;
using Bytes = std::vector<std::uint8_t>;
struct Segment {
    Id epoch{};
    std::size_t logical{}, physical{}, size{};
    bool hole{};
    bool operator==(const Segment&) const = default;
};
struct View {
    Id epoch{}, manager{}, gpu_va{}, daddr{}, generation{1};
    std::size_t size{};
    std::vector<Segment> segments;
    bool mapped{true}, mutating{};
};
struct Stamp {
    Id view{}, epoch{}, generation{};
};
enum class Error {
    None,
    Invalid,
    Hole,
    StaleStamp,
    MutationInProgress,
    Unordered,
    Pending,
    UnsupportedAlias,
    UnknownCoverage,
    RawAccess,
    Layout,
    Allocation,
    Budget,
    NeedsPreservation,
    ResourceBusy,
    SyncRequired,
    Retry,
    Unknown,
    EpochDead,
};
enum class Phase { Accepted, Recorded, Submitted, Completed, Visible, Cancelled, Unknown };
enum class Kind { CPU, Staged, Scratch, Texture, Opaque };
enum class Access {
    CPUReady,
    ClosingCPU,
    GPUPending,
    GPUActive,
    VisibilityPending,
    StartingCPU,
    Unknown
};
enum class Ack { Success, Retry, Fail };
enum class Fault { Allocation, Submit, Complete, Visibility, Sync, Eviction, Teardown };
struct Ticket {
    Id epoch{}, id{};
    bool operator==(const Ticket&) const = default;
};
struct Result {
    Error error{Error::None};
    Ticket ticket{};
    Id id{};
    Bytes bytes;
    std::vector<Ticket> tickets;
    explicit operator bool() const {
        return error == Error::None;
    }
};
struct Binding {
    Stamp stamp;
    Id representation{};
    bool writable{}, footprint_known{true}, coverage_known{true}, layout_supported{true};
};
struct Limits {
    std::size_t cells{256}, representations{32}, operations{1024}, views{128};
    std::size_t metadata_bytes{4 << 20}, storage_bytes{1 << 20};
};
struct Metrics {
    std::size_t cells{}, replicas{}, producers{}, pending_producers{};
    std::size_t metadata_bytes{}, peak_metadata_bytes{}, storage_bytes{};
    Id splits{}, coalesces{}, rejected{}, quarantined{}, calls{}, elapsed_ns{}, max_call_ns{};
};
struct CellInfo {
    std::size_t begin{}, end{};
    Id version{}, producer{}, holder{}, prior_version{};
    bool unknown{};
    bool operator==(const CellInfo&) const = default;
};

// CPU-only executable contract. All public mutation APIs are transactional;
// typed admission errors have no model effects. Unknown errors deliberately
// retain cells, producers, bytes and leases. No production headers or callbacks.
class Model {
public:
    explicit Model(Bytes initial, Id epoch = 1, std::size_t chunk_size = 32, Limits limits = {});
    static constexpr Id CPU = 1;
    Result RegisterView(View view);
    Stamp GetStamp(Id view) const;
    Result RegisterRepresentation(Stamp stamp, Kind kind, bool codec = true);
    Result Preflight(const std::vector<Binding>& bindings) const;
    Result AcquireCPU(Stamp stamp, bool write, const std::vector<Ticket>& after,
                      bool controlled = true);
    Result CommitCPU(Ticket lease, std::size_t offset, const Bytes& data);
    Result ReleaseCPU(Ticket lease);
    Result ReserveGPU(Stamp stamp, Id representation, const std::vector<Ticket>& after,
                      unsigned queue = 0);
    Result ReserveDispatch(const std::vector<Binding>& bindings, const std::vector<Ticket>& after,
                           unsigned queue = 0);
    Result Record(Ticket operation, std::size_t offset, const Bytes& data);
    Result Submit(Ticket operation);
    Result Complete(Ticket operation);
    Result Cancel(Ticket operation);
    Result MakeVisible(Ticket operation);
    Result Read(Stamp stamp, Id representation);
    Result Synchronize(Id representation);
    Result Gather(Id scratch);
    Result Scatter(Ticket operation); // preserves only still-authoritative slices
    Result Unmap(Stamp stamp);
    Result Remap(Stamp stamp, View replacement);
    Result BeginMutation(Stamp stamp);
    Result EndMutation(Id view);
    Result Evict(Id representation);
    Result DrainRenderer();
    Result ReplaceBacking(Bytes initial, Id new_epoch);
    Result DMA(Stamp source, Stamp destination, const std::vector<Ticket>& after);
    Result BeginGPUAccess(std::size_t chunk, bool coverage_proven);
    Result EndCPUAccess(std::size_t chunk, Ack ack, bool flushed);
    Result UseChunk(Ticket operation, std::size_t chunk);
    Result BeginCPUAccess(std::size_t chunk);
    Result PrepareCPUStart(std::size_t chunk, bool barrier, bool invalidated);
    Result StartCPUAccess(std::size_t chunk, Ack ack);
    void SetNonCoherent(std::size_t chunk, bool value);
    void Inject(Fault fault);
    Phase Status(Ticket ticket) const;
    Access ChunkStatus(std::size_t chunk) const;
    Id VersionClock() const;
    Bytes ReplicaBytes(Id representation) const;
    std::vector<Id> ReplicaVersions(Id representation) const;
    std::vector<CellInfo> Cells() const;
    Metrics Statistics() const;
    std::string Fingerprint() const; // semantic state excluding diagnostics/fault injector
    void CheckInvariants() const;

private:
    struct Slice {
        std::size_t physical{}, size{}, logical{}, source_logical{};
        Id version{}, producer{}, holder{};
    };
    struct Cell {
        std::size_t begin{}, end{};
        Id version{}, producer{}, holder{CPU}, prior_version{};
        bool unknown{};
        bool operator==(const Cell&) const = default;
    };
    struct Representation {
        Id epoch{};
        Kind kind{};
        bool codec{true};
        std::size_t size{};
        std::vector<Segment> segments;
        Bytes bytes;
        std::vector<Id> versions;
    };
    struct Operation {
        Id id{};
        Id version{};
        Phase phase{Phase::Accepted};
        Id representation{}, view{}, lease{};
        unsigned queue{};
        bool cpu{};
        bool readonly{};
        bool executed{};
        std::vector<Id> readers;
        std::vector<Segment> read_segments;
        std::vector<Ticket> after;
        std::vector<Segment> segments;
        std::vector<Slice> prior;
        std::size_t size{}, write_offset{};
        Bytes write_data;
        std::vector<std::size_t> chunks;
    };
    struct Lease {
        Id view{};
        bool write{};
        std::vector<Segment> segments;
        std::size_t size{};
        std::vector<Ticket> after;
    };
    struct Chunk {
        Access access{Access::CPUReady};
        bool noncoherent{};
        std::vector<Id> uses;
    };
    struct State {
        Id epoch{}, clock{}, sequence{}, next_view{1}, next_rep{2}, next_lease{1};
        std::size_t size{}, chunk_size{};
        std::map<Id, View> views;
        std::map<Id, Representation> reps;
        std::map<Id, Operation> ops;
        std::map<Id, Lease> leases;
        std::vector<Cell> cells;
        std::vector<Chunk> chunks;
    } state;
    Limits limits;
    mutable Metrics metrics;
    std::optional<Fault> fault;
    template <class F>
    Result Change(F function);
    Error Validate(const State& s, Stamp stamp) const;
    Error Geometry(const State& s, const View& view, bool holes_allowed) const;
    bool Consume(Fault value);
    static bool Overlap(const Segment& a, const Segment& b);
    static bool Terminal(Phase phase);
    static std::optional<std::size_t> Locate(const Representation& r, std::size_t physical);
    static std::vector<Slice> Sources(const State& s, const std::vector<Segment>& segments);
    static Error Ready(const State& s, const std::vector<Slice>& slices, bool cpu);
    static bool Ordered(const State& s, Id predecessor, const std::vector<Ticket>& after);
    static bool Intersects(const std::vector<Segment>& a, const std::vector<Segment>& b);
    Error Admit(const State& s, const std::vector<Segment>& segments,
                const std::vector<Ticket>& after, Id own_lease = 0, bool write = true) const;
    void Split(State& s, std::size_t position);
    void Coalesce(State& s);
    static bool SameReplicaBoundary(const State& s, std::size_t boundary);
    void Assign(State& s, const std::vector<Segment>& segments, Id version, Id producer, Id holder);
    static Error CopyLatest(State& s, Id representation, bool cpu);
    static Error CopySegments(State& s, Id representation, const std::vector<Segment>& segments,
                              bool cpu);
    static bool RepInUse(const State& s, Id representation);
    static bool ViewInUse(const State& s, Id view);
    static void Quarantine(State& s, Operation& operation);
    static Error Preserve(State& s, Id representation);
    static Metrics Measure(const State& s);
};
const char* Name(Error error);
} // namespace Coherency

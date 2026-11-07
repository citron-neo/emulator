# Deferred NVCTRL QueryEvent slot decoding investigation

Recorded 2026-10-26 while investigating the FFR NVN interrupt wait failure.
This is a separate unresolved issue. No slot-decoding change is included in the
post-wake rearm fix (`a5907dc1e8`), and no Android/device fix is claimed here.

## Local producer/consumer mismatch

`nvhost_ctrl::SyncpointEventValue` declares two overlapping layouts:

- `partial_slot`: bits 0-3, with `syncpoint_id` in bits 4-31;
- `slot`: bits 0-15, with allocation syncpoint identity in bits 16-27 and
  `event_allocated` in bit 28.

`QueryEvent` currently selects `partial_slot` when `event_allocated` is set and
the full `slot` otherwise. The captured runtime's cached event path has 64
entries and queries an explicitly registered event using `0x10000000 | slot`.
For that format, selecting four bits truncates slots 16-63:

| Query ID | Requested slot | Current selected slot |
| --- | --- | --- |
| `0x10000000` | 0 | 0 |
| `0x1000000f` | 15 | 15 |
| `0x10000010` | 16 | 0 |
| `0x1000001f` | 31 | 15 |
| `0x1000003f` | 63 | 15 |

Minimal check using the actual public union and current selection expression:

```cpp
nvhost_ctrl::SyncpointEventValue id{.raw = 0x10000010};
const u32 selected = id.event_allocated.Value() ? id.partial_slot.Value()
                                               : static_cast<u32>(id.slot);
// selected == 0; the runtime requested registered slot 16.
```

The reverse format also needs an audit: `IocCtrlEventWait` without allocation
sets the bits-4-and-up syncpoint ID before OR-ing in the slot. Reading that
format as a full 16-bit slot can include syncpoint bits. Do not fix just one
field width without checking both local producer/consumer layouts, QueryEvent
callers, and ClearEventWait compatibility.

## Controlled source-level observation

An isolated test extracted the actual personal-main QueryEvent function and
union, with a controlled event table and Release-style `NDEBUG` assertions:

1. Register distinct events at slots 0 and 16, both with assigned syncpoint 0.
   `QueryEvent(0x10000010)` returns event 0 rather than event 16.
2. Set slot 0's assigned syncpoint to 1 while slot 16 remains assigned to 0.
   The same query returns null. `Module::QueryEvent` maps null to BadParameter.

This PR has the same selector. Its assertion policy can instead stop at the
mismatched-slot assertion; the exact Release null-return outcome above was
verified with personal-main's actual function. Kernel event identity, scheduling,
full IPC and Android execution were not exercised by that isolated test.

## Relation to the FFR report

The saved random-encounter freeze had cached event handles only in slots 0 and 1
and no `Invalid event request` entry in its captured log. The evidence does not
connect this high-slot defect to that occurrence. NVN FAILED(3) also does not
identify the underlying NvResult by itself. Keep this issue separate from the
confirmed post-wake/pre-Signalled rearm window and from shader ATOM_cas support.

## Suggested continuation

- Establish both ioctl/QueryEvent ID formats from their actual writers/callers.
- Add round-trip cases for slots 0, 15, 16, 31, 63, nonzero syncpoint IDs, and
  invalid/unregistered identities; check returned KEvent identity, not only status.
- Verify allocation and explicit-registration paths separately, plus readable
  handles retained across rearm/cancellation and slot replacement.
- Require invalid IDs to remain rejected and retain callback joins/lifetime
  ownership. Do not replace a mismatch with an arbitrary event or success.
- Validate the eventual correction offline before a user-requested device build.

No workflow was dispatched for this note or the rearm backport.

# MMIO write safety model

Prototype 1 performs no MMIO writes.

Later NVIDIA bring-up stages inevitably need to write registers for Falcon/GSP reset, mailboxes, queues, MMU state, and interrupts. RTXMac will not expose a generic `write32(offset, value)` path to userspace and hope for the best.

## Policy model

Every future write sequence is first expressed as a **dry-run plan** in the portable core.

Each audited register gets an explicit rule:

```text
register offset + writable bit mask
```

A planned write is allowed only when:

1. the register offset exists in the stage's allow-list, and
2. every bit changed by the write is inside that register's audited writable mask.

The default policy contains **zero rules**, which means all writes are denied.

## Why masks matter

GPU registers often mix writable control bits with reserved/status fields. An offset-only allow-list is not enough: a bad full-width write could still modify fields we never intended to touch. Comparing the old and new values lets the policy reject any changed bit outside the audited mask.

## Development flow

1. Build a GSP/Falcon sequence as a dry-run on Windows.
2. Compare every planned write against NVIDIA's published register definitions and TinyGPU/Nouveau/OpenRM behavior.
3. Add the narrowest required mask to the stage-specific policy.
4. Unit-test both permitted and forbidden mutations.
5. Only after the complete stage is reviewed do we connect that stage to a real hardware backend.

This policy does not make arbitrary GPU programming inherently safe, but it makes accidental scope expansion visible and testable before a Hackintosh boot.

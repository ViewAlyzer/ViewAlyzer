"""
viewalyzer.protocol_rtos — RTOS extension events for the VA protocol.

Adds TaskSwitch, ISR, TaskCreate, TaskNotify, Semaphore, Mutex, Queue,
TaskStackUsage, and MutexContention event builders on top of the core
protocol.  These events populate the **timeline / swimlane** view in
ViewAlyzer and require task / ISR / sync-object setup packets.

Import this module explicitly when your target firmware has an RTOS::

    from viewalyzer.protocol_rtos import (
        build_task_map, build_task_switch, build_isr_event, ...
    )
"""

import struct
from .protocol import FLAG_START, _ts_mask

# ── RTOS event type codes ───────────────────────────────────────────────────

EVT_TASK_SWITCH      = 0x01
EVT_ISR              = 0x02
EVT_TASK_CREATE      = 0x03
EVT_TASK_NOTIFY      = 0x05
EVT_SEMAPHORE        = 0x06
EVT_MUTEX            = 0x07
EVT_QUEUE            = 0x08
EVT_TASK_STACK_USAGE = 0x09
EVT_MUTEX_CONTENTION = 0x0C

# ── RTOS setup packet codes ─────────────────────────────────────────────────

SETUP_TASK_MAP      = 0x70
SETUP_ISR_MAP       = 0x71
SETUP_SEMAPHORE_MAP = 0x73
SETUP_MUTEX_MAP     = 0x74
SETUP_QUEUE_MAP     = 0x75


# ── RTOS setup packet builders ──────────────────────────────────────────────

def build_task_map(task_id: int, name: str) -> bytes:
    """Setup::TaskMap — map a task ID (0–255) to a name."""
    nb = name.encode("ascii")
    return struct.pack("BBB", SETUP_TASK_MAP, task_id, len(nb)) + nb


def build_isr_map(isr_id: int, name: str) -> bytes:
    """Setup::ISRMap — map an ISR ID (0–255) to a name."""
    nb = name.encode("ascii")
    return struct.pack("BBB", SETUP_ISR_MAP, isr_id, len(nb)) + nb


def build_semaphore_map(sem_id: int, name: str) -> bytes:
    """Setup::SemaphoreMap — map a semaphore ID to a name."""
    nb = name.encode("ascii")
    return struct.pack("BBB", SETUP_SEMAPHORE_MAP, sem_id, len(nb)) + nb


def build_mutex_map(mutex_id: int, name: str) -> bytes:
    """Setup::MutexMap — map a mutex ID to a name."""
    nb = name.encode("ascii")
    return struct.pack("BBB", SETUP_MUTEX_MAP, mutex_id, len(nb)) + nb


def build_queue_map(queue_id: int, name: str) -> bytes:
    """Setup::QueueMap — map a queue ID to a name."""
    nb = name.encode("ascii")
    return struct.pack("BBB", SETUP_QUEUE_MAP, queue_id, len(nb)) + nb


# ── RTOS event packet builders ──────────────────────────────────────────────

def build_task_switch(task_id: int, is_enter: bool, timestamp: int) -> bytes:
    """TaskSwitch (0x01) — 10 bytes."""
    tb = EVT_TASK_SWITCH | (FLAG_START if is_enter else 0)
    return struct.pack("<BBQ", tb, task_id, _ts_mask(timestamp))


def build_isr_event(isr_id: int, is_enter: bool, timestamp: int) -> bytes:
    """ISR (0x02) — 10 bytes."""
    tb = EVT_ISR | (FLAG_START if is_enter else 0)
    return struct.pack("<BBQ", tb, isr_id, _ts_mask(timestamp))


def build_task_create(task_id: int, timestamp: int,
                      priority: int, base_priority: int, stack_size: int) -> bytes:
    """TaskCreate (0x03) — 22 bytes."""
    tb = EVT_TASK_CREATE | FLAG_START
    return struct.pack("<BBQiii", tb, task_id, _ts_mask(timestamp),
                       priority, base_priority, stack_size)


def build_task_notify(src_id: int, dst_id: int, timestamp: int, value: int) -> bytes:
    """TaskNotify (0x05) — 15 bytes."""
    tb = EVT_TASK_NOTIFY | FLAG_START
    return struct.pack("<BBBQi", tb, src_id, dst_id, _ts_mask(timestamp), value)


def build_semaphore(sem_id: int, is_give: bool, timestamp: int) -> bytes:
    """Semaphore (0x06) — 10 bytes."""
    tb = EVT_SEMAPHORE | (FLAG_START if is_give else 0)
    return struct.pack("<BBQ", tb, sem_id, _ts_mask(timestamp))


def build_mutex(mutex_id: int, is_acquire: bool, timestamp: int) -> bytes:
    """Mutex (0x07) — 10 bytes."""
    tb = EVT_MUTEX | (FLAG_START if is_acquire else 0)
    return struct.pack("<BBQ", tb, mutex_id, _ts_mask(timestamp))


def build_queue(queue_id: int, is_send: bool, timestamp: int) -> bytes:
    """Queue (0x08) — 10 bytes."""
    tb = EVT_QUEUE | (FLAG_START if is_send else 0)
    return struct.pack("<BBQ", tb, queue_id, _ts_mask(timestamp))


def build_task_stack_usage(task_id: int, timestamp: int,
                           used: int, total: int) -> bytes:
    """TaskStackUsage (0x09) — 18 bytes."""
    return struct.pack("<BBQII", EVT_TASK_STACK_USAGE, task_id,
                       _ts_mask(timestamp), used, total)


def build_mutex_contention(mutex_id: int, wait_task_id: int,
                           holder_task_id: int, timestamp: int) -> bytes:
    """MutexContention (0x0C) — 12 bytes."""
    return struct.pack("<BBBBQ", EVT_MUTEX_CONTENTION, mutex_id,
                       wait_task_id, holder_task_id, _ts_mask(timestamp))

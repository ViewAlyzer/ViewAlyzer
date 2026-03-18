"""
viewalyzer.sender_rtos — RTOS-extended ViewAlyzer UDP sender.

Subclasses the core ``ViewAlyzerSender`` and adds methods for
RTOS-specific events: TaskSwitch, ISR, TaskCreate, TaskNotify,
Semaphore, Mutex, Queue, TaskStackUsage, and MutexContention.

Usage::

    from viewalyzer.sender_rtos import ViewAlyzerRtosSender

    va = ViewAlyzerRtosSender("127.0.0.1", 17200, cpu_freq=170_000_000)
    va.send_task_map(0, "MainTask")
    va.send_task_switch(0, enter=True, timestamp=ts)
    va.send_trace_float(0, ts, 23.5)   # core methods still available
    va.close()
"""

from viewalyzer.sender import ViewAlyzerSender
from viewalyzer.protocol_rtos import (
    build_task_map, build_isr_map,
    build_semaphore_map, build_mutex_map, build_queue_map,
    build_task_switch, build_isr_event, build_task_create,
    build_task_notify, build_semaphore, build_mutex, build_queue,
    build_task_stack_usage, build_mutex_contention,
)


class ViewAlyzerRtosSender(ViewAlyzerSender):
    """COBS-framed UDP sender with full RTOS support.

    Inherits all core tracing methods from ``ViewAlyzerSender`` and adds
    RTOS-specific events and setup packets.
    """

    # ── RTOS setup packets ───────────────────────────────────────────────

    def send_task_map(self, task_id: int, name: str) -> None:
        """Map a task ID to a human-readable name."""
        self.send_framed(build_task_map(task_id, name))

    def send_isr_map(self, isr_id: int, name: str) -> None:
        """Map an ISR ID to a human-readable name."""
        self.send_framed(build_isr_map(isr_id, name))

    def send_semaphore_map(self, sem_id: int, name: str) -> None:
        self.send_framed(build_semaphore_map(sem_id, name))

    def send_mutex_map(self, mutex_id: int, name: str) -> None:
        self.send_framed(build_mutex_map(mutex_id, name))

    def send_queue_map(self, queue_id: int, name: str) -> None:
        self.send_framed(build_queue_map(queue_id, name))

    # ── RTOS event packets ───────────────────────────────────────────────

    def send_task_switch(self, task_id: int, enter: bool, timestamp: int) -> None:
        self.send_framed(build_task_switch(task_id, enter, self._ts(timestamp)))

    def send_isr(self, isr_id: int, enter: bool, timestamp: int) -> None:
        self.send_framed(build_isr_event(isr_id, enter, self._ts(timestamp)))

    def send_task_create(self, task_id: int, timestamp: int,
                         priority: int, base_priority: int, stack_size: int) -> None:
        self.send_framed(build_task_create(task_id, self._ts(timestamp),
                                           priority, base_priority, stack_size))

    def send_task_notify(self, src_id: int, dst_id: int,
                         timestamp: int, value: int) -> None:
        self.send_framed(build_task_notify(src_id, dst_id, self._ts(timestamp), value))

    def send_semaphore(self, sem_id: int, is_give: bool, timestamp: int) -> None:
        self.send_framed(build_semaphore(sem_id, is_give, self._ts(timestamp)))

    def send_mutex(self, mutex_id: int, is_acquire: bool, timestamp: int) -> None:
        self.send_framed(build_mutex(mutex_id, is_acquire, self._ts(timestamp)))

    def send_queue(self, queue_id: int, is_send: bool, timestamp: int) -> None:
        self.send_framed(build_queue(queue_id, is_send, self._ts(timestamp)))

    def send_stack_usage(self, task_id: int, timestamp: int,
                         used: int, total: int) -> None:
        self.send_framed(build_task_stack_usage(task_id, self._ts(timestamp), used, total))

    def send_mutex_contention(self, mutex_id: int, waiting_task_id: int,
                              holder_task_id: int, timestamp: int) -> None:
        self.send_framed(build_mutex_contention(mutex_id, waiting_task_id,
                                                holder_task_id, self._ts(timestamp)))

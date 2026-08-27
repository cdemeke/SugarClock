"""Bounded background geolocation with deduplication and a provider circuit breaker."""

import queue
import sqlite3
import threading
import time

from .geolocation import lookup_approximate_location


class LocationWorker:
    def __init__(self, database, *, queue_size=128, synchronous=False):
        self.database = database
        self.synchronous = synchronous
        self.jobs = queue.Queue(maxsize=queue_size)
        self.pending = set()
        self.lock = threading.Lock()
        self.consecutive_failures = 0
        self.circuit_until = 0.0
        if not synchronous:
            threading.Thread(target=self._run, name="fleet-geolocation", daemon=True).start()

    def submit(self, device_id, source_ip, checked_at):
        with self.lock:
            if device_id in self.pending:
                return False
            if time.monotonic() < self.circuit_until:
                return False
            self.pending.add(device_id)
        job = (device_id, source_ip, checked_at)
        if self.synchronous:
            self._process(job)
            return True
        try:
            self.jobs.put_nowait(job)
            return True
        except queue.Full:
            with self.lock:
                self.pending.discard(device_id)
            return False

    def _run(self):
        while True:
            job = self.jobs.get()
            try:
                self._process(job)
            except Exception:
                # A provider or database failure must not kill the only worker.
                # The next successful job will close the provider circuit.
                pass
            finally:
                self.jobs.task_done()

    def _process(self, job):
        device_id, source_ip, checked_at = job
        location = None
        try:
            if time.monotonic() >= self.circuit_until:
                try:
                    location = lookup_approximate_location(source_ip)
                except Exception:
                    location = None
            with self.lock:
                if location:
                    self.consecutive_failures = 0
                    self.circuit_until = 0.0
                else:
                    self.consecutive_failures += 1
                    if self.consecutive_failures >= 3:
                        self.circuit_until = time.monotonic() + 10 * 60
            connection = sqlite3.connect(self.database, timeout=5)
            try:
                connection.execute("PRAGMA busy_timeout = 5000")
                if location:
                    connection.execute(
                        "UPDATE devices SET detected_city=?, detected_region=?, detected_country_code=?, "
                        "detected_location_checked_at=? WHERE id=?",
                        (location["city"], location["region"], location["country_code"], checked_at, device_id),
                    )
                else:
                    connection.execute(
                        "UPDATE devices SET detected_location_checked_at=? WHERE id=?",
                        (checked_at, device_id),
                    )
                connection.commit()
            finally:
                connection.close()
        finally:
            with self.lock:
                self.pending.discard(device_id)


def init_app(app):
    app.extensions["location_worker"] = LocationWorker(
        app.config["DATABASE"],
        queue_size=app.config["IP_GEOLOCATION_QUEUE_SIZE"],
        synchronous=app.config.get("IP_GEOLOCATION_SYNCHRONOUS", False),
    )

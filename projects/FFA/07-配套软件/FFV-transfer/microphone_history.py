"""Permanent second statistics of received microphones; never stores speech PCM.

Network callbacks only accumulate numbers. A worker commits deltas every five
seconds, retaining pending deltas on disk errors. SQLite has no retention cutoff.
"""
import math
import sqlite3
import threading
import time
from contextlib import closing
from pathlib import Path


class MicrophoneHistory:
    def __init__(self, path):
        self.path = Path(path)
        self.lock = threading.Lock()
        self.flush_lock = threading.Lock()
        self.pending = {}
        self.error = ''
        self.last_received = None
        self.stop = threading.Event()
        self.thread = None

    def initialize(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with closing(sqlite3.connect(self.path)) as db, db:
            db.execute('PRAGMA journal_mode=WAL')
            for table in ('seconds', 'minutes'):
                db.execute(f'''CREATE TABLE IF NOT EXISTS {table} (
                    bucket INTEGER NOT NULL, gain INTEGER NOT NULL,
                    samples INTEGER NOT NULL, seconds REAL NOT NULL,
                    left_energy REAL NOT NULL, right_energy REAL NOT NULL,
                    left_max REAL NOT NULL, right_max REAL NOT NULL,
                    first REAL NOT NULL, last REAL NOT NULL,
                    PRIMARY KEY (bucket, gain))''')

    def start(self):
        self.initialize()
        if self.thread is None:
            self.thread = threading.Thread(target=self.run, daemon=True, name='ffv-mic-history')
            self.thread.start()

    def offer(self, left_db, right_db, samples, rate, gain, timestamp=None):
        timestamp = time.time() if timestamp is None else timestamp
        if samples <= 0 or rate <= 0 or not all(math.isfinite(x) for x in (timestamp, left_db, right_db)):
            return
        key = (int(timestamp), int(gain))
        delta = [samples, samples / rate, 10 ** (left_db / 10) * samples,
                 10 ** (right_db / 10) * samples, left_db, right_db, timestamp, timestamp]
        with self.lock:
            self.merge(self.pending, key, delta)
            self.last_received = timestamp

    @staticmethod
    def merge(target, key, delta):
        if key not in target:
            target[key] = list(delta)
            return
        current = target[key]
        for i in range(4):
            current[i] += delta[i]
        current[4] = max(current[4], delta[4])
        current[5] = max(current[5], delta[5])
        current[6] = min(current[6], delta[6])
        current[7] = max(current[7], delta[7])

    def flush(self):
        with self.flush_lock:
            with self.lock:
                batch, self.pending = self.pending, {}
            if not batch:
                return
            try:
                minutes = {}
                for (second, gain), delta in batch.items():
                    self.merge(minutes, (second // 60 * 60, gain), delta)
                with closing(sqlite3.connect(self.path, timeout=2)) as db, db:
                    # Raw seconds and query rollups commit together; neither is pruned.
                    for table, values in (('seconds', batch), ('minutes', minutes)):
                        db.executemany(f'''INSERT INTO {table} VALUES (?,?,?,?,?,?,?,?,?,?)
                            ON CONFLICT(bucket,gain) DO UPDATE SET
                            samples=samples+excluded.samples, seconds=seconds+excluded.seconds,
                            left_energy=left_energy+excluded.left_energy,
                            right_energy=right_energy+excluded.right_energy,
                            left_max=MAX(left_max,excluded.left_max),
                            right_max=MAX(right_max,excluded.right_max),
                            first=MIN(first,excluded.first), last=MAX(last,excluded.last)''',
                            [(*key, *value) for key, value in values.items()])
                self.error = ''
            except (OSError, sqlite3.Error) as exc:
                with self.lock:
                    for key, delta in batch.items():
                        self.merge(self.pending, key, delta)
                self.error = str(exc)

    def run(self):
        while not self.stop.wait(5):
            self.flush()

    def close(self):
        self.stop.set()
        if self.thread:
            self.thread.join(3)
        self.flush()

    def snapshot(self):
        return dict(error=self.error, last_received=self.last_received, interval_seconds=1)

    def query(self, span='24h', end=None):
        if span not in ('5m', '1h', '24h', '7d', '30d', 'all'):
            raise ValueError('时间范围无效')
        end = time.time() if end is None else float(end)
        if not math.isfinite(end) or end < 0 or end > 32503680000:
            raise ValueError('结束时间无效')
        # Read only: queries never block the audio producer or trigger disk writes.
        with closing(sqlite3.connect(self.path, timeout=2)) as db:
            first_row = db.execute('SELECT first FROM seconds ORDER BY bucket LIMIT 1').fetchone()
            first = first_row[0] if first_row else None
            durations = {'5m': 300, '1h': 3600, '24h': 86400, '7d': 604800, '30d': 2592000}
            start = (first if first is not None else end - 3600) if span == 'all' else end - durations[span]
            start = min(start, end)
            step = max(1, math.ceil((end - start) / 480))
            if step >= 60:
                step = math.ceil(step / 60) * 60
            table = 'minutes' if step >= 60 else 'seconds'
            resolution = 60 if table == 'minutes' else 1
            start = math.floor(start / resolution) * resolution
            data = db.execute(f'''SELECT (bucket / ?) * ? AS plot_bucket,
                SUM(samples), SUM(seconds), SUM(left_energy), SUM(right_energy),
                MAX(left_max), MAX(right_max), MIN(first), MAX(last), GROUP_CONCAT(DISTINCT gain)
                FROM {table} WHERE bucket >= ? AND bucket < ?
                GROUP BY plot_bucket ORDER BY plot_bucket''', (step, step, start, end)).fetchall()
        rows = []
        for bucket, samples, seconds, left, right, lmax, rmax, began, ended, gains in data:
            rows.append(dict(time=bucket, left=round(10 * math.log10(max(left / samples, 1e-10)), 1),
                             right=round(10 * math.log10(max(right / samples, 1e-10)), 1),
                             left_max=lmax, right_max=rmax, seconds=round(seconds, 2),
                             first=began, last=ended, gains=sorted(map(int, gains.split(',')))))
        return dict(rows=rows, start=start, end=end, step=step, first_recorded=first,
                    unit='dBFS', resolution_seconds=1, **self.snapshot())


def register_history_routes(app, history):
    from fastapi import HTTPException

    @app.get('/v1/audio/history')
    def microphone_history(span: str = '24h', end: float | None = None):
        try:
            return history.query(span, end)
        except ValueError as exc:
            raise HTTPException(400, str(exc)) from exc
        except sqlite3.Error as exc:
            raise HTTPException(503, '麦克风历史暂时无法读取') from exc

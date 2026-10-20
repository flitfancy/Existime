"""Bounded local voice event journal with explicit reconnect boundaries."""
from collections import deque
import json
import threading
import time
import uuid


class VoiceEvents:
    def __init__(self, capacity=256):
        self.epoch = uuid.uuid4().hex
        self.sequence = 0
        self.events = deque(maxlen=capacity)
        self.lock = threading.Lock()

    def cursor(self):
        with self.lock:return f'{self.epoch}:{self.sequence}'

    @staticmethod
    def parse(cursor):
        try:
            epoch, number = cursor.split(':')
            if len(epoch)!=32 or any(c not in '0123456789abcdef' for c in epoch):raise ValueError()
            if not number.isdigit() or len(number)>20:raise ValueError()
            return epoch,int(number)
        except (ValueError,AttributeError):raise ValueError('Invalid voice event cursor') from None

    def publish(self, kind, payload):
        # Serialize a copy: later session mutations cannot rewrite a published event.
        data = json.loads(json.dumps(payload,ensure_ascii=False))
        with self.lock:
            self.sequence += 1
            event = dict(id=f'{self.epoch}:{self.sequence}',type=kind,time=time.time(),data=data)
            self.events.append(event)
            return event['id']

    def read_after(self, cursor):
        epoch, sequence = self.parse(cursor)
        with self.lock:
            current = f'{self.epoch}:{self.sequence}'
            oldest = self.sequence-len(self.events)+1
            reason = ('service-restarted' if epoch!=self.epoch else
                      'cursor-ahead' if sequence>self.sequence else
                      'history-expired' if sequence<oldest-1 else '')
            if reason:
                # Never silently replay an arbitrary old command after history loss.
                return [dict(id=current,type='stream.reset',time=time.time(),data={'reason':reason})],current
            batch = [event for event in self.events if int(event['id'].split(':')[1])>sequence]
            return batch,current

    @staticmethod
    def encode(event):
        body=json.dumps(event,ensure_ascii=False,separators=(',',':'))
        return f"id: {event['id']}\nevent: {event['type']}\ndata: {body}\n\n"

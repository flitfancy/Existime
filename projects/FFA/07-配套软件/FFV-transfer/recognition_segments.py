"""Bounded speech utterances from processed PCM and channel VAD probabilities."""
from collections import deque
import numpy as np


class UtteranceBuffer:
    rate=16000
    pre_roll=32000      # Far-field/AEC onsets can precede confident VAD by >1 s.
    continuation=4800  # Context overlap only when a long utterance hits its cap.
    onset=1600         # Confirm 100 ms of speech.
    end_hold=12800     # End after 800 ms below the VAD release threshold.
    maximum=240000    # Bound continuous speech at 15 seconds.

    def __init__(self,channels,selected):
        self.channels=channels;self.selected=selected
        self.clear()

    def clear(self):
        self.packets=deque();self.frames=0
        self.active=False;self.positive=0;self.silent=0

    def push(self,payload,probabilities,playback):
        frames=len(payload)//(2*self.channels)
        if frames!=160:
            raise ValueError('utterance input must be a 10 ms PCM16 frame')
        probabilities=np.asarray(probabilities,dtype=np.float64)
        probability=(max(probabilities) if self.selected=='both' else
                     probabilities[0 if self.selected=='left' else 1])
        self.packets.append((payload,probabilities.copy(),bool(playback),frames))
        self.frames+=frames
        if not self.active:
            while self.frames>self.pre_roll:
                self.frames-=self.packets.popleft()[3]
            self.positive=self.positive+frames if probability>=.5 else 0
            if self.positive>=self.onset:self.active=True
            return None
        self.silent=self.silent+frames if probability<.35 else 0
        if self.silent>=self.end_hold:
            return self.finish('silence')
        if self.frames>=self.maximum:
            tail=list(self.packets)[-self.continuation//160:]
            result=self.finish('limit')
            # Preserve a short context for the following part of long speech.
            self.packets.extend(tail);self.frames=sum(row[3] for row in tail)
            return result
        return None

    def finish(self,reason='stop'):
        result=None
        if self.active and self.frames>=3200:
            result=(b''.join(row[0] for row in self.packets),
                    np.sum([row[1]*row[3] for row in self.packets],axis=0),
                    any(row[2] for row in self.packets))
        self.clear()
        self.last_reason=reason
        return result

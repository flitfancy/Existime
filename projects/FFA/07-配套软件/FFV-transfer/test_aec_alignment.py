"""Replay measured correlation jitter and distinguish it from a real clock move."""
from aec_runtime import ReferenceAlignment


def main():
    tracker=ReferenceAlignment()
    # Captured real-music estimates: startup outlier, stable path, a brief
    # reflection peak, then 3 ms excursions during the lost user utterance.
    estimates=[111932,112240,112241,112240,112716,112241,112240,112290,112290,112242]
    changes=[estimate for estimate in estimates if tracker.observe(estimate)]
    assert len(changes)==1, ('correlation jitter restarted the filter',changes)
    assert abs(tracker.offset-112240)<=1
    # A persistent 30 ms clock move is accepted after three consecutive matches.
    assert not tracker.observe(112720)
    assert not tracker.observe(112724)
    assert tracker.observe(112721)
    assert abs(tracker.offset-112721)<=2
    # Loss of a qualified peak breaks a pending change; old evidence cannot vote.
    tracker.observe(113200);tracker.observe(113201);tracker.miss()
    assert not tracker.observe(113200)
    tracker.clear()
    assert tracker.offset is None
    assert not tracker.observe(200)
    assert tracker.observe(201)
    print('measured jitter, sustained clock move, lost confidence and reset passed')


if __name__=='__main__':main()

import queue
import unittest
from unittest.mock import patch

import numpy as np

import app
from protocol_v2 import FrameType
from voice_turn import VoiceTurn, wake_chime
from output_scheduler import OutputScheduler, PCMSource


class VoiceTurnTests(unittest.TestCase):
    def setUp(self):
        self.now = 100.
        self.turn = VoiceTurn('test', clock=lambda:self.now)
        self.patches = [patch.dict(app.state, recording=False, playing=False, model='ready', mic_mode='both'),
                        patch.object(app, 'jobs', queue.Queue(maxsize=8)),
                        patch.object(app, 'output_scheduler', OutputScheduler(app.state)),
                        patch.object(app, 'active_voice_turn', None)]
        for item in self.patches:item.start()
        app.output_scheduler.connected()

    def tearDown(self):
        for item in reversed(self.patches):item.stop()
        app.capture_stop.clear()

    def test_silence_returns_to_waiting_without_stt(self):
        self.now += 7.9
        self.assertEqual(self.turn.stop_reason(), '')
        self.now += .2
        self.assertEqual(self.turn.stop_reason(), 'no-speech')
        self.turn.close(self.turn.stop_reason())
        self.turn.finalize()
        self.assertEqual(self.turn.snapshot()['phase'], 'timeout')
        self.assertTrue(app.jobs.empty())

    def test_wait_budget_starts_after_cue_and_cue_cannot_be_recognized(self):
        self.turn.cue_started()
        self.assertFalse(self.turn.allows_audio())
        self.now += 2
        self.turn.cue_finished()
        self.now += 7.9
        self.assertEqual(self.turn.stop_reason(), '')
        self.now += .2
        self.assertEqual(self.turn.stop_reason(), 'no-speech')

    def test_real_capture_ends_one_utterance_and_tracks_async_result(self):
        capture = app.MicCapture(45, wake=True);capture.first=0
        capture.turn.cue_started()
        voice = np.random.default_rng(42).normal(0,.02,(160,2)).astype(np.float32)
        for seq in range(30):capture.process_clean(seq, voice, [.95,.2], False)
        self.assertFalse(capture.utterance.active)
        capture.turn.cue_finished()
        for seq in range(30,110):capture.process_clean(seq, voice, [.95,.2], False)
        for seq in range(110,195):capture.process_clean(seq,np.zeros((160,2),np.float32),[0.,0.],False)
        self.assertTrue(capture.should_stop())
        self.assertEqual(app.jobs.qsize(), 1)
        job = app.jobs.get_nowait()
        self.assertIs(job[6], capture.turn)
        capture.turn.close('utterance')
        capture.turn.finalize()
        self.assertEqual(capture.turn.snapshot()['phase'], 'processing')
        job[6].result('现在测试语音交互。')
        self.assertEqual(capture.turn.snapshot()['phase'], 'complete')
        self.assertEqual(capture.turn.snapshot()['text'], '现在测试语音交互。')

    def test_manual_stop_drains_pending_audio_before_finish(self):
        self.turn.close('manual')
        self.assertTrue(self.turn.allows_audio())
        self.turn.queued()
        self.assertEqual(self.turn.snapshot()['phase'], 'processing')
        self.turn.result('尾句')
        self.turn.finalize()
        self.assertEqual(self.turn.snapshot()['text'], '尾句')

    def test_continuous_manual_capture_has_no_automatic_turn(self):
        capture=app.MicCapture(600)
        self.assertIsNone(capture.turn)
        self.assertFalse(capture.should_stop())

    def test_long_command_cap_then_silence_and_hard_limit(self):
        self.turn.observe(True)
        self.now += .9
        self.assertEqual(self.turn.stop_reason(), '')
        self.now += .4
        self.assertEqual(self.turn.stop_reason(), 'utterance')
        self.now += 45
        self.assertEqual(self.turn.stop_reason(), 'limit')

    def test_queue_failure_does_not_leave_processing_stuck(self):
        for _ in range(8):app.jobs.put(None)
        pcm=np.random.default_rng(1).integers(-4000,4000,(3200,2),dtype=np.int16).tobytes()
        self.assertFalse(app.enqueue('test',0,pcm,2,False,16000,[1.,0.],self.turn))
        self.turn.close('utterance')
        self.turn.finalize()
        self.assertEqual(self.turn.snapshot()['phase'],'error')
        self.assertEqual(self.turn.snapshot()['pending'],0)

    def test_music_has_priority_and_cue_never_lingers(self):
        music=app.output_scheduler.reserve('file','music')
        app.request_wake_feedback(self.turn)
        self.assertEqual(self.turn.snapshot()['cue'],'visual')
        app.output_scheduler.cancel(music)
        app.request_wake_feedback(self.turn)
        self.assertTrue(app.output_scheduler.busy)
        app.output_scheduler.reserve('file','next music')
        self.assertEqual(self.turn.snapshot()['phase'],'waiting-speech')
        self.assertEqual(app.state['play_name'],'next music')

    def test_cue_uses_existing_framing_and_exact_reference_pcm(self):
        class Connection:
            def __init__(self):self.frames=[]
            def send_frame(self, kind, body=b'', flags=0):
                self.frames.append((kind,body,flags));return len(self.frames)
        app.request_wake_feedback(self.turn)
        session=app.DeviceSession();session.connection=Connection()
        with patch.object(app.aec_runtime.reference, 'append') as reference:
            session.start_next_output()
            self.assertEqual(session.active.kind,'cue')
            session.active.ready=True;session.active.capacity=96000
            for _ in range(5):session.active.drive()
            bodies=[row[1] for row in session.connection.frames if row[0]==FrameType.AUDIO_DATA]
            self.assertEqual(b''.join(bodies),wake_chime())
            self.assertEqual([call.args[0] for call in reference.call_args_list],bodies)
            self.assertTrue(session.active.end_sent)
            session.active.finish('播放完成')
        self.assertFalse(app.state['playing'])
        self.assertEqual(self.turn.snapshot()['phase'],'waiting-speech')
        samples=np.frombuffer(wake_chime(),dtype='<i2')
        self.assertLessEqual(np.abs(samples).max(),1967)
        self.assertTrue(np.all(samples[-4000:]==0))


if __name__=='__main__':unittest.main()

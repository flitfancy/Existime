import json
import unittest

from voice_events import VoiceEvents
from voice_turn import VoiceTurn


class VoiceEventTests(unittest.TestCase):
    def test_reconnect_replays_only_unseen_events(self):
        journal=VoiceEvents()
        first=journal.publish('transcript.final',{'text':'第一句'})
        second=journal.publish('transcript.final',{'text':'第二句'})
        events,cursor=journal.read_after(first)
        self.assertEqual([event['id'] for event in events],[second])
        self.assertEqual(journal.read_after(cursor)[0],[])

    def test_lost_history_and_restart_are_explicit(self):
        journal=VoiceEvents(capacity=2);original=journal.cursor()
        for i in range(3):journal.publish('transcript.final',{'text':str(i)})
        events,cursor=journal.read_after(original)
        self.assertEqual(events[0]['type'],'stream.reset')
        self.assertEqual(events[0]['data']['reason'],'history-expired')
        self.assertEqual(journal.read_after(cursor)[0],[])
        events,_=VoiceEvents().read_after(cursor)
        self.assertEqual(events[0]['data']['reason'],'service-restarted')

    def test_newline_is_data_not_a_forged_sse_event(self):
        journal=VoiceEvents();cursor=journal.cursor()
        payload={'text':'你好\nevent: forged\ndata: unexpected'}
        journal.publish('transcript.final',payload);payload['text']='changed'
        events,_=journal.read_after(cursor)
        encoded=journal.encode(events[0])
        self.assertEqual(encoded.count('\nevent:'),1)
        line=next(line[6:] for line in encoded.splitlines() if line.startswith('data: '))
        self.assertEqual(json.loads(line)['data']['text'],'你好\nevent: forged\ndata: unexpected')

    def test_invalid_cursor(self):
        for cursor in ['bad','x:1',None,'0'*32+':-1','0'*32+':1\n']:
            with self.assertRaises(ValueError):VoiceEvents.parse(cursor)

    def test_completed_instruction_waits_for_all_audio_and_asr(self):
        events=[];turn=VoiceTurn('session',on_complete=events.append)
        turn.queued();turn.result('第一段。')
        turn.close('utterance')
        self.assertEqual(events,[])
        # Audio already queued before stop can still generate the final segment.
        turn.queued();turn.finalize()
        self.assertEqual(events,[])
        turn.result('第二段。')
        self.assertEqual(len(events),1)
        self.assertEqual(events[0]['text'],'第一段。第二段。')
        self.assertEqual(events[0]['phase'],'complete')
        turn.finalize();turn.close('utterance')
        self.assertEqual(len(events),1)

    def test_timeout_is_not_an_instruction(self):
        events=[];turn=VoiceTurn('session',on_complete=events.append)
        turn.close('no-speech');turn.finalize()
        self.assertEqual(events[0]['phase'],'timeout')
        self.assertEqual(events[0]['text'],'')


if __name__=='__main__':unittest.main()

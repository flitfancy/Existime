"""Install the local plugin and reuse FFV's ASR endpoint; preserve other settings."""
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import sqlite3

ROOT = Path(__file__).resolve().parent
DATA = Path.home() / '.astrbot' / 'data'


def main():
    config_path = DATA / 'cmd_config.json'
    original = config_path.read_bytes()
    config = json.loads(original.decode('utf-8-sig'))
    providers = [p for p in config['provider'] if p.get('id') == 'sensevoice' and p.get('type') == 'openai_whisper_api']
    if len(providers) != 1:
        raise RuntimeError('Expected the existing SenseVoice provider; no settings changed')
    provider = providers[0]
    if provider.get('api_base') not in ('http://127.0.0.1:7890/v1', 'http://127.0.0.1:7865/v1'):
        raise RuntimeError('SenseVoice endpoint changed; inspect before installing')
    owners = [str(v) for v in config['admins_id'] if str(v).isdigit()]
    platforms = [p for p in config['platform'] if p.get('type') == 'aiocqhttp']
    if len(owners) != 1 or len(platforms) != 1:
        raise RuntimeError('Ambiguous QQ owner/platform; no settings changed')
    umo = f"{platforms[0]['id']}:FriendMessage:{owners[0]}"
    with sqlite3.connect(f'file:{(DATA / "data_v4.db").as_posix()}?mode=ro', uri=True) as db:
        has_selected = db.execute('SELECT count(*) FROM preferences WHERE scope_id=? AND key=?', (umo, 'sel_conv_id')).fetchone()[0]
        conversations = db.execute('SELECT count(*) FROM conversations WHERE user_id=?', (umo,)).fetchone()[0]
    if not has_selected:
        raise RuntimeError('No selected conversation for the inferred owner; confirm binding first')
    plugin = 'astrbot_plugin_firefly_voice'
    destination = DATA / 'plugins' / plugin
    destination.mkdir(exist_ok=True)
    hashes = {}
    for name in ('__init__.py', 'main.py', 'metadata.yaml'):
        source = ROOT / plugin / name
        shutil.copy2(source, destination / name)
        hashes[name] = hashlib.sha256(source.read_bytes()).hexdigest()
    if provider['api_base'] != 'http://127.0.0.1:7865/v1':
        backup = config_path.with_name('cmd_config.json.bak-firefly-voice-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
        backup.write_bytes(original)
        provider['api_base'] = 'http://127.0.0.1:7865/v1'
        temporary = config_path.with_suffix('.ffv.tmp')
        temporary.write_text(json.dumps(config, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        temporary.replace(config_path)
    print(json.dumps({'installed': str(destination), 'selected_conversation_found': bool(has_selected),
                      'conversation_count': conversations, 'stt': 'http://127.0.0.1:7865/v1', 'sha256': hashes}, ensure_ascii=False))


if __name__ == '__main__':
    main()

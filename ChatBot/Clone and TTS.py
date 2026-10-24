import os
import time
import requests

workspace_id = "ws-xxxxxxxxxxxxxx"
target_model = "qwen-audio-3.1-tts-flash"
base_url = f"https://{workspace_id}.cn-beijing.maas.aliyuncs.com/api/v1"
headers = {"Authorization": f"Bearer {os.environ['DASHSCOPE_API_KEY']}"}

response = requests.post(
    f"{base_url}/services/audio/tts/customization",
    headers=headers,
    json={
        "model": "voice-enrollment",
        "input": {
            "action": "create_voice",
            "target_model": target_model,
            "prefix": "myvoice",
            "audio": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        },
    },
    timeout=60,
)
response.raise_for_status()
voice_id = response.json()["output"]["voice_id"]
print(f"voice_id: {voice_id}")

for _ in range(30):
    response = requests.post(
        f"{base_url}/services/audio/tts/customization",
        headers=headers,
        json={
            "model": "voice-enrollment",
            "input": {"action": "query_voice", "voice_id": voice_id},
        },
        timeout=60,
    )
    response.raise_for_status()
    status = response.json()["output"]["status"]
    if status == "OK":
        break
    if status != "DEPLOYING":
        raise RuntimeError(f"Voice status: {status}")
    time.sleep(1)
else:
    raise TimeoutError("Voice is not ready")

response = requests.post(
    f"{base_url}/services/audio/tts/SpeechSynthesizer",
    headers=headers,
    json={
        "model": target_model,
        "input": {
            "text": "今天天气晴朗，适合外出散步。祝你度过愉快的一天。",
            "voice": voice_id,
            "format": "wav",
            "sample_rate": 24000,
        },
    },
    timeout=60,
)
response.raise_for_status()
audio_url = response.json()["output"]["audio"]["url"]
audio_response = requests.get(audio_url, timeout=60)
audio_response.raise_for_status()
output_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "output.wav")
with open(output_path, "wb") as f:
    f.write(audio_response.content)

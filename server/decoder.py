import paho.mqtt.client as mqtt
import json
import base64
import os
import time
import sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend

# Cấu hình
MQTT_BROKER = os.getenv("MQTT_BROKER", "mosquitto")
MQTT_USER = os.getenv("MQTT_USER", "admin")
MQTT_PASS = os.getenv("MQTT_PASS", "123456")
MQTT_TOPIC = "esp32/data"
SHARED_KEY_PATH = "/shared/aes_key.bin"

aes_key = None

def load_key():
    global aes_key
    try:
        if os.path.exists(SHARED_KEY_PATH):
            with open(SHARED_KEY_PATH, "rb") as f:
                aes_key = f.read()
            print("[Decoder] Đã tải AES Key từ file thành công!")
            return True
        else:
            return False
    except Exception as e:
        print(f"[Decoder] Lỗi đọc key: {e}")
        return False

def on_message(client, userdata, msg):
    # Luôn thử tải lại key nếu chưa có (phòng trường hợp server mới tạo xong)
    if aes_key is None:
        if not load_key():
            print("[Decoder] Nhận tin nhắn nhưng chưa có Key. Vui lòng chạy Key Exchange trước.")
            return

    try:
        payload = msg.payload.decode('utf-8')
        data = json.loads(payload)
        
        iv = base64.b64decode(data['iv'])
        tag = base64.b64decode(data['tag'])
        ciphertext = base64.b64decode(data['ciphertext'])

        decryptor = Cipher(
            algorithms.AES(aes_key),
            modes.GCM(iv, tag),
            backend=default_backend()
        ).decryptor()

        plaintext = decryptor.update(ciphertext) + decryptor.finalize()
        
        # === IN RA TERMINAL ===
        print(f"\n[TERMINAL]  GIẢI MÃ: {plaintext.decode('utf-8')}")
        print("------------------------------------------------")

    except Exception as e:
        print(f"[Decoder] Giải mã thất bại: {e}")

def start():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.on_message = on_message

    while True:
        try:
            print(f"[Decoder] Kết nối tới {MQTT_BROKER}...")
            client.connect(MQTT_BROKER, 1883, 60)
            client.subscribe(MQTT_TOPIC)
            client.loop_forever()
        except Exception:
            print("[Decoder] Mất kết nối. Thử lại sau 5s...")
            time.sleep(5)

if __name__ == "__main__":
    print("=== DECODER PROCESS STARTED ===")
    print("Đang đợi AES Key từ Server...")
    start()
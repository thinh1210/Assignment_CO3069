from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
import uvicorn
import paho.mqtt.client as mqtt
import json
import base64
import os
import time

# Crypto imports
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend
from cryptography.exceptions import InvalidTag

app = FastAPI(title="ESP32 ECDH Key Exchange Server")

# ==================== CẤU HÌNH (Đã sửa cho Docker) ====================
MQTT_BROKER = os.getenv("MQTT_BROKER", "mosquitto") 
MQTT_PORT = 1883
MQTT_TOPIC = "esp32/data"

MQTT_USER = os.getenv("MQTT_USER", "admin")
MQTT_PASS = os.getenv("MQTT_PASS", "123456")

# ĐƯỜNG DẪN FILE KEY (Quan trọng để share với Decoder)
# Mặc định là /shared/aes_key.bin nếu chạy trong Docker
KEY_PATH = os.getenv("KEY_PATH", "/shared/aes_key.bin")

# ==================== LOGIC CRYPTO & SERVER ====================
laptop_private_key = ec.generate_private_key(ec.SECP256R1())
laptop_public_key = laptop_private_key.public_key()
pub_bytes = laptop_public_key.public_bytes(
    encoding=serialization.Encoding.X962,
    format=serialization.PublicFormat.UncompressedPoint
)
laptop_pub_64 = pub_bytes[1:]
laptop_pub_hex = laptop_pub_64.hex().upper()

derived_aes_key = None
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

@app.post("/exchange")
async def exchange_key(request: Request):
    global derived_aes_key
    try:
        data = await request.json()
        esp32_pub_hex = data.get("publicKey")
        if not esp32_pub_hex:
            return JSONResponse({"error": "Missing publicKey"}, status_code=400)

        print(f"\n[HTTP] Nhận Key từ ESP32: {esp32_pub_hex[:10]}...")
        
        # Tính toán Shared Secret
        esp32_pub_bytes = bytes.fromhex("04" + esp32_pub_hex)
        esp32_public_key = ec.EllipticCurvePublicKey.from_encoded_point(
            ec.SECP256R1(), esp32_pub_bytes
        )
        shared_secret = laptop_private_key.exchange(ec.ECDH(), esp32_public_key)
        
        # KDF SHA-256
        digest = hashes.Hash(hashes.SHA256())
        digest.update(shared_secret)
        derived_aes_key = digest.finalize() # Key dạng bytes (32 bytes)

        # ==================== [MỚI] LƯU KEY RA FILE ====================
        try:
            # Đảm bảo thư mục tồn tại (phòng hờ)
            os.makedirs(os.path.dirname(KEY_PATH), exist_ok=True)
            
            # Ghi file dạng binary ('wb')
            with open(KEY_PATH, "wb") as f:
                f.write(derived_aes_key)
            
            print(f"[SYSTEM] >>> Đã lưu AES Key vào: {KEY_PATH}")
            print(f"[SYSTEM] >>> Hex Key (để debug): {derived_aes_key.hex()}")
        except Exception as file_err:
            print(f"[ERROR] Không thể ghi file key: {file_err}")
        # ===============================================================
        
        print("[HTTP] Key Exchange Success! Ready to decrypt.")
        return JSONResponse({"publicKey": laptop_pub_hex})
    except Exception as e:
        print(f"Error: {e}")
        return JSONResponse({"error": str(e)}, status_code=500)

# ==================== MQTT LOGIC (Backend chỉ nên subscribe để debug) ====================
# Lưu ý: Nếu bạn đã có service 'decoder' riêng, bạn có thể xóa phần MQTT ở đây 
# để tránh 2 bên cùng in log gây rối. Nhưng giữ lại để test cũng không sao.

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(f"[MQTT] Connected to Broker at {MQTT_BROKER}")
        client.subscribe(MQTT_TOPIC)
    else:
        print(f"[MQTT] Connect failed with code {rc}")

def on_message(client, userdata, msg):
    # Nếu chưa có key trong RAM thì thử đọc lại từ file (phòng trường hợp restart)
    global derived_aes_key
    if derived_aes_key is None:
        if os.path.exists(KEY_PATH):
            with open(KEY_PATH, "rb") as f:
                derived_aes_key = f.read()
                print(f"[MQTT] Đã load lại key từ file: {KEY_PATH}")
        else:
            print("[MQTT] Ignored message (No key established)")
            return

    try:
        payload = msg.payload.decode('utf-8')
        data = json.loads(payload)
        
        iv = base64.b64decode(data['iv'])
        tag = base64.b64decode(data['tag'])
        ciphertext = base64.b64decode(data['ciphertext'])

        decryptor = Cipher(
            algorithms.AES(derived_aes_key),
            modes.GCM(iv, tag),
            backend=default_backend()
        ).decryptor()

        plaintext = decryptor.update(ciphertext) + decryptor.finalize()
        print(f"\n>>> [Backend] DECRYPTED: {plaintext.decode('utf-8')}")

    except Exception as e:
        print(f"[MQTT] Decryption Failed: {e}")

# ==================== MAIN ====================
def start_mqtt():
    mqtt_client.username_pw_set(MQTT_USER, MQTT_PASS)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    
    print("[SYSTEM] Connecting to MQTT Broker...")
    while True:
        try:
            mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
            mqtt_client.loop_start()
            break
        except Exception:
            print(f"[MQTT] Waiting for Broker at {MQTT_BROKER}...")
            time.sleep(2)

if __name__ == "__main__":

    uvicorn.run(app, host="0.0.0.0", port=8000)
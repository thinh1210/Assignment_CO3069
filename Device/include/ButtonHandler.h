#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>

class ButtonHandler
{
private:
    uint8_t _pin;
    bool _activeLow;           // True nếu dùng INPUT_PULLUP (Nhấn = LOW)
    uint32_t _longPressTimeMs; // Thời gian quy định là Long Press
    uint32_t _debounceTimeMs;  // Thời gian chống rung (thường là 50ms)

    // Biến trạng thái
    bool _lastReading;          // Trạng thái đọc thô lần trước
    bool _currentState;         // Trạng thái ổn định hiện tại (đã debounce)
    uint32_t _lastDebounceTime; // Mốc thời gian bắt đầu chống rung
    uint32_t _pressStartTime;   // Mốc thời gian bắt đầu nhấn

    // Cờ sự kiện (Flags)
    bool _isJustPressed;      // Cờ báo: Vừa mới nhấn (One-shot)
    bool _isLongPressed;      // Cờ báo: Đã đạt ngưỡng Long Press (One-shot)
    bool _longPressTriggered; // Cờ khóa: Đảm bảo Long Press chỉ báo 1 lần mỗi lần nhấn

public:
    // Constructor
    // pin: Chân GPIO
    // longPressMs: Thời gian giữ để tính là Long Press (mặc định 2000ms)
    // activeLow: true nếu nút nối đất (GND), false nếu nối nguồn (VCC)
    ButtonHandler(uint8_t pin, uint32_t longPressMs = 2000, bool activeLow = true);

    void begin();

    // Hàm này phải được gọi liên tục trong loop() hoặc Task
    void loop();

    // Kiểm tra sự kiện (Trả về true 1 lần duy nhất khi sự kiện xảy ra)
    bool isJustPressed(); // Phát hiện ngay khi vừa nhấn xuống
    bool isLongPressed(); // Phát hiện khi đã giữ đủ thời gian

    // Kiểm tra trạng thái hiện tại (Realtime)
    bool isPressedRaw(); // Đang giữ nút?
};

#endif
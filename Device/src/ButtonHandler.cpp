#include "ButtonHandler.h"

ButtonHandler::ButtonHandler(uint8_t pin, uint32_t longPressMs, bool activeLow)
{
    _pin = pin;
    _longPressTimeMs = longPressMs;
    _activeLow = activeLow;
    _debounceTimeMs = 50; // 50ms là đủ để chống rung cơ học

    _lastReading = !activeLow; // Mặc định trạng thái ban đầu là chưa nhấn
    _currentState = !activeLow;
    _lastDebounceTime = 0;
    _pressStartTime = 0;

    _isJustPressed = false;
    _isLongPressed = false;
    _longPressTriggered = false;
}

void ButtonHandler::begin()
{
    if (_activeLow)
    {
        pinMode(_pin, INPUT_PULLUP);
    }
    else
    {
        pinMode(_pin, INPUT);
    }
}

void ButtonHandler::loop()
{
    // 1. Đọc trạng thái vật lý
    int reading = digitalRead(_pin);
    bool pressedState = _activeLow ? LOW : HIGH; // Trạng thái logic khi nhấn

    // 2. Xử lý chống rung (Debounce Logic)
    if (reading != _lastReading)
    {
        _lastDebounceTime = millis(); // Reset timer khi tín hiệu thay đổi
    }
    _lastReading = reading;

    // Nếu tín hiệu ổn định đủ lâu (> debounceTime)
    if ((millis() - _lastDebounceTime) > _debounceTimeMs)
    {

        // Nếu trạng thái ổn định khác với trạng thái hiện tại của hệ thống
        if (reading != _currentState)
        {
            _currentState = reading;

            // --- SỰ KIỆN NHẤN XUỐNG (FALLING EDGE cho ActiveLow) ---
            if (_currentState == pressedState)
            {
                _isJustPressed = true;       // Bật cờ "Vừa nhấn"
                _pressStartTime = millis();  // Ghi lại thời gian bắt đầu
                _longPressTriggered = false; // Reset cờ Long Press
            }
        }
    }

    // 3. Xử lý Long Press
    // Nếu đang nhấn VÀ chưa báo Long Press lần nào VÀ thời gian giữ > ngưỡng
    if ((_currentState == pressedState) && !_longPressTriggered)
    {
        if ((millis() - _pressStartTime) >= _longPressTimeMs)
        {
            _isLongPressed = true;      // Bật cờ "Long Press"
            _longPressTriggered = true; // Khóa lại để không báo liên tục
        }
    }
}

// Hàm lấy cờ và tự động reset cờ (Read-and-Clear)
bool ButtonHandler::isJustPressed()
{
    if (_isJustPressed)
    {
        _isJustPressed = false;
        return true;
    }
    return false;
}

bool ButtonHandler::isLongPressed()
{
    if (_isLongPressed)
    {
        _isLongPressed = false;
        return true;
    }
    return false;
}

bool ButtonHandler::isPressedRaw()
{
    bool pressedState = _activeLow ? LOW : HIGH;
    return (_currentState == pressedState);
}
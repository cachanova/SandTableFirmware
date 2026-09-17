#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class SemaphoreGuard {
public:
    explicit SemaphoreGuard(SemaphoreHandle_t mutex) : m_mutex(mutex) {
        if (m_mutex) xSemaphoreTake(m_mutex, portMAX_DELAY);
    }
    ~SemaphoreGuard() {
        if (m_mutex) xSemaphoreGive(m_mutex);
    }
    SemaphoreGuard(const SemaphoreGuard&) = delete;
    SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;
private:
    SemaphoreHandle_t m_mutex;
};


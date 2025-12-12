#include <Arduino.h>
#include <FastLED.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>  // <-- ¡IMPORTANTE! Para semáforos

// Configuración de LEDs
#define NUM_LEDS 16
#define DATA_PIN PA7  // Pin para WS2812B
#define BRIGHTNESS 64

CRGB leds[NUM_LEDS];

// Variables para animaciones
TaskHandle_t Task1, Task2;
SemaphoreHandle_t xSemaphore = NULL;  // Cambiado de 'sSemaphore' a 'xSemaphore'

// Declaración de tareas
void TaskAnimation1(void *pvParameters);
void TaskAnimation2(void *pvParameters);

void setup() {
  Serial.begin(115200);
  delay(1000); // Espera para estabilizar
  
  // Inicializar FastLED
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  
  // Crear semáforo para acceso seguro a los LEDs
  xSemaphore = xSemaphoreCreateMutex();
  
  if (xSemaphore != NULL) {
    // Crear tareas de FreeRTOS
    xTaskCreate(TaskAnimation1, "Anim1", 1024, NULL, 1, &Task1);
    xTaskCreate(TaskAnimation2, "Anim2", 1024, NULL, 1, &Task2);
    
    Serial.println("Tareas creadas correctamente");
  } else {
    Serial.println("Error creando semáforo");
  }
  
  // Iniciar scheduler de FreeRTOS
  vTaskStartScheduler();
}

void loop() {
  // FreeRTOS maneja las tareas, este loop no se usa
  vTaskDelete(NULL);
}

// Tarea 1: Efecto arcoíris
void TaskAnimation1(void *pvParameters) {
  (void)pvParameters;
  uint8_t hue = 0;
  
  while (1) {
    // CORRECCIÓN: Usa xSemaphoreTake (no sSemaphoreTake)
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
      // Llenar con arcoíris
      fill_rainbow(leds, NUM_LEDS, hue++, 7);
      FastLED.show();
      xSemaphoreGive(xSemaphore);  // CORRECCIÓN: Usa xSemaphoreGive
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// Tarea 2: Efecto respiración en rojo
void TaskAnimation2(void *pvParameters) {
  (void)pvParameters;
  uint8_t brightness = 0;
  bool increasing = true;
  
  while (1) {
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
      // Efecto respiración
      if (increasing) {
        brightness += 5;
        if (brightness >= 255) increasing = false;
      } else {
        brightness -= 5;
        if (brightness <= 30) increasing = true;
      }
      
      fill_solid(leds, NUM_LEDS, CRGB(brightness, 0, 0));
      FastLED.show();
      xSemaphoreGive(xSemaphore);  // CORRECCIÓN: Usa xSemaphoreGive
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
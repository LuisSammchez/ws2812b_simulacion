#include <Arduino.h>
#include <FastLED.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ==================== CONFIGURACIÓN ====================
#define NUM_LEDS 30
#define DATA_PIN 13
#define BRIGHTNESS 100
#define MQTT_TOPIC "led_control"
#define MQTT_TOPIC_STATUS "led_status"

// ==================== VARIABLES GLOBALES ====================
CRGB leds[NUM_LEDS];
SemaphoreHandle_t xSemaphore = NULL;

// Estados de animación (cada uno puede estar activo/inactivo)
typedef struct {
    bool reverseEnabled;      // B - Reversa (blanco)
    bool intermittentEnabled; // I - Intermitentes (ámbar)
    bool leftEnabled;         // L - Izquierda
    bool rightEnabled;        // R - Derecha
    bool stopEnabled;         // S - Alto (rojo)
    bool defaultEnabled;      // Animación RGB por defecto
} AnimationStates;

AnimationStates animStates = {false, false, false, false, false, true};

// Variables MQTT/WiFi
const char* ssid = "TU_SSID";
const char* password = "TU_PASSWORD";
const char* mqtt_server = "broker.hivemq.com"; // Broker público gratuito
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP32_LED_Controller";

WiFiClient espClient;
PubSubClient client(espClient);

// ==================== DECLARACIÓN DE TAREAS ====================
void TaskDefaultAnimation(void *pvParameters);
void TaskReverseAnimation(void *pvParameters);
void TaskIntermittentAnimation(void *pvParameters);
void TaskLeftAnimation(void *pvParameters);
void TaskRightAnimation(void *pvParameters);
void TaskStopAnimation(void *pvParameters);
void TaskMQTTHandler(void *pvParameters);

// Handles para las tareas
TaskHandle_t hTaskDefault = NULL;
TaskHandle_t hTaskReverse = NULL;
TaskHandle_t hTaskIntermittent = NULL;
TaskHandle_t hTaskLeft = NULL;
TaskHandle_t hTaskRight = NULL;
TaskHandle_t hTaskStop = NULL;
TaskHandle_t hTaskMQTT = NULL;

// ==================== FUNCIONES AUXILIARES ====================
void connectToWiFi() {
    Serial.print("Conectando a WiFi");
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\nConectado a WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Mensaje recibido [");
    Serial.print(topic);
    Serial.print("]: ");
    
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println(message);
    
    // Procesar comando
    if (message.length() > 0) {
        char command = message.charAt(0);
        
        // Toggle de estados
        switch(command) {
            case 'B': // Reversa
                animStates.reverseEnabled = !animStates.reverseEnabled;
                Serial.println(animStates.reverseEnabled ? "Reversa ACTIVADA" : "Reversa DESACTIVADA");
                break;
                
            case 'I': // Intermitentes
                animStates.intermittentEnabled = !animStates.intermittentEnabled;
                Serial.println(animStates.intermittentEnabled ? "Intermitentes ACTIVADOS" : "Intermitentes DESACTIVADOS");
                break;
                
            case 'L': // Izquierda
                animStates.leftEnabled = !animStates.leftEnabled;
                Serial.println(animStates.leftEnabled ? "Izquierda ACTIVADA" : "Izquierda DESACTIVADA");
                break;
                
            case 'R': // Derecha
                animStates.rightEnabled = !animStates.rightEnabled;
                Serial.println(animStates.rightEnabled ? "Derecha ACTIVADA" : "Derecha DESACTIVADA");
                break;
                
            case 'S': // Alto
                animStates.stopEnabled = !animStates.stopEnabled;
                Serial.println(animStates.stopEnabled ? "Alto ACTIVADO" : "Alto DESACTIVADO");
                break;
                
            default:
                Serial.println("Comando no reconocido");
                return;
        }
        
        // Si algún comando especial está activo, desactivar animación por defecto
        if (animStates.reverseEnabled || animStates.intermittentEnabled || 
            animStates.leftEnabled || animStates.rightEnabled || animStates.stopEnabled) {
            animStates.defaultEnabled = false;
        } else {
            animStates.defaultEnabled = true;
        }
        
        // Enviar estado actualizado
        sendStatusUpdate();
    }
}

void reconnectMQTT() {
    while (!client.connected()) {
        Serial.print("Intentando conexión MQTT...");
        
        if (client.connect(mqtt_client_id)) {
            Serial.println("Conectado al broker MQTT!");
            client.subscribe(MQTT_TOPIC);
        } else {
            Serial.print("Falló, rc=");
            Serial.print(client.state());
            Serial.println(" Intentando de nuevo en 5 segundos...");
            delay(5000);
        }
    }
}

void sendStatusUpdate() {
    StaticJsonDocument<256> doc;
    doc["default"] = animStates.defaultEnabled;
    doc["reverse"] = animStates.reverseEnabled;
    doc["intermittent"] = animStates.intermittentEnabled;
    doc["left"] = animStates.leftEnabled;
    doc["right"] = animStates.rightEnabled;
    doc["stop"] = animStates.stopEnabled;
    
    char buffer[256];
    serializeJson(doc, buffer);
    client.publish(MQTT_TOPIC_STATUS, buffer);
}

// ==================== TAREAS DE ANIMACIÓN ====================

// Tarea 1: Animación RGB por defecto
void TaskDefaultAnimation(void *pvParameters) {
    uint8_t hue = 0;
    
    while (1) {
        if (animStates.defaultEnabled) {
            if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
                fill_rainbow(leds, NUM_LEDS, hue++, 7);
                FastLED.show();
                xSemaphoreGive(xSemaphore);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Tarea 2: Reversa (Parpadear primeros y últimos 15 LEDs en blanco)
void TaskReverseAnimation(void *pvParameters) {
    bool state = false;
    
    while (1) {
        if (animStates.reverseEnabled) {
            if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
                CRGB color = state ? CRGB::White : CRGB::Black;
                
                // Primeros 15 LEDs
                for (int i = 0; i < 15 && i < NUM_LEDS; i++) {
                    leds[i] = color;
                }
                
                // Últimos 15 LEDs
                for (int i = NUM_LEDS - 1; i >= NUM_LEDS - 15 && i >= 0; i--) {
                    leds[i] = color;
                }
                
                FastLED.show();
                xSemaphoreGive(xSemaphore);
                
                state = !state;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // Parpadeo cada 500ms
    }
}

// Tarea 3: Intermitentes (Parpadear primeros y últimos 15 LEDs en ámbar)
void TaskIntermittentAnimation(void *pvParameters) {
    bool state = false;
    
    while (1) {
        if (animStates.intermittentEnabled) {
            if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
                CRGB color = state ? CRGB(255, 100, 0) : CRGB::Black; // Ámbar
                
                // Primeros 15 LEDs
                for (int i = 0; i < 15 && i < NUM_LEDS; i++) {
                    leds[i] = color;
                }
                
                // Últimos 15 LEDs
                for (int i = NUM_LEDS - 1; i >= NUM_LEDS - 15 && i >= 0; i--) {
                    leds[i] = color;
                }
                
                FastLED.show();
                xSemaphoreGive(xSemaphore);
                
                state = !state;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300)); // Parpadeo más rápido
    }
}

// Tarea 4: Animación direccional izquierda usando dos LEDs
void TaskLeftAnimation(void *pvParameters) {
    int position = 0;
    
    while (1) {
        if (animStates.leftEnabled) {
            if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
                // Apagar todos los LEDs
                fill_solid(leds, NUM_LEDS, CRGB::Black);
                
                // Encender dos LEDs consecutivos
                if (position < NUM_LEDS - 1) {
                    leds[position] = CRGB::Blue;
                    leds[position + 1] = CRGB::Blue;
                }
                
                FastLED.show();
                xSemaphoreGive(xSemaphore);
                
                position++;
                if (position >= NUM_LEDS - 1) {
                    position = 0;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Velocidad de animación
    }
}

// Tarea 5: Animación direccional derecha usando dos LEDs
void TaskRightAnimation(void *pvParameters) {
    int position = NUM_LEDS - 1;
    
    while (1) {
        if (animStates.rightEnabled) {
            if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
                // Apagar todos los LEDs
                fill_solid(leds, NUM_LEDS, CRGB::Black);
                
                // Encender dos LEDs consecutivos
                if (position > 0) {
                    leds[position] = CRGB::Green;
                    leds[position - 1] = CRGB::Green;
                }
                
                FastLED.show();
                xSemaphoreGive(xSemaphore);
                
                position--;
                if (position <= 0) {
                    position = NUM_LEDS - 1;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Velocidad de animación
    }
}

// Tarea 6: Alto (Encender primeros y últimos 15 LEDs en rojo)
void TaskStopAnimation(void *pvParameters) {
    while (1) {
        if (animStates.stopEnabled) {
            if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
                // Encender primeros 15 LEDs en rojo
                for (int i = 0; i < 15 && i < NUM_LEDS; i++) {
                    leds[i] = CRGB::Red;
                }
                
                // Encender últimos 15 LEDs en rojo
                for (int i = NUM_LEDS - 1; i >= NUM_LEDS - 15 && i >= 0; i--) {
                    leds[i] = CRGB::Red;
                }
                
                FastLED.show();
                xSemaphoreGive(xSemaphore);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Tarea 7: Manejo MQTT
void TaskMQTTHandler(void *pvParameters) {
    while (1) {
        if (!client.connected()) {
            reconnectMQTT();
        }
        client.loop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ==================== SETUP PRINCIPAL ====================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("=== SISTEMA DE CONTROL LED POR NUBE ===");
    
    // Inicializar FastLED
    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    
    // Crear semáforo para sincronización
    xSemaphore = xSemaphoreCreateMutex();
    
    // Conectar a WiFi
    connectToWiFi();
    
    // Configurar MQTT
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    
    // Crear todas las tareas de animación
    xTaskCreatePinnedToCore(
        TaskDefaultAnimation,   // Función de la tarea
        "DefaultAnim",          // Nombre
        4096,                   // Stack size
        NULL,                   // Parámetros
        1,                      // Prioridad
        &hTaskDefault,          // Handle
        1                       // Núcleo
    );
    
    xTaskCreatePinnedToCore(
        TaskReverseAnimation,
        "ReverseAnim",
        2048,
        NULL,
        2,
        &hTaskReverse,
        1
    );
    
    xTaskCreatePinnedToCore(
        TaskIntermittentAnimation,
        "IntermittentAnim",
        2048,
        NULL,
        2,
        &hTaskIntermittent,
        1
    );
    
    xTaskCreatePinnedToCore(
        TaskLeftAnimation,
        "LeftAnim",
        2048,
        NULL,
        2,
        &hTaskLeft,
        1
    );
    
    xTaskCreatePinnedToCore(
        TaskRightAnimation,
        "RightAnim",
        2048,
        NULL,
        2,
        &hTaskRight,
        1
    );
    
    xTaskCreatePinnedToCore(
        TaskStopAnimation,
        "StopAnim",
        2048,
        NULL,
        2,
        &hTaskStop,
        1
    );
    
    xTaskCreatePinnedToCore(
        TaskMQTTHandler,
        "MQTTHandler",
        8192,                   // Más stack para MQTT
        NULL,
        3,                      // Prioridad más alta
        &hTaskMQTT,
        0                       // Núcleo 0
    );
    
    Serial.println("Sistema inicializado. Esperando comandos MQTT...");
    Serial.println("Comandos disponibles: B, I, L, R, S");
}

void loop() {
    // FreeRTOS maneja todas las tareas
    vTaskDelete(NULL);
}
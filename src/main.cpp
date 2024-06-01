#include <WiFi.h>
#include <HTTPClient.h>
#include "Adafruit_SHT4x.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

const char *ssid = "Horizons";
const char *password = "crooked-moose";
const char *serverUrl = "http://192.168.1.85:8081/data";

Adafruit_SHT4x sht4 = Adafruit_SHT4x();

void get_network_info()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("[*] Network information for ");
        Serial.println(ssid);

        Serial.println("[+] BSSID : " + WiFi.BSSIDstr());
        Serial.print("[+] Gateway IP : ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("[+] Subnet Mask : ");
        Serial.println(WiFi.subnetMask());
        Serial.println((String) "[+] RSSI : " + WiFi.RSSI() + " dB");
        Serial.print("[+] ESP32 IP : ");
        Serial.println(WiFi.localIP());
    }
}

Adafruit_SHT4x sht4 = Adafruit_SHT4x();

// Buffer to hold collected data
#define BUFFER_SIZE 5000 // Adjust size as necessary
String dataBuffer = "";
SemaphoreHandle_t bufferMutex;

void collectDataTask(void *pvParameters)
{
    for (;;)
    {
        sensors_event_t humidity, temp;
        sht4.getEvent(&humidity, &temp);

        // Printing to the serial monitor
        Serial.print("Temperature: ");
        Serial.print(temp.temperature);
        Serial.println(" degrees C");
        Serial.print("Humidity: ");
        Serial.print(humidity.relative_humidity);
        Serial.println("% rH");

        // Collect data into buffer
        if (xSemaphoreTake(bufferMutex, portMAX_DELAY) == pdTRUE)
        {
            String data = "{\"temperature\": " + String(temp.temperature) + ", \"humidity\": " + String(humidity.relative_humidity) + "}";

            if (dataBuffer.length() > 0)
            {
                dataBuffer += ",";
            }
            dataBuffer += data;
            xSemaphoreGive(bufferMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Collect data at a rate of 1 Hz
    }
}

void sendDataTask(void *pvParameters)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for 5 seconds

        if (WiFi.status() == WL_CONNECTED && xSemaphoreTake(bufferMutex, portMAX_DELAY) == pdTRUE)
        {
            if (dataBuffer.length() > 0)
            {
                HTTPClient http;
                http.begin(serverUrl);
                http.addHeader("Content-Type", "application/json");

                String payload = "[" + dataBuffer + "]";
                int httpResponseCode = http.POST(payload);

                if (httpResponseCode > 0)
                {
                    String response = http.getString();
                    Serial.println(httpResponseCode);
                    Serial.println(response);
                }
                else
                {
                    Serial.print("Error on sending POST: ");
                    Serial.println(httpResponseCode);
                }

                dataBuffer = ""; // Clear the buffer after sending
                http.end();
            }
            xSemaphoreGive(bufferMutex);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);

    Serial.println("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("Connected to WiFi");

    Serial.println("Adafruit SHT4x test");
    if (!sht4.begin())
    {
        Serial.println("Couldn't find SHT4x");
        while (1)
            delay(1);
    }
    Serial.println("Found SHT4x sensor");
    Serial.print("Serial number 0x");
    Serial.println(sht4.readSerial(), HEX);

    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);

    bufferMutex = xSemaphoreCreateMutex();
    if (bufferMutex == NULL)
    {
        Serial.println("Failed to create buffer mutex");
        while (1)
            delay(1);
    }

    xTaskCreatePinnedToCore(collectDataTask, "Collect Data Task", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(sendDataTask, "Send Data Task", 8192, NULL, 1, NULL, 1);
}
#include <WiFi.h>
#include <HTTPClient.h>
#include "Adafruit_SHT4x.h"
#include "Adafruit_TSL2591.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

const char *ssid = "Horizons";
const char *password = "crooked-moose";
const char *serverUrl = "http://micr0.dev:8080/data";

Adafruit_SHT4x sht4 = Adafruit_SHT4x();
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591);

// Buffer to hold collected data
#define BUFFER_SIZE 5000 // Adjust size as necessary
String dataBuffer = "";
SemaphoreHandle_t bufferMutex;

// Sensor enable flags
bool enableSHT4x = false;
bool enableTSL2591 = true;

// Luminosity level
uint8_t TSL2591level = 1;

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

void configureSensors()
{
    if (enableSHT4x)
    {
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
    }

    if (enableTSL2591)
    {
        if (!tsl.begin())
        {
            Serial.println("Couldn't find TSL2591");
            while (1)
                delay(1);
        }
        Serial.println("Found TSL2591 sensor");
        Serial.print("ID 0x");
        Serial.println(2591, HEX);
        tsl.setGain(TSL2591_GAIN_MED);                // Default gain
        tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS); // Default integration time
    }
}

void configureTSL2591()
{
    tsl.setGain(TSL2591_GAIN_MED);                // Default gain
    tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS); // Default integration time

    Serial.println("------------------------------------");
    Serial.print("Gain:          ");
    tsl2591Gain_t gain = tsl.getGain();

    Serial.print("Timing:        ");
    Serial.print((tsl.getTiming() + 1) * 100, DEC);
    Serial.println(" ms");
    Serial.println("------------------------------------");
    Serial.println("");
}

void adjustTSL2591Settings(uint16_t initLuminosity)
{
    Serial.print("Initial luminosity: ");
    Serial.println(initLuminosity);

    bool adjust = false;
    if (initLuminosity < 10 && TSL2591level != 5)
    {
        TSL2591level++;
        adjust = true;
    }
    else if (initLuminosity > 100 && TSL2591level != 0)
    {
        TSL2591level--;
        adjust = true;
    }

    if (adjust)
    {
        Serial.print("Adjusting TSL2591 settings to level ");
        Serial.println(TSL2591level);

        switch (TSL2591level)
        {
        case 0:
            tsl.setGain(TSL2591_GAIN_LOW);
            tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);
            break;
        case 1:
            tsl.setGain(TSL2591_GAIN_MED);
            tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);
            break;
        case 2:
            tsl.setGain(TSL2591_GAIN_HIGH);
            tsl.setTiming(TSL2591_INTEGRATIONTIME_200MS);
            break;
        case 3:
            tsl.setGain(TSL2591_GAIN_MAX);
            tsl.setTiming(TSL2591_INTEGRATIONTIME_200MS);
            break;
        case 4:
            tsl.setGain(TSL2591_GAIN_MAX);
            tsl.setTiming(TSL2591_INTEGRATIONTIME_400MS);
            break;
        case 5:
            tsl.setGain(TSL2591_GAIN_MAX);
            tsl.setTiming(TSL2591_INTEGRATIONTIME_600MS);
            break;
        default:
            break;
        }
    }
}

void collectDataTask(void *pvParameters)
{
    sensors_event_t humidity, temp;

    for (;;)
    {
        if (enableSHT4x && sht4.getEvent(&humidity, &temp))
        {
            Serial.print("Temperature: ");
            Serial.print(temp.temperature);
            Serial.println(" degrees C");
            Serial.print("Humidity: ");
            Serial.print(humidity.relative_humidity);
            Serial.println("% rH");

            // Collect data into buffer
            if (xSemaphoreTake(bufferMutex, portMAX_DELAY) == pdTRUE)
            {
                String data = "{\"sensor\":\"SHT4x\", \"temperature\": " + String(temp.temperature) + ", \"humidity\": " + String(humidity.relative_humidity) + "}";
                if (dataBuffer.length() > 0)
                {
                    dataBuffer += ",";
                }
                dataBuffer += data;
                xSemaphoreGive(bufferMutex);
            }
        }

        if (enableTSL2591)
        {
            // Adjust settings based on initial luminosity

            uint32_t lum = tsl.getFullLuminosity();
            uint16_t ir, full, visible;
            ir = lum >> 16;
            full = lum & 0xFFFF;
            visible = full - ir;

            float lux = tsl.calculateLux(full, ir);

            Serial.print("[ ");
            Serial.print(millis());
            Serial.print(" ms ] ");
            Serial.print("Lux: ");
            Serial.println(lux);

            adjustTSL2591Settings(visible);

            // Collect data into buffer
            if (xSemaphoreTake(bufferMutex, portMAX_DELAY) == pdTRUE)
            {
                String data = "{\"sensor\":\"TSL2591\", \"lux\": " + String(lux) + "}";
                if (dataBuffer.length() > 0)
                {
                    dataBuffer += ",";
                }
                dataBuffer += data;
                xSemaphoreGive(bufferMutex);
            }
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

    get_network_info();

    bufferMutex = xSemaphoreCreateMutex();
    if (bufferMutex == NULL)
    {
        Serial.println("Failed to create buffer mutex");
        while (1)
            delay(1);
    }

    configureSensors();

    xTaskCreatePinnedToCore(collectDataTask, "Collect Data Task", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(sendDataTask, "Send Data Task", 8192, NULL, 1, NULL, 1);
}

void loop()
{
    // Empty loop
}
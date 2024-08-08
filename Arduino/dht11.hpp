//------------Config DHT11 --------------------
#pragma once
#include "DHT.h"

class DHT11Sensor {
private:
    DHT dht;
    float humidity;
    float temperatureC;
    float temperatureF;
    float heatIndexC;
    float heatIndexF;

public:
    DHT11Sensor(uint8_t pin, uint8_t type) : dht(pin, type), humidity(NAN), temperatureC(NAN), temperatureF(NAN), heatIndexC(NAN), heatIndexF(NAN) {dht.begin();}


    float getTempC() noexcept{
      return temperatureC;
    }

    float getTempHum() noexcept{
      return humidity;
    }

    void begin() noexcept{
        dht.begin();
    }

    void readData() noexcept{
        humidity = dht.readHumidity();
        temperatureC = dht.readTemperature();
        temperatureF = dht.readTemperature(true);

        Serial.println(temperatureC);

        if (isnan(humidity) || isnan(temperatureC) || isnan(temperatureF)) {
            Serial.println(F("Failed to read from DHT sensor!"));
            return;
        }

        heatIndexC = dht.computeHeatIndex(temperatureC, humidity, false);
        heatIndexF = dht.computeHeatIndex(temperatureF, humidity);
    }

    void printDataToSerial() noexcept{
        Serial.print(F("Влажность: "));
        Serial.print(humidity);
        Serial.print(F("%  Температура: "));
        Serial.print(temperatureC);
        Serial.print(F("°C "));
        Serial.print(temperatureF);
        Serial.print(F("°F  Индекс тепла: "));
        Serial.print(heatIndexC);
        Serial.print(F("°C "));
        Serial.print(heatIndexF);
        Serial.println(F("°F"));
    }
};

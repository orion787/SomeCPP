#include "dht11.hpp"
#include "event.hpp"
#include "relay.hpp"

//------------Config devices --------------------
#define DHTTYPE DHT11   // DHT 11

DHT11Sensor temp_sensors[4] = {DHT11Sensor(6, DHT11),DHT11Sensor(5, DHT11), DHT11Sensor(4, DHT11), DHT11Sensor(3, DHT11)};
Relay relays[4] = {Relay{7, 0}, Relay{8, 0}, Relay{9, 0}, Relay{10, 0}};


#define _LCD_TYPE 1 
#include <LCD_1602_RUS.h>
LCD_1602_RUS lcd(0x27, 16, 4);



SensorData sensor_data[4] = {SensorData{0.0f, 0.0f}, SensorData{0.0f, 0.0f}, SensorData{0.0f, 0.0f}, SensorData{0.0f, 0.0f}};
Event<SensorData> events[5] = {
  Event<SensorData>(
    [](const SensorData& data) { return data.temperature >= 25.0f; },
    []() { OnRelay(relays[0]); },
    []() { OffRelay(relays[0]); }
  ),
  Event<SensorData>(
    [](const SensorData& data) { return data.temperature <= 20.0f; },
    []() { OnRelay(relays[1]); },
    []() { OffRelay(relays[1]); }
  ),
  Event<SensorData>(
    [](const SensorData& data) { return data.temperature >= 25.0f; },
    []() { OnRelay(relays[2]); },
    []() { OffRelay(relays[2]); }
  ),
  Event<SensorData>(
    [](const SensorData& data) { return data.temperature <= 20.0f; },
    []() { OnRelay(relays[3]); },
    []() { OffRelay(relays[3]); }
  ),
  Event<SensorData>(
    [](const SensorData& data) { return data.temperature >= 25.0f; },
    []() { OnRelay(relays[4]); },
    []() { OffRelay(relays[4]); }
  )
};


void setup() {
  for(int i = 0; i < 4; ++i){
    temp_sensors[i].begin();
  }
  pinMode(7, OUTPUT);
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(10, OUTPUT);

  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
}


void loop() {
  for(int i = 0; i < 4; ++i){
    temp_sensors[i].readData();
    delay(50);
    sensor_data[i].temperature = temp_sensors[i].getTempC();
    sensor_data[i].humidity = temp_sensors[i].getTempHum();
    delay(50);
    events[i].handleEvent(sensor_data[i]);
  }

  //logging to lcd console
  for(int i = 0; i < 4; ++i){
    lcd.setCursor(0, i);
    lcd.print("S");
    lcd.print(i + 1);
    lcd.print(": ");
    lcd.print(sensor_data[i].temperature);
    lcd.print("C");
  }
  delay(2000);
}

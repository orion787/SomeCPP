#include "dht11.hpp"
#include "event.hpp"
#include "relay.hpp"

//------------Config devices --------------------
#define DHTTYPE DHT11   // DHT 11
//DHT11Sensor dht {5, DHTTYPE};

DHT11Sensor temp_sensors[5] = {DHT11Sensor(5, DHT11),DHT11Sensor(5, DHT11), DHT11Sensor(5, DHT11), DHT11Sensor(5, DHT11), DHT11Sensor(5, DHT11)};
Relay relays[5] = {Relay{7, 0}, Relay{8, 0}, Relay{7, 0}, Relay{7, 0}, Relay{7, 0}};


//I have only 1 instead of 5 
DHT11Sensor& dht = temp_sensors[0];

#define _LCD_TYPE 1 
#include <LCD_1602_RUS.h>
LCD_1602_RUS lcd(0x27, 16, 2);



SensorData sensor_data[5]{SensorData{0.0f, 0.0f}};
Event<SensorData> events[5] = {
  Event<SensorData>(
    [](const SensorData& data) { return data.temperature > 25.0; },
    []() { OnRelay(relays[0]); },
    []() { OffRelay(relays[0]); }
  ),
  Event<SensorData>(
    [](const SensorData& data) { return data.temperature <20.0; },
    []() { OnRelay(relays[1]); },
    []() { OffRelay(relays[1]); }
  ),
  Event<SensorData>(
    [](const SensorData& data) { return data.humidity > 60.0; },
    []() { OnRelay(relays[2]); },
    []() { OffRelay(relays[2]); }
  ),
  Event<SensorData>(
    [](const SensorData& data) { return data.humidity < 30.0; },
    []() { OnRelay(relays[3]); },
    []() { OffRelay(relays[3]); }
  ),
  Event<SensorData>(
    [](const SensorData& data) { return data.temperature > 30.0; },
    []() { OnRelay(relays[4]); },
    []() { OffRelay(relays[4]); }
  )
};


void setup() {
  pinMode(3, OUTPUT);
  pinMode(7, OUTPUT);
  pinMode(8, OUTPUT);

  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Z");
}


void loop() {

  for(int i = 0; i < 5; ++i){
    dht.readData();
    delay(50);
    sensor_data[i].temperature = dht.getTempC();
    sensor_data[i].humidity = dht.getTempHum();
    delay(50);
    events[i].handleEvent(sensor_data[i]);
  }


  lcd.setCursor(0, 0);
  lcd.print("S1");
  //lcd.print(i + 1);
  lcd.print(": Temp ");
  lcd.print(sensor_data[0].temperature);
  lcd.print("C");
  lcd.setCursor(0, 1);
  lcd.print("Hum ");
  lcd.print(sensor_data[0].humidity);
  lcd.print("% ");
  delay(1000);
}

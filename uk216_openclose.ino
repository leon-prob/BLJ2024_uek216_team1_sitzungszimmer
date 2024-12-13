#include <WiFi.h>
#include <PubSubClient.h>
 
// Pins für den HC-SR04
const int trigPin = 27; // Output
const int echoPin = 14; // Input
 
// WLAN- und MQTT-Konfiguration
const char* device_id = "HC-SR04";
const char* ssid = "GuestWLANPortal";
const char* mqtt_server = "10.10.2.127";
const char* topic1 = "zuerich/sitzungszimmer/door/distance";
const char* topic2 = "zuerich/sitzungszimmer/door/openclosed";
 
// WLAN- und MQTT-Objekte
WiFiClient espClient;
PubSubClient client(espClient);
 
bool openclosed = true;
 
void setup() {
  Serial.begin(115200); // Initialisiere die serielle Kommunikation
  setup_hc();           // Initialisiere den HC-SR04
  setup_wifi();         // Verbinde mit WLAN
  client.setServer(mqtt_server, 1883); // Konfiguriere den MQTT-Server
}
 
void setup_hc() {
  pinMode(trigPin, OUTPUT); // Trig-Pin als Ausgang
  pinMode(echoPin, INPUT);  // Echo-Pin als Eingang
}
 
void setup_wifi() {
  Serial.print("Connecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid); // Verbinde mit dem WLAN
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" done!");
}
 
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(device_id)) {
      Serial.println(" connected!");
      client.subscribe(topic1); // Abonniere das Topic
    } else {
      Serial.print(".");
      delay(500);
    }
  }
}
 
void loop() {
  if (!client.connected()) {
    reconnect();
  }
 
  // Messe die Entfernung mit dem HC-SR04
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
 
  long duration = pulseIn(echoPin, HIGH); // Dauer des Signals messen
  float distance = duration * 0.034 / 2; // Entfernung in cm berechnen
 
  // Abstand zur Konsole ausgeben
  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.println(" cm");
 
  // Status von openclosed basierend auf der Entfernung aktualisieren
  if (distance < 20) {
    openclosed = false;
  } else {
    openclosed = true;
  }
 
  // MQTT-Nachricht senden  
  char tempBuffer[10];
  sprintf(tempBuffer, "%.2f", distance); // Entfernung formatieren
  client.publish(topic1, tempBuffer);
 
  // Boolean als Text senden
  client.publish(topic2, openclosed ? "open" : "closed");
 
  client.loop(); // MQTT-Client aktiv halten
  delay(500);    // Pause zwischen Messungen
}
 
 
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Konfiguration
#define MAX_RESERVATIONS 100
const char* device_id = "Buchungssystem";
const char* ssid = "GuestWLANPortal";
const char* mqtt_server = "10.10.2.127";
const char* topic_reservation = "zuerich/sitzungszimmer/sommer/reservation";
const char* timeTopic = "zuerich/sitzungszimmer/sommer/time";
const char* statusTopic = "zuerich/sitzungszimmer/sommer/status";

// MQTT und Zeit
WiFiClient espClient;
PubSubClient client(espClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);

// Reservation-Datenstruktur
typedef struct {
    int startTime; // Format: HHMM
    int endTime;   // Format: HHMM
    char userName[50];
} Reservation;

Reservation reservations[MAX_RESERVATIONS];
int reservationCount = 0;

void setup() {
    Serial.begin(115200);

    // WLAN-Verbindung herstellen
    setup_wifi();

    // MQTT-Server konfigurieren
    client.setServer(mqtt_server, 1883);
    client.setCallback(mqttCallback);

    // NTP starten
    timeClient.begin();
}

void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();
    timeClient.update();

    // Zeit senden
    String currentTime = timeClient.getFormattedTime();
    client.publish(timeTopic, currentTime.c_str());

    // Reservierungsdaten senden
    sendVariableTopics();   // Einzelne Variablen senden

    // Systemstatus senden (offen oder besetzt)
    sendStatusUpdate();     // Status des Buchungssystems senden

    delay(5000); // Intervall für Updates
}

// Neue Funktion zum Senden des Systemstatus (offen oder besetzt)
void sendStatusUpdate() {
    String statusMessage = isRoomOccupied() ? "besetzt" : "offen";
    client.publish(statusTopic, statusMessage.c_str());
}

// Funktion zur Überprüfung, ob das Zimmer besetzt ist
bool isRoomOccupied() {
    int currentTime = getCurrentTime(); // Holt die aktuelle Zeit im Format HHMM
    for (int i = 0; i < reservationCount; i++) {
        if (currentTime >= reservations[i].startTime && currentTime < reservations[i].endTime) {
            return true; // Zimmer ist besetzt
        }
    }
    return false; // Zimmer ist offen
}

// Holt die aktuelle Zeit im Format HHMM
int getCurrentTime() {
    String formattedTime = timeClient.getFormattedTime();
    int hours = formattedTime.substring(0, 2).toInt();
    int minutes = formattedTime.substring(3, 5).toInt();
    return hours * 100 + minutes;
}

// Reservierungsdaten senden
void sendVariableTopics() {
    for (int i = 0; i < reservationCount; i++) {
        String userTopic = "zuerich/sitzungszimmer/reservation/" + String(i) + "/userName";
        String startTimeTopic = "zuerich/sitzungszimmer/reservation/" + String(i) + "/startTime";
        String endTimeTopic = "zuerich/sitzungszimmer/reservation/" + String(i) + "/endTime";

        client.publish(userTopic.c_str(), reservations[i].userName);
        client.publish(startTimeTopic.c_str(), String(reservations[i].startTime).c_str());
        client.publish(endTimeTopic.c_str(), String(reservations[i].endTime).c_str());
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char message[length + 1];
    strncpy(message, (char*)payload, length);
    message[length] = '\0'; // Nullterminierung

    if (strcmp(topic, topic_reservation) == 0) {
        Serial.printf("New MQTT message received on topic '%s': %s\n", topic, message);

        if (reservationCount >= MAX_RESERVATIONS) {
            client.publish("zuerich/sitzungszimmer/response", "Fehler: Maximale Anzahl an Reservierungen erreicht.");
            return;
        }

        Reservation newRes;
        int parsed = sscanf(message, "%49[^;];%d;%d", newRes.userName, &newRes.startTime, &newRes.endTime);
        if (parsed != 3 || newRes.startTime >= newRes.endTime) {
            client.publish("zuerich/sitzungszimmer/response", "Fehler: Ungültige Buchungsdaten.");
            return;
        }

        if (!isTimeAvailable(newRes.startTime, newRes.endTime)) {
            client.publish("zuerich/sitzungszimmer/response", "Fehler: Zeitfenster nicht verfügbar.");
            return;
        }

        reservations[reservationCount++] = newRes;
        sortReservations();
        client.publish("zuerich/sitzungszimmer/response", "Reservierung erfolgreich hinzugefügt.");

        // Neue Reservationsliste senden
        String reservationList = "Aktuelle Reservierungen:\n";
        for (int i = 0; i < reservationCount; i++) {
            reservationList += String(reservations[i].userName) + " " +
                               String(reservations[i].startTime) + "-" +
                               String(reservations[i].endTime) + "\n";
        }
        client.publish(topic_reservation, reservationList.c_str());
    }
}

void setup_wifi() {
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("WiFi connected");
}

void reconnect() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect("Reservation_Device")) {
            Serial.println("connected");
            client.subscribe(topic_reservation);
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

int isTimeAvailable(int start, int end) {
    for (int i = 0; i < reservationCount; i++) {
        if ((start < reservations[i].endTime) && (end > reservations[i].startTime)) {
            return 0; // Zeit nicht verfügbar
        }
    }
    return 1; // Zeit verfügbar
}

void sortReservations() {
    for (int i = 0; i < reservationCount - 1; i++) {
        for (int j = 0; j < reservationCount - i - 1; j++) {
            if (reservations[j].startTime > reservations[j + 1].startTime) {
                Reservation temp = reservations[j];
                reservations[j] = reservations[j + 1];
                reservations[j + 1] = temp;
            }
        }
    }
}

void processIncomingData(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, "zuerich/sitzungszimmer/reservation/update") == 0) {
        char message[length + 1];
        strncpy(message, (char*)payload, length);
        message[length] = '\0'; // Null-terminierte Zeichenkette

        // Beispiel: "John Doe;930;1030\nJane Smith;1100;1200"
        reservationCount = 0;
        char* line = strtok(message, "\n");
        while (line != NULL && reservationCount < MAX_RESERVATIONS) {
            sscanf(line, "%49[^;];%d;%d",
                   reservations[reservationCount].userName,
                   &reservations[reservationCount].startTime,
                   &reservations[reservationCount].endTime);
            reservationCount++;
            line = strtok(NULL, "\n");
        }
        Serial.println("Reservations updated from Node-RED!");
        listReservations(); // Optional: Zeige die neuen Reservierungen in der Konsole
    }
}

void listReservations() {
    Serial.println("Current Reservations:");
    for (int i = 0; i < reservationCount; i++) {
        Serial.printf("%s: %04d-%04d\n", reservations[i].userName, reservations[i].startTime, reservations[i].endTime);
    }
}

#include <WiFi.h>
#include <WebSocketsServer.h>
#include <driver/i2s.h>

// WiFi credentials
const char* ssid = "cam-test";
const char* password = "pomidor123";
const char* hostname = "JUDAS";

// I2S pins settings
const int I2S_WS = 25;
const int I2S_SD = 33;
const int I2S_SCK = 32;

// I2S settings
const int SAMPLE_RATE = 16000;
const int SAMPLE_BITS = 16;
const int BLOCK_SIZE = 512;

// WebSocket server on port 81
WebSocketsServer webSocket = WebSocketsServer(81);

// Audio transmission state
bool isAudioTransmitting = false;

// Button pin
const int BUTTON_PIN = 4;
bool lastButtonState = HIGH;

// I2S configuration
void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // Using only a left channel
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = BLOCK_SIZE,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };

    // Install I2S driver
    esp_err_t result = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (result != ESP_OK) {
        Serial.printf("I2S driver installation failed: %d\n", result);
        return;
    }

    // Set I2S pins
    result = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (result != ESP_OK) {
        Serial.printf("I2S pin configuration failed: %d\n", result);
        return;
    }

    Serial.println("I2S configured successfully");
}

// Web Socket Events handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        // Disconnection event
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        // Connection Event
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
            }
            break;
        // Text commands events
        case WStype_TEXT:
            {
                String message = String((char*)payload);
                Serial.printf("[%u] Received: %s\n", num, message);

                if (message == "start_audio") {
                    isAudioTransmitting = true;
                    Serial.println("ESP32 audio transmission started");
                } else if (message == "stop_audio") {
                    isAudioTransmitting = false;
                    Serial.println("ESP32 audio transmission stopped");
                }
                else if (message == "Opened") {
                    Serial.println("Door is now OPEN");
                } else if (message == "Closed") {
                    Serial.println("Door is now CLOSED");
                }
            }
            break;
        // Data received event
        // It was done only for testing purpoue and data should be directed to the speaker
        case WStype_BIN:
            {
                Serial.printf("[%u] Received %d bytes of audio data\n", num, length);

                int16_t* audioData = (int16_t*)payload;
                int numSamples = length / 2;
                long sum = 0;

                for (int i = 0; i < numSamples; i++) {
                    sum += (long)audioData[i] * audioData[i];
                }
                if (numSamples > 0) {
                    float rms = sqrt(sum / numSamples); 
                    float dB = 20 * log10(rms / 32767.0);
                    Serial.printf("Loudness: %.2f dB\n", dB);
                }
            }
            break;
    }
}

void setup() {
    // Initialize serial for debugging porpouses
    Serial.begin(115200);
    Serial.println("Starting...");

    // Configuring button pin
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // WiFi connection
    WiFi.setHostname(hostname);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // I2S configuration
    setupI2S();

    // Turning on Web Socket Server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.println("WebSocket server started");
}

void loop() {
    webSocket.loop();

    // Checking button state and sending text command to the client
    bool buttonState = digitalRead(BUTTON_PIN);
    if (buttonState == LOW && lastButtonState == HIGH) {
        webSocket.broadcastTXT("Ringing");
        Serial.println("Button pressed, sent 'Ringing' message");
    }
    lastButtonState = buttonState;

    // Audio transmition
    if (isAudioTransmitting) {
        static int16_t samples[BLOCK_SIZE];  // 16-bit samples
        size_t bytes_read = 0;

        // Reading I2S data and sending it througha Web Socket
        esp_err_t result = i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytes_read, portMAX_DELAY);
        if (result == ESP_OK && bytes_read > 0) {
            if (webSocket.connectedClients() > 0) {
              webSocket.broadcastBIN((uint8_t*)samples, bytes_read);
            }
        } else {
            Serial.printf("I2S read failed: %d\n", result);
        }
    }
}
/*  FSR Pressure Sensor - Serial Plotter Mode
 *  Output format: comma-separated values for Arduino Serial Plotter
 *  Wiring: 5V --[FSR]-- A0 --[10k]-- GND
 *  Serial Monitor: 115200 baud
 *  Tools -> Serial Plotter to view graph
 */

#define FSR_PIN A0
#define NUM_SAMPLES 5

void setup() {
    Serial.begin(115200);
    analogReadResolution(14);
}

int readFSR() {
    long sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        sum += analogRead(FSR_PIN);
        delayMicroseconds(200);
    }
    return (int)(sum / NUM_SAMPLES);
}

void loop() {
    int raw = readFSR();
    float v = raw * (5.0 / 16383.0);

    float rfsr = 0.0;
    float pressure = 0.0; 

    if (v > 0.01) {
        rfsr = 10000.0 * (5.0 - v) / v;
        if (rfsr > 0 && rfsr < 1000000.0) {
            pressure = 1000000.0 / rfsr;
        }
    }

    // Comma-separated: ADC, Voltage, Pressure
    Serial.print(raw);
    Serial.print(",");
    Serial.print(v, 2);
    Serial.print(",");
    Serial.println(pressure, 1);

    delay(50);
}

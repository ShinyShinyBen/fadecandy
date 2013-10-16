/*
 * Electrical test for Fadecandy boards
 * 
 * Copyright (c) 2013 Micah Elizabeth Scott
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <Arduino.h>
#include "electrical_test.h"
#include "testjig.h"
#include "arm_kinetis_reg.h"


float ElectricalTest::analogVolts(int pin)
{
    // Analog input and voltage divider constants
    const float reference = 1.2;
    const float dividerA = 1000;    // Input to ground
    const float dividerB = 6800;    // Input to signal
    const int adcMax = 1023;

    const float scale = (reference / adcMax) * ((dividerA + dividerB) / dividerA);
    return analogRead(pin) * scale;
}

bool ElectricalTest::analogThreshold(int pin, float nominal, float tolerance)
{
    // Measure an analog input, and verify it's close enough to expected values.

    float volts = analogVolts(pin);
    float lower = nominal - tolerance;
    float upper = nominal + tolerance;

    if (volts < lower || volts > upper) {
        target.log(LOG_ERROR,
                "ETEST: Analog value %d outside reference range! "
                "value = %.2fv, ref = %.2fv +/- %.2fv",
                pin, volts, nominal, tolerance);
        return false;
    }

    return true;
}

bool ElectricalTest::testOutputPattern(uint8_t bits)
{
    // Set the target's 8-bit output port to the given value, and check all analog values

    // Write the port all at once
    if (!target.digitalWritePort(outPin(0), bits))
        return false;

    // Check power supply each time
    if (!analogThreshold(analogTarget33vPin, 3.3)) return false;
    if (!analogThreshold(analogTargetVUsbPin, 5.0)) return false;

    // Check all data signal levels
    for (unsigned n = 0; n < 8; n++) {
        bool bit = (bits >> n) & 1;
        if (!analogThreshold(n, bit ? 5.0 : 0.0))
            return false;
    }

    return true;
}

bool ElectricalTest::testAllOutputPatterns()
{
    target.log(logLevel, "ETEST: Testing data output patterns");

    // All on, all off
    if (!testOutputPattern(0x00)) return false;
    if (!testOutputPattern(0xFF)) return false;

    // One bit set
    for (unsigned n = 0; n < 8; n++) {
        if (!testOutputPattern(1 << n))
            return false;
    }

    // One bit missing
    for (unsigned n = 0; n < 8; n++) {
        if (!testOutputPattern(0xFF ^ (1 << n)))
            return false;
    }

    // Leave all outputs on
    return testOutputPattern(0xFF);
}

bool ElectricalTest::initTarget()
{
    // Target setup that's needed only once per test run

    // Output pin directions
    for (unsigned n = 0; n < 8; n++) {
        if (!target.pinMode(outPin(n), OUTPUT))
            return false;
    }

    // Disable target USB USB pull-ups
    if (!target.usbSetPullup(false))
        return false;

    return true;
}

void ElectricalTest::setPowerSupplyVoltage(float volts)
{
    // Set the variable power supply voltage. Usable range is from 0V to system VUSB.

    int pwm = constrain(volts * (255 / powerSupplyFullScaleVoltage), 0, 255);
    pinMode(powerPWMPin, OUTPUT);
    analogWriteFrequency(powerPWMPin, 1000000);
    analogWrite(powerPWMPin, pwm);

    /*
     * Time for the PSU to settle. Our testjig's power supply settles very
     * fast (<1ms), but the capacitors on the target need more time to charge.
     */
    delay(30);
}

void ElectricalTest::powerOff()
{
    setPowerSupplyVoltage(0);
}

bool ElectricalTest::powerOn()
{
    target.log(logLevel, "ETEST: Enabling power supply");
    const float volts = 5.0;
    setPowerSupplyVoltage(volts);
    return analogThreshold(analogTargetVUsbPin, volts);
}

bool ElectricalTest::testHighZ(int pin)
{
    // Test a pin to make sure it's high-impedance, by using its parasitic capacitance
    for (unsigned i = 0; i < 10; i++) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, i & 1);
        pinMode(pin, INPUT);
        if (digitalRead(pin) != (i & 1))
            return false;
    }
    return true;
}

bool ElectricalTest::testPull(int pin, bool state)
{
    // Test a pin for a pull-up/down resistor
    for (unsigned i = 0; i < 10; i++) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, i & 1);
        pinMode(pin, INPUT);
        if (digitalRead(pin) != state)
            return false;
    }
    return true;
}    

bool ElectricalTest::testUSBConnections()
{
    target.log(logLevel, "ETEST: Testing USB connections");

    // Run this test a few times
    for (unsigned iter = 0; iter < 4; iter++) {

        // Start with pull-up disabled
        if (!target.usbSetPullup(false))
            return false;

        // Test both USB ground connections
        pinMode(usbShieldGroundPin, INPUT_PULLUP);
        pinMode(usbSignalGroundPin, INPUT_PULLUP);
        if (digitalRead(usbShieldGroundPin) != LOW) {
            target.log(LOG_ERROR, "ETEST: Faulty USB shield ground");
            return false;
        }
        if (digitalRead(usbSignalGroundPin) != LOW) {
            target.log(LOG_ERROR, "ETEST: Faulty USB signal ground");
            return false;
        }

        // Test for a high-impedance USB D+ and D- by charging and discharging parasitic capacitance
        if (!testHighZ(usbDMinusPin)) {
            target.log(LOG_ERROR, "ETEST: Fault on USB D-, expected High-Z");
            return false;
        }
        if (!testHighZ(usbDPlusPin)) {
            target.log(LOG_ERROR, "ETEST: Fault on USB D+, expected High-Z");
            return false;
        }

        // Turn on USB pull-up on D+
        if (!target.usbSetPullup(true))
            return false;

        // Now D+ should be pulled up, and D- needs to still be high-Z
        if (!testPull(usbDPlusPin, HIGH)) {
            target.log(LOG_ERROR, "ETEST: Fault on USB D+, no pull-up found");
            return false;
        }
        if (!testHighZ(usbDMinusPin)) {
            target.log(LOG_ERROR, "ETEST: Fault on USB D-, expected High-Z. Possible short to D+");
            return false;
        }

    }

    return true;
}

bool ElectricalTest::runAll()
{
    target.log(logLevel, "ETEST: Beginning electrical test");

    if (!initTarget())
        return false;

    // USB tests
    if (!testUSBConnections())
        return false;

    // Output patterns
    if (!testAllOutputPatterns())
        return false;

    target.log(logLevel, "ETEST: Successfully completed electrical test");
    return true;
}
